#include "HomographyManager.h"
#include "HomographySolver.h"

#include <QJsonArray>
#include <QVariantMap>
#include <QFile>
#include <QDataStream>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPitchW = 105.0;
constexpr double kPitchH = 68.0;

// Reassigning a landmark commits to the governing keyframe when the current
// frame is within this many frames of it (covers imprecise seeks / stepper
// nudges). Farther away the edit belongs to a new keyframe and is committed by
// the next Recompute, so it must not overwrite a distant existing keyframe.
constexpr int kPitchEditWindowFrames = 8;

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
        { "circ_top",   "Centre circle top",     52.5,  24.85 },
        { "circ_bot",   "Centre circle bottom",  52.5,  43.15 },
        { "circ_left",  "Centre circle left",    43.35, 34.0 },
        { "circ_right", "Centre circle right",   61.65, 34.0 },
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
        // Penalty-area front line (18-yd) extended to the touchlines.
        { "lpa_front_top", "L box front × top touchline",    16.5, 0.0 },
        { "lpa_front_bot", "L box front × bottom touchline", 16.5, 68.0 },
        { "rpa_front_top", "R box front × top touchline",    88.5, 0.0 },
        { "rpa_front_bot", "R box front × bottom touchline", 88.5, 68.0 },
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

// The standard pitch drawing as a set of polylines in meters (105 x 68).
// Straight lines are two points; the centre circle and penalty arcs are
// sampled so they project as proper conics under the homography.
const std::vector<std::vector<cv::Point2d>> &pitchModelPolylines()
{
    static const std::vector<std::vector<cv::Point2d>> k = [] {
        std::vector<std::vector<cv::Point2d>> m;
        // Outer boundary + halfway line.
        m.push_back({ {0,0}, {105,0}, {105,68}, {0,68}, {0,0} });
        m.push_back({ {52.5,0}, {52.5,68} });
        // Penalty areas.
        m.push_back({ {0,13.84}, {16.5,13.84}, {16.5,54.16}, {0,54.16} });
        m.push_back({ {105,13.84}, {88.5,13.84}, {88.5,54.16}, {105,54.16} });
        // Goal areas.
        m.push_back({ {0,24.84}, {5.5,24.84}, {5.5,43.16}, {0,43.16} });
        m.push_back({ {105,24.84}, {99.5,24.84}, {99.5,43.16}, {105,43.16} });
        // Centre circle (r = 9.15 about 52.5,34).
        {
            std::vector<cv::Point2d> c;
            const int n = 96;
            for (int i = 0; i <= n; ++i) {
                const double a = 2.0 * M_PI * i / n;
                c.push_back({ 52.5 + 9.15 * std::cos(a), 34.0 + 9.15 * std::sin(a) });
            }
            m.push_back(std::move(c));
        }
        // Penalty arcs (the "D"): the part of the r=9.15 circle about each
        // penalty spot that lies outside the box (half-angle acos(5.5/9.15)).
        const double half = std::acos(5.5 / 9.15);
        auto arc = [&](double cx, double cy, double a0, double a1) {
            std::vector<cv::Point2d> c;
            const int n = 40;
            for (int i = 0; i <= n; ++i) {
                const double a = a0 + (a1 - a0) * i / n;
                c.push_back({ cx + 9.15 * std::cos(a), cy + 9.15 * std::sin(a) });
            }
            m.push_back(std::move(c));
        };
        arc(11.0, 34.0, -half, half);                 // left arc, opens toward +x
        arc(94.0, 34.0, M_PI - half, M_PI + half);    // right arc, opens toward -x
        return m;
    }();
    return k;
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
    int idx = -1;
    for (int i = 0; i < 4; ++i)
        if (m_points[i].id == id) { idx = i; break; }
    if (idx < 0)
        return;
    m_points[idx].pitch = QPointF(px, py);
    m_touched = true;

    // Re-solve H from the CURRENT image + pitch points so the overlay updates
    // live (image points are never overwritten by interpolation here).
    QPointF img[4], pit[4];
    for (int i = 0; i < 4; ++i) {
        img[i] = m_points[i].image;
        pit[i] = m_points[i].pitch;
    }
    double err = 0.0;
    m_H = solveH(img, pit, &err);
    m_reprojError = err;

    // Commit the choice to the keyframe that governs this frame — the *nearest*
    // one, i.e. the same keyframe whose assignment pitchPointsAt() shows here.
    // Matching by nearest-within-a-window (not exact frame) makes the
    // reassignment persist even when the current frame is a frame or two off a
    // keyframe (imprecise seek, stepper nudge), which previously dropped the
    // edit on the next navigation. Far from any keyframe the edit belongs to a
    // new keyframe and stays uncommitted until Recompute — committing it here
    // would corrupt a distant existing keyframe.
    const int ki = nearestKeyframeIndex(m_currentFrame);
    const bool committed = ki >= 0
        && std::abs(m_currentFrame - m_keyframes[ki].frame) <= kPitchEditWindowFrames;
    if (committed) {
        m_keyframes[ki].pitch[idx] = QPointF(px, py);
        if (m_keyframes[ki].frame == m_currentFrame)
            m_keyframes[ki].reprojError = err;
    }
    m_verified = committed && !m_H.empty();

    emit pointsChanged();
    emit stateChanged();
    emit edited();
    if (committed)
        emit keyframesChanged();
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

void HomographyManager::setModelOverlayEnabled(bool on)
{
    if (m_modelOverlayEnabled == on)
        return;
    m_modelOverlayEnabled = on;
    emit modelOverlayChanged();
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
    int idx = -1;
    for (int i = 0; i < 4; ++i)
        if (m_points[i].id == id) { idx = i; break; }
    if (idx < 0)
        return;
    m_points[idx].image = QPointF(x, y);
    m_touched = true;
    // Edits are uncommitted until Recompute stores a keyframe, but keep the
    // working H live so overlays (including the model reprojection) preview the
    // edit immediately.
    QPointF img[4], pit[4];
    for (int i = 0; i < 4; ++i) {
        img[i] = m_points[i].image;
        pit[i] = m_points[i].pitch;
    }
    double err = 0.0;
    m_H = solveH(img, pit, &err);
    m_reprojError = err;
    m_verified = false;
    emit pointsChanged();
    emit stateChanged();
    emit edited();
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

cv::Mat HomographyManager::solveH(const QPointF img[4], const QPointF pit[4],
                                  double *reprojErrPx) const
{
    std::vector<cv::Point2f> ip, pp;
    ip.reserve(4);
    pp.reserve(4);
    for (int i = 0; i < 4; ++i) {
        ip.emplace_back(static_cast<float>(img[i].x()), static_cast<float>(img[i].y()));
        pp.emplace_back(static_cast<float>(pit[i].x()),
                        static_cast<float>(pit[i].y()));
    }

    // Exact 4-point homography (image -> pitch) via the configurable solver
    // module (OpenCV cv::findHomography or the custom DLT).
    cv::Mat H = homog::solveDLT(ip, pp);
    if (H.empty() || cv::countNonZero(H != H) > 0)
        return cv::Mat();

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

void HomographyManager::imagePointsAt(int frame, QPointF out[4]) const
{
    if (!m_dense.isEmpty() && frame >= m_denseStart
        && frame < m_denseStart + m_dense.size()) {
        const std::array<QPointF, 4> &a = m_dense[frame - m_denseStart];
        for (int k = 0; k < 4; ++k)
            out[k] = a[k];
        return;
    }
    interpolatedImagePoints(frame, out);
}

int HomographyManager::nearestKeyframeIndex(int frame) const
{
    if (m_keyframes.isEmpty())
        return -1;
    int best = 0;
    int bestDist = std::abs(frame - m_keyframes[0].frame);
    for (int i = 1; i < m_keyframes.size(); ++i) {
        const int d = std::abs(frame - m_keyframes[i].frame);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void HomographyManager::pitchPointsAt(int frame, QPointF out[4]) const
{
    // Nearest keyframe wins (pitch meaning is categorical — interpolating it
    // between two differently-assigned keyframes is meaningless).
    const int ki = nearestKeyframeIndex(frame);
    if (ki < 0) {
        for (int i = 0; i < 4; ++i)
            out[i] = m_points[i].pitch;
        return;
    }
    for (int i = 0; i < 4; ++i)
        out[i] = m_keyframes[ki].pitch[i];
}

void HomographyManager::refreshForCurrentFrame()
{
    if (m_keyframes.isEmpty())
        return;
    QPointF img[4], pit[4];
    imagePointsAt(m_currentFrame, img);
    pitchPointsAt(m_currentFrame, pit);
    for (int i = 0; i < 4; ++i) {
        m_points[i].image = img[i];
        m_points[i].pitch = pit[i];   // reflect this frame's landmark choice
    }
    double err = 0.0;
    m_H = solveH(img, pit, &err);
    m_verified = !m_H.empty();
    m_reprojError = err;
    emit pointsChanged();
    emit stateChanged();
}

void HomographyManager::upsertKeyframe(int frame, const QPointF img[4],
                                       const QPointF pit[4], bool verified, double err)
{
    Keyframe kf;
    kf.frame = frame;
    kf.verified = verified;
    kf.reprojError = err;
    for (int j = 0; j < 4; ++j) {
        kf.image[j] = img[j];
        kf.pitch[j] = pit[j];
    }

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
    QPointF img[4], pit[4];
    for (int i = 0; i < 4; ++i) {
        img[i] = m_points[i].image;
        pit[i] = m_points[i].pitch;
    }

    double err = 0.0;
    cv::Mat H = solveH(img, pit, &err);
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
    upsertKeyframe(m_currentFrame, img, pit, true, err);
    m_touched = true;
    // The dense propagation is now stale relative to the edited keyframes.
    if (!m_dense.isEmpty()) {
        m_dense.clear();
        m_denseConf.clear();
        m_denseStart = 0;
        emit propagationChanged();
    }
    emit stateChanged();
    emit keyframesChanged();
    emit edited();
}

void HomographyManager::removeKeyframe(int frame)
{
    for (int i = 0; i < m_keyframes.size(); ++i) {
        if (m_keyframes[i].frame == frame) {
            m_keyframes.removeAt(i);
            if (!m_dense.isEmpty()) {
                m_dense.clear();
                m_denseConf.clear();
                m_denseStart = 0;
                emit propagationChanged();
            }
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
    QPointF img[4], pit[4];
    imagePointsAt(frame, img);
    pitchPointsAt(frame, pit);
    return solveH(img, pit);
}

void HomographyManager::applyRefinedHomography(int frame, const cv::Mat &H, double errPx)
{
    if (H.empty())
        return;
    // Recover the 4 reference image points: pitch -> image via H^-1.
    const cv::Mat Hinv = H.inv();
    std::vector<cv::Point2f> pitVec, imgPts;
    pitVec.reserve(4);
    for (int i = 0; i < 4; ++i)
        pitVec.emplace_back(static_cast<float>(m_points[i].pitch.x()),
                            static_cast<float>(m_points[i].pitch.y()));
    cv::perspectiveTransform(pitVec, imgPts, Hinv);

    QPointF img[4], pit[4];
    for (int i = 0; i < 4; ++i) {
        img[i] = QPointF(imgPts[i].x, imgPts[i].y);
        pit[i] = m_points[i].pitch;
    }

    upsertKeyframe(frame, img, pit, true, errPx);
    m_touched = true;
    if (!m_dense.isEmpty()) {   // dense propagation is now stale
        m_dense.clear();
        m_denseConf.clear();
        m_denseStart = 0;
        emit propagationChanged();
    }
    if (frame == m_currentFrame) {
        refreshForCurrentFrame();   // emits pointsChanged + stateChanged
    } else {
        emit stateChanged();
    }
    emit keyframesChanged();
    emit edited();
}

bool HomographyManager::atPropagated() const
{
    return !m_dense.isEmpty() && m_currentFrame >= m_denseStart
        && m_currentFrame < m_denseStart + m_dense.size();
}

double HomographyManager::confidenceAt(int frame) const
{
    const int idx = frame - m_denseStart;
    if (m_denseConf.isEmpty() || idx < 0 || idx >= m_denseConf.size())
        return 1.0;   // no dense confidence -> treat as valid
    return m_denseConf[idx];
}

void HomographyManager::setPropagating(bool on, const QString &label)
{
    m_propagating = on;
    if (!label.isNull())
        m_propLabel = label;
    if (on)
        m_propProgress = 0.0;
    emit propagationChanged();
}

void HomographyManager::setPropProgress(double frac, const QString &label)
{
    m_propProgress = frac;
    if (!label.isNull())
        m_propLabel = label;
    emit propagationChanged();
}

void HomographyManager::applyDenseTrack(int start,
                                        const QVector<std::array<QPointF, 4>> &dense)
{
    m_denseStart = start;
    m_dense = dense;
    m_denseConf.clear();   // legacy caller path: no per-frame confidence
    emit propagationChanged();
    if (!m_keyframes.isEmpty())
        refreshForCurrentFrame();
    else
        emit stateChanged();
}

bool HomographyManager::loadDenseTrack(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    // v2 files begin with the sentinel -2 and carry a per-frame confidence;
    // v1 files begin directly with the start frame (>= 0).
    qint32 first = 0;
    ds >> first;
    const bool v2 = (first == -2);
    qint32 start = first, count = 0;
    if (v2)
        ds >> start;
    ds >> count;
    if (count <= 0 || count > 10000000)
        return false;
    QVector<std::array<QPointF, 4>> dense;
    QVector<double> confs;
    dense.reserve(count);
    if (v2)
        confs.reserve(count);
    for (int i = 0; i < count; ++i) {
        std::array<QPointF, 4> a;
        for (int k = 0; k < 4; ++k) {
            double x = 0.0, y = 0.0;
            ds >> x >> y;
            a[k] = QPointF(x, y);
        }
        if (v2) {
            double c = 1.0;
            ds >> c;
            confs.append(c);
        }
        if (ds.status() != QDataStream::Ok)
            return false;
        dense.append(a);
    }
    applyDenseTrack(start, dense);
    m_denseConf = confs;   // set after applyDenseTrack (which clears it)
    emit stateChanged();
    return true;
}

QVariantList HomographyManager::graphicsVariant() const
{
    QVariantList list;
    for (const QRectF &g : m_graphics) {
        QVariantMap m;
        m[QStringLiteral("x")] = g.x();
        m[QStringLiteral("y")] = g.y();
        m[QStringLiteral("w")] = g.width();
        m[QStringLiteral("h")] = g.height();
        list.append(m);
    }
    return list;
}

void HomographyManager::addGraphicsRegion(double x, double y, double w, double h)
{
    // Normalize to a top-left rect clamped to [0,1]; ignore negligible drags.
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    x = std::max(0.0, std::min(1.0, x));
    y = std::max(0.0, std::min(1.0, y));
    w = std::min(w, 1.0 - x);
    h = std::min(h, 1.0 - y);
    if (w < 0.01 || h < 0.01)
        return;
    m_graphics.append(QRectF(x, y, w, h));
    emit graphicsChanged();
    emit edited();
}

void HomographyManager::setStaticVoteFrac(double f)
{
    f = std::max(0.05, std::min(1.0, f));
    if (qFuzzyCompare(f, m_staticVoteFrac))
        return;
    m_staticVoteFrac = f;
    emit staticVoteFracChanged();
    emit edited();
}

void HomographyManager::removeGraphicsRegion(int index)
{
    if (index < 0 || index >= m_graphics.size())
        return;
    m_graphics.removeAt(index);
    emit graphicsChanged();
    emit edited();
}

void HomographyManager::clearGraphics()
{
    if (m_graphics.isEmpty())
        return;
    m_graphics.clear();
    emit graphicsChanged();
    emit edited();
}

void HomographyManager::clearPropagation()
{
    if (m_dense.isEmpty() && !m_propagating && m_propProgress == 0.0)
        return;
    m_dense.clear();
    m_denseConf.clear();
    m_denseStart = 0;
    m_propagating = false;
    m_propProgress = 0.0;
    emit propagationChanged();
    if (!m_keyframes.isEmpty())
        refreshForCurrentFrame();
    else
        emit stateChanged();
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

namespace {
// Project a pitch point (meters) through Hinv (pitch->image). A homography is
// only defined up to scale, so the sign of the projective depth w is arbitrary;
// `frontSign` (derived from a point known to be on-camera) selects the visible
// side of the horizon. Returns false for points on/behind it — they must not be
// drawn or connected across.
inline bool projectPitch(const cv::Mat &Hinv, double px, double py,
                         double frontSign, QPointF *out)
{
    const double *h = Hinv.ptr<double>();
    const double u = h[0] * px + h[1] * py + h[2];
    const double v = h[3] * px + h[4] * py + h[5];
    const double w = h[6] * px + h[7] * py + h[8];
    if (frontSign * w <= 1e-9)
        return false;
    *out = QPointF(u / w, v / w);
    return true;
}
}  // namespace

// Sign of the projective depth for the on-camera side, taken from the four
// calibration correspondences (which are, by construction, in front).
double HomographyManager::frontSign(const cv::Mat &Hinv) const
{
    const double *h = Hinv.ptr<double>();
    double wsum = 0.0;
    for (const Correspondence &c : m_points)
        wsum += h[6] * c.pitch.x() + h[7] * c.pitch.y() + h[8];
    return wsum >= 0.0 ? 1.0 : -1.0;
}

QPointF HomographyManager::pitchToImage(double px, double py) const
{
    if (m_H.empty())
        return QPointF(-1.0, -1.0);
    const cv::Mat Hinv = m_H.inv();
    QPointF p;
    return projectPitch(Hinv, px, py, frontSign(Hinv), &p) ? p : QPointF(-1.0, -1.0);
}

QPointF HomographyManager::pitchToImageAt(int frame, double px, double py) const
{
    const cv::Mat H = homographyAt(frame);
    if (H.empty())
        return QPointF(-1.0, -1.0);
    const cv::Mat Hinv = H.inv();
    QPointF p;
    return projectPitch(Hinv, px, py, frontSign(Hinv), &p) ? p : QPointF(-1.0, -1.0);
}

QVariantMap HomographyManager::projectedPitchModel(int frame) const
{
    QVariantMap result;
    // On the frame that is showing, use the live working H so edits to the
    // point correspondences or their pitch landmarks preview immediately
    // (before Recompute commits them to a keyframe). Other frames use the
    // interpolated/propagated keyframe homography.
    const cv::Mat H = (frame == m_currentFrame && !m_H.empty())
                          ? m_H : homographyAt(frame);
    if (H.empty()) {
        result[QStringLiteral("valid")] = false;
        return result;
    }
    const cv::Mat Hinv = H.inv();
    const double s = frontSign(Hinv);

    // Project each model polyline, breaking it into runs of consecutive
    // on-camera vertices so a line never jumps across the horizon.
    QVariantList lines;
    for (const std::vector<cv::Point2d> &poly : pitchModelPolylines()) {
        QVariantList run;
        for (const cv::Point2d &p : poly) {
            QPointF img;
            if (projectPitch(Hinv, p.x, p.y, s, &img)) {
                run.append(img);
            } else if (!run.isEmpty()) {
                lines.append(QVariant(run));   // nest as one polyline (not splice)
                run.clear();
            }
        }
        if (!run.isEmpty())
            lines.append(QVariant(run));
    }
    result[QStringLiteral("lines")] = lines;

    // Reference landmark dots.
    QVariantList points;
    for (const Landmark &l : landmarkCatalog()) {
        QPointF img;
        if (projectPitch(Hinv, l.px, l.py, s, &img))
            points.append(img);
    }
    result[QStringLiteral("points")] = points;
    result[QStringLiteral("valid")] = true;
    return result;
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
        QJsonArray pit;
        for (int j = 0; j < 4; ++j) {
            QJsonObject ip;
            ip[QStringLiteral("ix")] = k.image[j].x();
            ip[QStringLiteral("iy")] = k.image[j].y();
            img.append(ip);
            QJsonObject pp;
            pp[QStringLiteral("px")] = k.pitch[j].x();
            pp[QStringLiteral("py")] = k.pitch[j].y();
            pit.append(pp);
        }
        kobj[QStringLiteral("image")] = img;
        kobj[QStringLiteral("pitch")] = pit;
        kfs.append(kobj);
    }
    o[QStringLiteral("keyframes")] = kfs;

    QJsonArray gfx;
    for (const QRectF &g : m_graphics) {
        QJsonObject gobj;
        gobj[QStringLiteral("x")] = g.x();
        gobj[QStringLiteral("y")] = g.y();
        gobj[QStringLiteral("w")] = g.width();
        gobj[QStringLiteral("h")] = g.height();
        gfx.append(gobj);
    }
    o[QStringLiteral("graphics")] = gfx;
    o[QStringLiteral("static_vote_frac")] = m_staticVoteFrac;
    return o;
}

void HomographyManager::fromJson(const QJsonObject &o)
{
    m_graphics.clear();
    for (const QJsonValue &v : o[QStringLiteral("graphics")].toArray()) {
        const QJsonObject g = v.toObject();
        m_graphics.append(QRectF(g[QStringLiteral("x")].toDouble(), g[QStringLiteral("y")].toDouble(),
                                 g[QStringLiteral("w")].toDouble(), g[QStringLiteral("h")].toDouble()));
    }
    emit graphicsChanged();

    m_staticVoteFrac = o.contains(QStringLiteral("static_vote_frac"))
        ? std::max(0.05, std::min(1.0, o[QStringLiteral("static_vote_frac")].toDouble()))
        : 0.15;
    emit staticVoteFracChanged();

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
            // Per-keyframe pitch assignment. Older projects have none, so fall
            // back to the global reference loaded into m_points above.
            const QJsonArray kpit = kobj[QStringLiteral("pitch")].toArray();
            for (int j = 0; j < 4; ++j) {
                if (j < kpit.size()) {
                    const QJsonObject pp = kpit[j].toObject();
                    kf.pitch[j] = QPointF(pp[QStringLiteral("px")].toDouble(),
                                          pp[QStringLiteral("py")].toDouble());
                } else {
                    kf.pitch[j] = m_points[j].pitch;
                }
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
                kf.pitch[i] = m_points[i].pitch;
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
