#include "TrackingManager.h"

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <vector>

namespace {

constexpr int    kStride       = 5;     // run the detector every N frames
constexpr int    kInputSize    = 640;   // YOLO letterbox input
constexpr float  kConfThresh   = 0.35f;
constexpr float  kNmsThresh    = 0.45f;
constexpr double kLowConf      = 0.5;   // below this a detection is "low confidence"
constexpr double kIouMatch     = 0.2;

// Same model as videopp's player tools (COCO detection, person = class 0).
QString resolveModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/models/yolov8n.onnx"),
        appDir + QStringLiteral("/../../models/yolov8n.onnx"),
        QStringLiteral("models/yolov8n.onnx"),
        QStringLiteral("D:/SANDBOX/videopp/LOCAL_DATA/models/yolov8n.onnx"),
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c))
            return QFileInfo(c).absoluteFilePath();
    }
    return {};
}

// Ultralytics YOLOv8 detection decode (letterbox pattern shared with
// videopp's PoseDetector): output [1, 4+80, anchors], person score at row 4.
std::vector<std::pair<cv::Rect, float>> detectPeople(cv::dnn::Net &net, const cv::Mat &frame)
{
    const int origW = frame.cols;
    const int origH = frame.rows;
    const float r = std::min(static_cast<float>(kInputSize) / origW,
                             static_cast<float>(kInputSize) / origH);
    const int newW = static_cast<int>(std::round(origW * r));
    const int newH = static_cast<int>(std::round(origH * r));
    const int padX = (kInputSize - newW) / 2;
    const int padY = (kInputSize - newH) / 2;

    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(newW, newH));
    cv::Mat canvas(kInputSize, kInputSize, frame.type(), cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(padX, padY, newW, newH)));

    cv::Mat blob = cv::dnn::blobFromImage(canvas, 1.0 / 255.0,
                                          cv::Size(kInputSize, kInputSize),
                                          cv::Scalar(), /*swapRB=*/true, /*crop=*/false);
    net.setInput(blob);

    std::vector<cv::Mat> outputs;
    net.forward(outputs, net.getUnconnectedOutLayersNames());
    if (outputs.empty())
        return {};

    cv::Mat output = outputs[0];
    if (output.dims == 3) {
        output = cv::Mat(output.size[1], output.size[2], CV_32F,
                         const_cast<void *>(static_cast<const void *>(output.ptr<float>())))
                     .clone();
        cv::transpose(output, output);
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    for (int i = 0; i < output.rows; ++i) {
        const float *row = output.ptr<float>(i);
        const float personScore = row[4];   // class 0 = person
        if (personScore < kConfThresh)
            continue;
        const float cx = row[0], cy = row[1], w = row[2], h = row[3];
        boxes.emplace_back(static_cast<int>((cx - w * 0.5f - padX) / r),
                           static_cast<int>((cy - h * 0.5f - padY) / r),
                           static_cast<int>(w / r),
                           static_cast<int>(h / r));
        scores.push_back(personScore);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, scores, kConfThresh, kNmsThresh, keep);

    std::vector<std::pair<cv::Rect, float>> result;
    const cv::Rect frameRect(0, 0, origW, origH);
    for (int k : keep) {
        const cv::Rect clipped = boxes[k] & frameRect;
        if (clipped.area() > 0)
            result.emplace_back(clipped, scores[k]);
    }
    return result;
}

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
    const QString modelPath = resolveModelPath();
    if (modelPath.isEmpty()) {
        emit error(QStringLiteral("Tracking: yolov8n.onnx not found (expected in models/)"));
        emit runFinished(false);
        return;
    }
    cv::dnn::Net net;
    try {
        net = cv::dnn::readNetFromONNX(modelPath.toStdString());
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    } catch (const cv::Exception &e) {
        emit error(QStringLiteral("Tracking: failed to load model: %1")
                       .arg(QString::fromStdString(e.msg)));
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

        if (frameIdx % kStride == 0) {
            if (!cap.retrieve(frame) || frame.empty()) {
                ++frameIdx;
                continue;
            }

            const auto people = detectPeople(net, frame);

            std::vector<cv::Rect> dets;
            std::vector<double> confs;
            double maxConf = 0.0;
            for (const auto &[box, conf] : people) {
                dets.push_back(box);
                confs.push_back(conf);
                maxConf = std::max(maxConf, static_cast<double>(conf));
                sumConfAll += conf;
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
