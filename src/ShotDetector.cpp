#include "ShotDetector.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <opencv2/opencv.hpp>

#include <algorithm>

namespace {

constexpr double kCutCorr = 0.55;     // hist-correlation below this = cut
constexpr double kGrassPitch = 0.34;  // median grass fraction to be a Pitch shot
constexpr int    kMinShotLen = 6;     // shorter runs get merged into neighbours

// Fraction of grass (green) pixels in a small HSV frame.
double grassFraction(const cv::Mat &hsv)
{
    cv::Mat mask;
    cv::inRange(hsv, cv::Scalar(28, 25, 25), cv::Scalar(95, 255, 255), mask);
    return static_cast<double>(cv::countNonZero(mask)) / (mask.rows * mask.cols);
}

// Normalized Hue-Saturation histogram for cut detection.
cv::Mat hsHist(const cv::Mat &hsv)
{
    const int hbins = 30, sbins = 32;
    const int histSize[] = {hbins, sbins};
    const float hr[] = {0, 180}, sr[] = {0, 256};
    const float *ranges[] = {hr, sr};
    const int channels[] = {0, 1};
    cv::Mat hist;
    cv::calcHist(&hsv, 1, channels, cv::Mat(), hist, 2, histSize, ranges, true, false);
    cv::normalize(hist, hist, 1.0, 0.0, cv::NORM_L1);
    return hist;
}

} // namespace

ShotDetector::ShotDetector(QObject *parent) : QThread(parent) {}
ShotDetector::~ShotDetector() { stopAndWait(); }

void ShotDetector::configure(const QString &videoPath, int startFrame, int endFrame,
                             const QString &outPath)
{
    m_videoPath = videoPath;
    m_startFrame = startFrame;
    m_endFrame = endFrame;
    m_outPath = outPath;
    m_stop.store(false);
}

void ShotDetector::stopAndWait()
{
    requestStop();
    if (isRunning()) wait(5000);
}

QVector<ShotDetector::Shot> ShotDetector::detectSync(
    const QString &videoPath, int startFrame, int endFrame,
    const std::function<void(double, const QString &)> &progress,
    const std::atomic<bool> *stop)
{
    QVector<Shot> shots;
    cv::VideoCapture cap;
    if (!cap.open(videoPath.toStdString()))
        return shots;

    const int total = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    if (endFrame <= 0 || endFrame > total) endFrame = total;
    if (startFrame < 0) startFrame = 0;
    if (endFrame <= startFrame) return shots;
    const double W = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    const double scale = (W > 320.0 ? 320.0 / W : 1.0);

    cap.set(cv::CAP_PROP_POS_FRAMES, startFrame);

    // Boundaries between shots (cut before this frame) + per-frame grass.
    QVector<double> grass;
    QVector<int> cutAt;                // frames that open a new shot
    QVector<double> cutStrength;
    cutAt.append(startFrame);
    cutStrength.append(1.0);

    cv::Mat frame, small, hsv, prevHist;
    const int span = endFrame - startFrame;
    for (int f = startFrame; f < endFrame; ++f) {
        if (stop && stop->load()) return shots;
        if (!cap.read(frame) || frame.empty()) { endFrame = f; break; }
        if (scale < 1.0) cv::resize(frame, small, cv::Size(), scale, scale, cv::INTER_AREA);
        else small = frame;
        cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);

        grass.append(grassFraction(hsv));
        const cv::Mat hist = hsHist(hsv);
        if (!prevHist.empty()) {
            const double corr = cv::compareHist(prevHist, hist, cv::HISTCMP_CORREL);
            if (corr < kCutCorr) { cutAt.append(f); cutStrength.append(1.0 - corr); }
        }
        prevHist = hist;

        if (progress && ((f - startFrame) % 25 == 0))
            progress(double(f - startFrame) / std::max(1, span),
                     QStringLiteral("Detectando cortes  frame %1 / %2").arg(f + 1).arg(endFrame));
    }
    if (grass.isEmpty()) return shots;
    const int realEnd = startFrame + grass.size();  // exclusive

    // Build shots from the boundaries, computing per-shot median grass.
    auto medianGrass = [&](int s, int e) {   // [s,e) absolute frames
        QVector<double> v;
        for (int f = s; f < e; ++f) v.append(grass[f - startFrame]);
        std::sort(v.begin(), v.end());
        return v.isEmpty() ? 0.0 : v[v.size() / 2];
    };

    for (int i = 0; i < cutAt.size(); ++i) {
        const int s = cutAt[i];
        const int e = (i + 1 < cutAt.size()) ? cutAt[i + 1] : realEnd;
        if (e <= s) continue;
        Shot sh;
        sh.startFrame = s;
        sh.endFrame = e - 1;
        sh.grassMean = medianGrass(s, e);
        sh.pitch = sh.grassMean >= kGrassPitch;
        sh.cutStrength = cutStrength[i];
        shots.append(sh);
    }

    // Merge sub-threshold-length shots into the previous one (flash/graphics
    // blips) so the segmentation is not fragmented.
    QVector<Shot> merged;
    for (const Shot &sh : shots) {
        if (!merged.isEmpty() && (sh.endFrame - sh.startFrame + 1) < kMinShotLen) {
            Shot &prev = merged.last();
            prev.endFrame = sh.endFrame;   // absorb; keep prev's classification
        } else {
            merged.append(sh);
        }
    }
    if (progress) progress(1.0, QStringLiteral("Listo"));
    return merged;
}

void ShotDetector::run()
{
    const QVector<Shot> shots = detectSync(
        m_videoPath, m_startFrame, m_endFrame,
        [this](double f, const QString &l) { emit progressChanged(f, l); },
        &m_stop);
    if (m_stop.load()) { emit finished(false, QStringLiteral("Cancelado"), 0); return; }
    if (shots.isEmpty()) { emit finished(false, QStringLiteral("Sin resultados"), 0); return; }
    if (!save(m_outPath, shots)) {
        emit finished(false, QStringLiteral("No se pudo guardar %1").arg(m_outPath), 0);
        return;
    }
    emit finished(true, QString(), shots.size());
}

QJsonObject ShotDetector::toJson(const QVector<Shot> &shots)
{
    QJsonArray arr;
    for (const Shot &s : shots) {
        QJsonObject o;
        o[QStringLiteral("start")] = s.startFrame;
        o[QStringLiteral("end")] = s.endFrame;
        o[QStringLiteral("pitch")] = s.pitch;
        o[QStringLiteral("grass")] = s.grassMean;
        o[QStringLiteral("cut")] = s.cutStrength;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("shots")] = arr;
    return root;
}

bool ShotDetector::save(const QString &path, const QVector<Shot> &shots)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(toJson(shots)).toJson(QJsonDocument::Indented));
    return true;
}

QVector<ShotDetector::Shot> ShotDetector::load(const QString &path)
{
    QVector<Shot> shots;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return shots;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll())
                               .object()[QStringLiteral("shots")].toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Shot s;
        s.startFrame = o[QStringLiteral("start")].toInt();
        s.endFrame = o[QStringLiteral("end")].toInt();
        s.pitch = o[QStringLiteral("pitch")].toBool();
        s.grassMean = o[QStringLiteral("grass")].toDouble();
        s.cutStrength = o[QStringLiteral("cut")].toDouble();
        shots.append(s);
    }
    return shots;
}
