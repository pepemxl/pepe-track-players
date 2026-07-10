#include "TrackingManager.h"
#include "YoloDetector.h"

#include <opencv2/opencv.hpp>

#include <QElapsedTimer>
#include <QMutexLocker>
#include <algorithm>
#include <vector>

namespace {

constexpr int    kStride       = 5;     // run the detector every N frames
constexpr double kLowConf      = 0.5;   // below this a detection is "low confidence"
constexpr double kIouMatch     = 0.2;

// Chip states, mirrored by the QML legend.
enum ChipState { NotProcessed = 0, Good = 1, LowConf = 2, Lost = 3 };

double iou(const cv::Rect &a, const cv::Rect &b)
{
    const int inter = (a & b).area();
    const int uni   = a.area() + b.area() - inter;
    return uni > 0 ? static_cast<double>(inter) / uni : 0.0;
}

struct Track
{
    int      id{0};
    cv::Rect box;
    int      lastFrame{0};
    int      framesTracked{0};
    double   sumConf{0.0};
    int      detections{0};
    int      gaps{0};       // times the track was lost and re-acquired
    bool     missing{false};
};

} // namespace

TrackingManager::TrackingManager(QObject *parent)
    : QThread(parent)
{
    for (int i = 0; i < kChipCount; ++i)
        m_frameChips.append(NotProcessed);

    // Worker -> GUI thread: queued because the emitting thread differs.
    connect(this, &TrackingManager::snapshotReady,
            this, &TrackingManager::applySnapshot, Qt::QueuedConnection);
    connect(this, &TrackingManager::runFinished,
            this, &TrackingManager::handleRunFinished, Qt::QueuedConnection);
}

TrackingManager::~TrackingManager()
{
    stopInference();
}

void TrackingManager::setSource(const QString &path)
{
    stopInference();
    m_sourcePath = path;
    m_completed = false;
    m_progress = 0.0;
    m_framesProcessed = 0;
    m_playersTracked = 0;
    m_avgConfidence = 0.0;
    m_lostFrames = 0;
    m_frameChips.clear();
    for (int i = 0; i < kChipCount; ++i)
        m_frameChips.append(NotProcessed);
    m_tracks.clear();
    emit runningChanged();
    emit snapshotChanged();
    emit tracksUpdated(m_tracks);
}

void TrackingManager::setExclusions(const QVector<QPair<int, int>> &frameRanges)
{
    QMutexLocker lock(&m_exclMutex);
    m_exclRanges = frameRanges;
}

void TrackingManager::toggleRun()
{
    if (m_runningInference) {
        stopInference();
        return;
    }
    if (m_sourcePath.isEmpty() || isRunning())
        return;
    m_stopRequested.store(false);
    m_runningInference = true;
    m_completed = false;
    emit runningChanged();
    start();
}

void TrackingManager::stopInference()
{
    m_stopRequested.store(true);
    if (isRunning()) {
        wait();
    }
}

void TrackingManager::applySnapshot(double progress, const QVariantMap &stats,
                                    const QVariantList &chips, const QVariantList &tracks)
{
    m_progress        = progress;
    m_framesProcessed = stats.value(QStringLiteral("framesProcessed")).toInt();
    m_playersTracked  = stats.value(QStringLiteral("playersTracked")).toInt();
    m_avgConfidence   = stats.value(QStringLiteral("avgConfidence")).toDouble();
    m_lostFrames      = stats.value(QStringLiteral("lostFrames")).toInt();
    m_frameChips      = chips;
    m_tracks          = tracks;
    emit snapshotChanged();
    emit tracksUpdated(m_tracks);
}

void TrackingManager::handleRunFinished(bool completedAll)
{
    m_runningInference = false;
    m_completed = completedAll;
    emit runningChanged();
}

void TrackingManager::run()
{
    YoloDetector detector;
    QString loadError;
    if (!detector.load(&loadError)) {
        emit error(QStringLiteral("Tracking: %1").arg(loadError));
        emit runFinished(false);
        return;
    }

    cv::VideoCapture cap;
    if (!cap.open(m_sourcePath.toStdString()) || !cap.isOpened()) {
        emit error(QStringLiteral("Tracking: failed to open video"));
        emit runFinished(false);
        return;
    }

    const int totalFrames = std::max(1, static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)));
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0.0 || fps > 240.0) fps = 30.0;
    const int maxGapFrames = static_cast<int>(fps * 1.5);

    // Snapshot the excluded frame ranges (pre/post match, commercials).
    QVector<QPair<int, int>> excluded;
    {
        QMutexLocker lock(&m_exclMutex);
        excluded = m_exclRanges;
    }
    auto isExcluded = [&excluded](int frame) {
        for (const auto &r : excluded) {
            if (frame >= r.first && frame <= r.second)
                return true;
        }
        return false;
    };

    std::vector<Track> tracks;
    int nextTrackId = 1;
    QVariantList chips;
    for (int i = 0; i < kChipCount; ++i)
        chips.append(NotProcessed);

    int    lostFrames = 0;
    double sumConfAll = 0.0;
    int    nConfAll = 0;

    auto buildTracks = [&]() {
        QVariantList list;
        for (const Track &t : tracks) {
            QVariantMap row;
            row[QStringLiteral("trackId")] = QStringLiteral("T%1").arg(t.id, 2, 10, QLatin1Char('0'));
            row[QStringLiteral("name")] = QString();  // assignment UI can come later
            row[QStringLiteral("framesTracked")] = t.framesTracked;
            const double avg = t.detections > 0 ? t.sumConf / t.detections : 0.0;
            row[QStringLiteral("avgConf")] = QString::number(std::min(avg, 0.99), 'f', 2);
            QString status = QStringLiteral("stable");
            int kind = 0;
            if (t.gaps > 2) {
                status = QStringLiteral("lost %1x").arg(t.gaps);
                kind = 2;
            } else if (t.gaps > 0 || t.missing) {
                status = QStringLiteral("flickering");
                kind = 1;
            }
            row[QStringLiteral("status")] = status;
            row[QStringLiteral("statusKind")] = kind;
            list.append(row);
        }
        return list;
    };

    auto emitSnapshot = [&](int frameIdx) {
        QVariantMap stats;
        stats[QStringLiteral("framesProcessed")] = frameIdx;
        int active = 0;
        for (const Track &t : tracks)
            if (!t.missing) ++active;
        stats[QStringLiteral("playersTracked")] = active;
        stats[QStringLiteral("avgConfidence")] = nConfAll > 0
            ? std::min(sumConfAll / nConfAll, 0.99) : 0.0;
        stats[QStringLiteral("lostFrames")] = lostFrames;
        emit snapshotReady(static_cast<double>(frameIdx) / totalFrames,
                           stats, chips, buildTracks());
    };

    QElapsedTimer lastEmit;
    lastEmit.start();

    int frameIdx = 0;
    bool completedAll = true;
    cv::Mat frame;

    while (frameIdx < totalFrames) {
        if (m_stopRequested.load()) {
            completedAll = false;
            break;
        }
        if (!cap.grab()) {
            break;
        }

        if (frameIdx % kStride == 0 && !isExcluded(frameIdx)) {
            if (!cap.retrieve(frame) || frame.empty()) {
                ++frameIdx;
                continue;
            }

            const auto people = detector.detect(frame);

            std::vector<cv::Rect> dets;
            std::vector<double> confs;
            double maxConf = 0.0;
            for (const auto &det : people) {
                dets.push_back(det.box);
                confs.push_back(det.conf);
                maxConf = std::max(maxConf, static_cast<double>(det.conf));
                sumConfAll += det.conf;
                ++nConfAll;
            }

            // Greedy IoU association, detections vs. existing tracks.
            std::vector<bool> detUsed(dets.size(), false);
            for (Track &t : tracks) {
                double bestIou = kIouMatch;
                int bestDet = -1;
                for (size_t d = 0; d < dets.size(); ++d) {
                    if (detUsed[d]) continue;
                    const double v = iou(t.box, dets[d]);
                    if (v > bestIou) { bestIou = v; bestDet = static_cast<int>(d); }
                }
                if (bestDet >= 0) {
                    detUsed[bestDet] = true;
                    if (t.missing) {
                        ++t.gaps;       // re-acquired after being lost
                        t.missing = false;
                    }
                    t.box = dets[bestDet];
                    t.framesTracked += kStride;
                    t.sumConf += confs[bestDet];
                    ++t.detections;
                    t.lastFrame = frameIdx;
                } else if (!t.missing && frameIdx - t.lastFrame > maxGapFrames) {
                    t.missing = true;
                }
            }
            for (size_t d = 0; d < dets.size(); ++d) {
                if (!detUsed[d]) {
                    Track t;
                    t.id = nextTrackId++;
                    t.box = dets[d];
                    t.lastFrame = frameIdx;
                    t.framesTracked = kStride;
                    t.sumConf = confs[d];
                    t.detections = 1;
                    tracks.push_back(t);
                }
            }

            // Frame quality chip (keep the worst state seen in the bucket).
            int state = Good;
            if (dets.empty()) {
                state = Lost;
                ++lostFrames;
            } else if (maxConf < kLowConf) {
                state = LowConf;
            }
            const int bucket = std::min(kChipCount - 1,
                                        frameIdx * kChipCount / totalFrames);
            chips[bucket] = std::max(chips[bucket].toInt(), state);

            if (lastEmit.elapsed() > 300) {
                emitSnapshot(frameIdx);
                lastEmit.restart();
            }
        }
        ++frameIdx;
    }

    emitSnapshot(completedAll ? totalFrames : frameIdx);
    emit runFinished(completedAll);
}
