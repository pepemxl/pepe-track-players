#include "HomographyManager.h"

#include <QJsonArray>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPitchW = 105.0;
constexpr double kPitchH = 68.0;

struct Landmark { const char *key; const char *label; double px; double py; };

// Standard landmarks on a 105 x 68 m pitch (origin top-left; x along the
// 105 m length, y along the 68 m width). Boxes: penalty area 16.5 m deep /
// 40.32 m wide (y 13.84..54.16); goal area 5.5 m / 18.32 m (y 24.84..43.16);
// penalty spots 11 m from the goal line; centre circle radius 9.15 m.
const std::vector<Landmark> &landmarkCatalog()
{
    static const std::vector<Landmark> k = {
        { "tl", "Corner ↖ (TL)", 0.0,   0.0 },
        { "tr", "Corner ↗ (TR)", 105.0, 0.0 },
        { "br", "Corner ↘ (BR)", 105.0, 68.0 },
        { "bl", "Corner ↙ (BL)", 0.0,   68.0 },
        { "half_top", "Halfway × top touchline",    52.5, 0.0 },
        { "half_bot", "Halfway × bottom touchline", 52.5, 68.0 },
        { "center",   "Centre spot",            52.5, 34.0 },
        { "circ_top", "Centre circle top",      52.5, 24.85 },
        { "circ_bot", "Centre circle bottom",   52.5, 43.15 },
        { "lpen", "Left penalty spot",  11.0, 34.0 },
        { "rpen", "Right penalty spot", 94.0, 34.0 },
        { "lpa_tl", "L penalty-area ↖ (goal line)", 0.0,  13.84 },
        { "lpa_tr", "L penalty-area top (18-yd)",        16.5, 13.84 },
        { "lpa_br", "L penalty-area bottom (18-yd)",     16.5, 54.16 },
        { "lpa_bl", "L penalty-area ↙ (goal line)", 0.0,  54.16 },
        { "rpa_tr", "R penalty-area ↗ (goal line)", 105.0, 13.84 },
        { "rpa_tl", "R penalty-area top (18-yd)",        88.5,  13.84 },
        { "rpa_bl", "R penalty-area bottom (18-yd)",     88.5,  54.16 },
        { "rpa_br", "R penalty-area ↘ (goal line)", 105.0, 54.16 },
        { "lga_tl", "L goal-area ↖ (goal line)", 0.0, 24.84 },
        { "lga_tr", "L goal-area top (6-yd)",         5.5, 24.84 },
        { "lga_br", "L goal-area bottom (6-yd)",      5.5, 43.16 },
        { "lga_bl", "L goal-area ↙ (goal line)", 0.0, 43.16 },
        { "rga_tr", "R goal-area ↗ (goal line)", 105.0, 24.84 },
        { "rga_tl", "R goal-area top (6-yd)",         99.5,  24.84 },
        { "rga_bl", "R goal-area bottom (6-yd)",      99.5,  43.16 },
        { "rga_br", "R goal-area ↘ (goal line)", 105.0, 43.16 },
    };
    return k;
}

// Human-readable name of the landmark at (px,py), or a "custom" fallback.
QString landmarkLabelFor(double px, double py)
{
    for (const Landmark &l : landmarkCatalog()) {
        if (std::abs(l.px - px) < 0.05 && std::abs(l.py - py) < 0.05)
            return QString::fromUtf8(l.label);
    }
    return QStringLiteral("custom (%1, %2)")
        .arg(px, 0, 'f', 1).arg(py, 0, 'f', 1);
}
}  // namespace

HomographyManager::HomographyManager(QObject *parent)
    : QObject(parent)
{
    applyDefaults();
}

void HomographyManager::applyDefaults()
{
    const double w = m_imageWidth;
    const double h = m_imageHeight;
    m_points = {
        { QStringLiteral("A"), QPointF(0.20 * w, 0.15 * h), QPointF(0.0,     0.0) },
        { QStringLiteral("B"), QPointF(0.80 * w, 0.12 * h), QPointF(kPitchW, 0.0) },
        { QStringLiteral("C"), QPointF(0.92 * w, 0.88 * h), QPointF(kPitchW, kPitchH) },
        { QStringLiteral("D"), QPointF(0.08 * w, 0.85 * h), QPointF(0.0,     kPitchH) },
    };
    m_touched = false;
    m_verified = false;
    m_reprojError = 0.0;
    m_H.release();
}

QVariantList HomographyManager::points() const
{
    QVariantList list;
    for (const Correspondence &c : m_points) {
        QVariantMap m;
        m[QStringLiteral("id")] = c.id;
        m[QStringLiteral("ix")] = c.image.x();
        m[QStringLiteral("iy")] = c.image.y();
        m[QStringLiteral("px")] = c.pitch.x();
        m[QStringLiteral("py")] = c.pitch.y();
        m[QStringLiteral("landmark")] = landmarkLabelFor(c.pitch.x(), c.pitch.y());
        list.append(m);
    }
    return list;
}

QVariantList HomographyManager::pitchLandmarks() const
{
    QVariantList list;
    for (const Landmark &l : landmarkCatalog()) {
        QVariantMap m;
        m[QStringLiteral("key")]   = QString::fromLatin1(l.key);
        m[QStringLiteral("label")] = QString::fromUtf8(l.label);
        m[QStringLiteral("px")]    = l.px;
        m[QStringLiteral("py")]    = l.py;
        list.append(m);
    }
    return list;
}

void HomographyManager::setPitchPoint(const QString &id, double px, double py)
{
    bool found = false;
    for (Correspondence &c : m_points) {
        if (c.id == id) {
            c.pitch = QPointF(px, py);
            found = true;
            break;
        }
    }
    if (!found)
        return;
    m_touched = true;
    // Pitch meaning is global (applies to every keyframe): re-solve H with
    // the new correspondence for the current frame.
    if (!m_keyframes.isEmpty()) {
        refreshForCurrentFrame();   // emits pointsChanged + stateChanged
    } else {
        emit pointsChanged();
        emit stateChanged();
    }
    emit edited();
}

void HomographyManager::setPitchLandmark(const QString &id, const QString &key)
{
    for (const Landmark &l : landmarkCatalog()) {
        if (key == QLatin1String(l.key)) {
            setPitchPoint(id, l.px, l.py);
            return;
        }
    }
}

QVariantList HomographyManager::keyframes() const
{
    QVariantList list;
    for (const Keyframe &k : m_keyframes) {
        QVariantMap m;
        m[QStringLiteral("frame")]    = k.frame;
        m[QStringLiteral("verified")] = k.verified;
        m[QStringLiteral("reproj")]   = k.reprojError;
        list.append(m);
    }
    return list;
}

bool HomographyManager::atKeyframe() const
{
    for (const Keyframe &k : m_keyframes)
        if (k.frame == m_currentFrame)
            return true;
    return false;
}

void HomographyManager::setOverlayEnabled(bool on)
{
    if (m_overlayEnabled == on)
        return;
    m_overlayEnabled = on;
    emit overlayChanged();
}

void HomographyManager::setImageSize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    m_imageWidth = width;
    m_imageHeight = height;
    // Only re-seed default points when nothing has been calibrated yet.
    if (!m_touched && m_keyframes.isEmpty()) {
        applyDefaults();
        emit pointsChanged();
        emit stateChanged();
    }
}

void HomographyManager::setCurrentFrame(int frame)
{
    if (m_currentFrame == frame)
        return;
    m_currentFrame = frame;
    if (!m_keyframes.isEmpty())
        refreshForCurrentFrame();   // emits pointsChanged + stateChanged
    else
        emit stateChanged();        // currentFrame / atKeyframe changed
}

void HomographyManager::setImagePoint(const QString &id, double x, double y)
{
    for (Correspondence &c : m_points) {
        if (c.id == id) {
            c.image = QPointF(x, y);
            m_touched = true;
            // Edits are uncommitted until Recompute stores a keyframe.
            m_verified = false;
            emit pointsChanged();
            emit stateChanged();
            emit edited();
            return;
        }
    }
}

void HomographyManager::reset()
{
    if (!m_keyframes.isEmpty()) {
        // Discard uncommitted edits: revert to the interpolated points.
        refreshForCurrentFrame();
    } else {
        applyDefaults();
        emit pointsChanged();
        emit stateChanged();
    }
    emit edited();
}

cv::Mat HomographyManager::solveH(const QPointF img[4], double *reprojErrPx) const
{
    std::vector<cv::Point2f> ip, pp;
    ip.reserve(4);
    pp.reserve(4);
    for (int i = 0; i < 4; ++i) {
        ip.emplace_back(static_cast<float>(img[i].x()), static_cast<float>(img[i].y()));
        pp.emplace_back(static_cast<float>(m_points[i].pitch.x()),
                        static_cast<float>(m_points[i].pitch.y()));
    }

    // 4-point DLT solved directly (no calib3d/cv::findHomography; with
    // exactly 4 correspondences the linear system is square).
    cv::Mat A = cv::Mat::zeros(8, 8, CV_64F);
    cv::Mat b(8, 1, CV_64F);
    for (int i = 0; i < 4; ++i) {
        const double x = ip[i].x, y = ip[i].y;
        const double u = pp[i].x, v = pp[i].y;
        double *r0 = A.ptr<double>(2 * i);
        r0[0] = x; r0[1] = y; r0[2] = 1.0;
        r0[6] = -u * x; r0[7] = -u * y;
        double *r1 = A.ptr<double>(2 * i + 1);
        r1[3] = x; r1[4] = y; r1[5] = 1.0;
        r1[6] = -v * x; r1[7] = -v * y;
        b.at<double>(2 * i)     = u;
        b.at<double>(2 * i + 1) = v;
    }
    cv::Mat h;
    if (!cv::solve(A, b, h, cv::DECOMP_LU) || cv::countNonZero(h != h) > 0)
        return cv::Mat();

    cv::Mat H = (cv::Mat_<double>(3, 3) <<
                     h.at<double>(0), h.at<double>(1), h.at<double>(2),
                     h.at<double>(3), h.at<double>(4), h.at<double>(5),
                     h.at<double>(6), h.at<double>(7), 1.0);

    if (reprojErrPx) {
        cv::Mat Hinv = H.inv();
        std::vector<cv::Point2f> back;
        cv::perspectiveTransform(pp, back, Hinv);
        double err = 0.0;
        for (size_t i = 0; i < ip.size(); ++i)
            err += std::hypot(back[i].x - ip[i].x, back[i].y - ip[i].y);
        *reprojErrPx = err / static_cast<double>(ip.size());
    }
    return H;
}

void HomographyManager::interpolatedImagePoints(int frame, QPointF out[4]) const
{
    if (m_keyframes.isEmpty()) {
        for (int i = 0; i < 4; ++i)
            out[i] = m_points[i].image;
        return;
    }
    const Keyframe &first = m_keyframes.first();
    const Keyframe &last  = m_keyframes.last();
    if (frame <= first.frame) {
        for (int i = 0; i < 4; ++i) out[i] = first.image[i];
        return;
    }
    if (frame >= last.frame) {
        for (int i = 0; i < 4; ++i) out[i] = last.image[i];
        return;
    }
    for (int k = 0; k + 1 < m_keyframes.size(); ++k) {
        const Keyframe &a = m_keyframes[k];
        const Keyframe &b = m_keyframes[k + 1];
        if (frame >= a.frame && frame <= b.frame) {
            const double t = b.frame > a.frame
                ? static_cast<double>(frame - a.frame) / (b.frame - a.frame) : 0.0;
            for (int j = 0; j < 4; ++j)
                out[j] = a.image[j] * (1.0 - t) + b.image[j] * t;
            return;
        }
    }
    for (int i = 0; i < 4; ++i) out[i] = last.image[i];
}

void HomographyManager::refreshForCurrentFrame()
{
    if (m_keyframes.isEmpty())
        return;
    QPointF img[4];
    interpolatedImagePoints(m_currentFrame, img);
    for (int i = 0; i < 4; ++i)
        m_points[i].image = img[i];
    double err = 0.0;
    m_H = solveH(img, &err);
    m_verified = !m_H.empty();
    m_reprojError = err;
    emit pointsChanged();
    emit stateChanged();
}

void HomographyManager::upsertKeyframe(int frame, const QPointF img[4],
                                       bool verified, double err)
{
    Keyframe kf;
    kf.frame = frame;
    kf.verified = verified;
    kf.reprojError = err;
    for (int j = 0; j < 4; ++j)
        kf.image[j] = img[j];

    for (int i = 0; i < m_keyframes.size(); ++i) {
        if (m_keyframes[i].frame == frame) {
            m_keyframes[i] = kf;
            return;
        }
    }
    int pos = 0;
    while (pos < m_keyframes.size() && m_keyframes[pos].frame < frame)
        ++pos;
    m_keyframes.insert(pos, kf);
}

void HomographyManager::recompute()
{
    QPointF img[4];
    for (int i = 0; i < 4; ++i)
        img[i] = m_points[i].image;

    double err = 0.0;
    cv::Mat H = solveH(img, &err);
    if (H.empty()) {
        m_H.release();
        m_verified = false;
        m_reprojError = 0.0;
        emit stateChanged();
        return;
    }
    m_H = H;
    m_reprojError = err;
    m_verified = true;
    upsertKeyframe(m_currentFrame, img, true, err);
    m_touched = true;
    emit stateChanged();
    emit keyframesChanged();
    emit edited();
}

void HomographyManager::removeKeyframe(int frame)
{
    for (int i = 0; i < m_keyframes.size(); ++i) {
        if (m_keyframes[i].frame == frame) {
            m_keyframes.removeAt(i);
            emit keyframesChanged();
            if (m_keyframes.isEmpty()) {
                m_verified = false;
                m_H.release();
                m_reprojError = 0.0;
                emit stateChanged();
            } else {
                refreshForCurrentFrame();
            }
            emit edited();
            return;
        }
    }
}

cv::Mat HomographyManager::homographyAt(int frame) const
{
    QPointF img[4];
    interpolatedImagePoints(frame, img);
    return solveH(img);
}

QPointF HomographyManager::imageToPitch(double x, double y) const
{
    if (m_H.empty())
        return QPointF(-1.0, -1.0);
    std::vector<cv::Point2f> in{ cv::Point2f(static_cast<float>(x), static_cast<float>(y)) };
    std::vector<cv::Point2f> out;
    cv::perspectiveTransform(in, out, m_H);
    return QPointF(out[0].x, out[0].y);
}

QPointF HomographyManager::imageToPitchAt(int frame, double x, double y) const
{
    const cv::Mat H = homographyAt(frame);
    if (H.empty())
        return QPointF(-1.0, -1.0);
    std::vector<cv::Point2f> in{ cv::Point2f(static_cast<float>(x), static_cast<float>(y)) };
    std::vector<cv::Point2f> out;
    cv::perspectiveTransform(in, out, H);
    return QPointF(out[0].x, out[0].y);
}

QJsonObject HomographyManager::toJson() const
{
    QJsonObject o;

    // Fixed pitch reference (corner meters), for completeness / portability.
    QJsonArray pitch;
    for (const Correspondence &c : m_points) {
        QJsonObject p;
        p[QStringLiteral("id")] = c.id;
        p[QStringLiteral("px")] = c.pitch.x();
        p[QStringLiteral("py")] = c.pitch.y();
        pitch.append(p);
    }
    o[QStringLiteral("pitch")] = pitch;

    QJsonArray kfs;
    for (const Keyframe &k : m_keyframes) {
        QJsonObject kobj;
        kobj[QStringLiteral("frame")]    = k.frame;
        kobj[QStringLiteral("verified")] = k.verified;
        kobj[QStringLiteral("reproj")]   = k.reprojError;
        QJsonArray img;
        for (int j = 0; j < 4; ++j) {
            QJsonObject ip;
            ip[QStringLiteral("ix")] = k.image[j].x();
            ip[QStringLiteral("iy")] = k.image[j].y();
            img.append(ip);
        }
        kobj[QStringLiteral("image")] = img;
        kfs.append(kobj);
    }
    o[QStringLiteral("keyframes")] = kfs;
    return o;
}

void HomographyManager::fromJson(const QJsonObject &o)
{
    // Optional fixed pitch reference.
    const QJsonArray pitch = o[QStringLiteral("pitch")].toArray();
    if (pitch.size() == 4) {
        for (int i = 0; i < 4; ++i) {
            const QJsonObject p = pitch[i].toObject();
            m_points[i].pitch = QPointF(p[QStringLiteral("px")].toDouble(),
                                        p[QStringLiteral("py")].toDouble());
        }
    }

    m_keyframes.clear();
    if (o.contains(QStringLiteral("keyframes"))) {
        // New multi-keyframe format.
        for (const QJsonValue &v : o[QStringLiteral("keyframes")].toArray()) {
            const QJsonObject kobj = v.toObject();
            Keyframe kf;
            kf.frame = kobj[QStringLiteral("frame")].toInt();
            kf.verified = kobj[QStringLiteral("verified")].toBool();
            kf.reprojError = kobj[QStringLiteral("reproj")].toDouble();
            const QJsonArray img = kobj[QStringLiteral("image")].toArray();
            for (int j = 0; j < 4 && j < img.size(); ++j) {
                const QJsonObject ip = img[j].toObject();
                kf.image[j] = QPointF(ip[QStringLiteral("ix")].toDouble(),
                                      ip[QStringLiteral("iy")].toDouble());
            }
            m_keyframes.append(kf);
        }
        std::sort(m_keyframes.begin(), m_keyframes.end(),
                  [](const Keyframe &a, const Keyframe &b) { return a.frame < b.frame; });
    } else {
        // Legacy single-homography format. Load the points into the working
        // buffer, and only promote them to a constant keyframe if they were
        // actually verified (unverified = never calibrated, so no valid H).
        const QJsonArray pts = o[QStringLiteral("points")].toArray();
        if (pts.size() == 4) {
            const bool wasVerified = o[QStringLiteral("verified")].toBool();
            Keyframe kf;
            kf.frame = 0;
            kf.verified = wasVerified;
            for (int i = 0; i < 4; ++i) {
                const QJsonObject p = pts[i].toObject();
                m_points[i].pitch = QPointF(p[QStringLiteral("px")].toDouble(),
                                            p[QStringLiteral("py")].toDouble());
                m_points[i].image = QPointF(p[QStringLiteral("ix")].toDouble(),
                                            p[QStringLiteral("iy")].toDouble());
                kf.image[i] = m_points[i].image;
            }
            if (wasVerified)
                m_keyframes.append(kf);
        }
    }

    m_touched = true;
    emit pointsChanged();
    emit keyframesChanged();
    refreshForCurrentFrame();
}
