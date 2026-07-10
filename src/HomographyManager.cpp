#include "HomographyManager.h"

#include <QJsonArray>
#include <QVariantMap>
#include <cmath>

namespace {
constexpr double kPitchW = 105.0;
constexpr double kPitchH = 68.0;
}

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
        list.append(m);
    }
    return list;
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
    if (!m_touched) {
        applyDefaults();
        emit pointsChanged();
        emit stateChanged();
    }
}

void HomographyManager::setImagePoint(const QString &id, double x, double y)
{
    for (Correspondence &c : m_points) {
        if (c.id == id) {
            c.image = QPointF(x, y);
            m_touched = true;
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
    applyDefaults();
    emit pointsChanged();
    emit stateChanged();
    emit edited();
}

void HomographyManager::recompute()
{
    std::vector<cv::Point2f> img, pitch;
    for (const Correspondence &c : m_points) {
        img.emplace_back(static_cast<float>(c.image.x()), static_cast<float>(c.image.y()));
        pitch.emplace_back(static_cast<float>(c.pitch.x()), static_cast<float>(c.pitch.y()));
    }

    // 4-point DLT solved directly (this OpenCV build ships without calib3d,
    // so cv::findHomography is unavailable; with exactly 4 correspondences
    // the linear system is square anyway).
    cv::Mat A = cv::Mat::zeros(8, 8, CV_64F);
    cv::Mat b(8, 1, CV_64F);
    for (int i = 0; i < 4; ++i) {
        const double x = img[i].x, y = img[i].y;
        const double u = pitch[i].x, v = pitch[i].y;
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
    if (!cv::solve(A, b, h, cv::DECOMP_LU) || cv::countNonZero(h != h) > 0) {
        m_H.release();
        m_verified = false;
        m_reprojError = 0.0;
        emit stateChanged();
        return;
    }
    m_H = (cv::Mat_<double>(3, 3) <<
               h.at<double>(0), h.at<double>(1), h.at<double>(2),
               h.at<double>(3), h.at<double>(4), h.at<double>(5),
               h.at<double>(6), h.at<double>(7), 1.0);

    // Reprojection error in image pixels: pitch -> image with H^-1.
    cv::Mat Hinv = m_H.inv();
    std::vector<cv::Point2f> back;
    cv::perspectiveTransform(pitch, back, Hinv);
    double err = 0.0;
    for (size_t i = 0; i < img.size(); ++i) {
        err += std::hypot(back[i].x - img[i].x, back[i].y - img[i].y);
    }
    m_reprojError = err / static_cast<double>(img.size());
    m_verified = true;
    emit stateChanged();
    emit edited();
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

QJsonObject HomographyManager::toJson() const
{
    QJsonObject o;
    QJsonArray pts;
    for (const Correspondence &c : m_points) {
        QJsonObject p;
        p[QStringLiteral("id")] = c.id;
        p[QStringLiteral("ix")] = c.image.x();
        p[QStringLiteral("iy")] = c.image.y();
        p[QStringLiteral("px")] = c.pitch.x();
        p[QStringLiteral("py")] = c.pitch.y();
        pts.append(p);
    }
    o[QStringLiteral("points")]   = pts;
    o[QStringLiteral("verified")] = m_verified;
    return o;
}

void HomographyManager::fromJson(const QJsonObject &o)
{
    const QJsonArray pts = o[QStringLiteral("points")].toArray();
    if (pts.size() == 4) {
        for (int i = 0; i < 4; ++i) {
            const QJsonObject p = pts[i].toObject();
            m_points[i].image = QPointF(p[QStringLiteral("ix")].toDouble(),
                                        p[QStringLiteral("iy")].toDouble());
        }
        m_touched = true;
        emit pointsChanged();
        if (o[QStringLiteral("verified")].toBool()) {
            recompute();
        }
    }
}
