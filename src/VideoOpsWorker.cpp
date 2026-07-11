#include "VideoOpsWorker.h"
#include "YoloDetector.h"

#include <opencv2/opencv.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>

namespace {

constexpr double kPreprocessFps = 20.0;
constexpr double kChunkFps      = 10.0;
constexpr int    kChunkSeconds  = 60;
constexpr double kTrackIou      = 0.3;
// A track unmatched for this long is dead: it may not re-acquire a
// detection later (that would entangle several players into one track);
// the player reappearing becomes a new track instead.
constexpr int    kTrackMaxMiss  = 15;   // 1.5 s at chunk fps

QString chunkName(int number, const char *ext)
{
    return QStringLiteral("video_part_%1.%2")
        .arg(number, 3, 10, QLatin1Char('0'))
        .arg(QLatin1String(ext));
}

double iou(const cv::Rect &a, const cv::Rect &b)
{
    const int inter = (a & b).area();
    const int uni   = a.area() + b.area() - inter;
    return uni > 0 ? static_cast<double>(inter) / uni : 0.0;
}

} // namespace

VideoOpsWorker::VideoOpsWorker(QObject *parent)
    : QThread(parent)
{
}

VideoOpsWorker::~VideoOpsWorker()
{
    stopAndWait();
}

void VideoOpsWorker::configure(Op op, const QString &sourcePath,
                               const QString &preprocessedPath, const QString &chunksDir,
                               const QRect &crop,
                               const std::vector<std::pair<double, double>> &excludedSec)
{
    m_op = op;
    m_sourcePath = sourcePath;
    m_preprocessedPath = preprocessedPath;
    m_chunksDir = chunksDir;
    m_crop = crop;
    m_excluded = excludedSec;
    m_stop.store(false);
}

void VideoOpsWorker::stopAndWait()
{
    m_stop.store(true);
    if (isRunning()) {
        wait();
    }
}

bool VideoOpsWorker::isExcluded(double sec) const
{
    for (const auto &[start, end] : m_excluded) {
        if (sec >= start && sec <= end)
            return true;
    }
    return false;
}

void VideoOpsWorker::run()
{
    switch (m_op) {
    case Preprocess: runPreprocess(); break;
    case Chunk:      runChunks();     break;
    case Track:      runTrack();      break;
    }
}

void VideoOpsWorker::runPreprocess()
{
    cv::VideoCapture cap;
    if (!cap.open(m_sourcePath.toStdString())) {
        emit opFinished(Preprocess, false, QStringLiteral("cannot open source video"), {});
        return;
    }
    double srcFps = cap.get(cv::CAP_PROP_FPS);
    if (srcFps <= 0.0 || srcFps > 240.0) srcFps = 30.0;
    const int total = std::max(1, static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)));
    const cv::Size srcSize(static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH)),
                           static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT)));

    // The selected camera view: clamp to the frame; empty = full frame.
    cv::Rect view(0, 0, srcSize.width, srcSize.height);
    if (m_crop.isValid() && !m_crop.isEmpty()) {
        view = cv::Rect(m_crop.x(), m_crop.y(), m_crop.width(), m_crop.height())
               & cv::Rect(0, 0, srcSize.width, srcSize.height);
        if (view.area() <= 0)
            view = cv::Rect(0, 0, srcSize.width, srcSize.height);
    }
    const cv::Size size(view.width, view.height);

    const QString outPath = m_preprocessedPath;
    cv::VideoWriter writer(outPath.toStdString(),
                           cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                           kPreprocessFps, size, true);
    if (!writer.isOpened()) {
        emit opFinished(Preprocess, false, QStringLiteral("cannot open writer: %1").arg(outPath), {});
        return;
    }

    // Time-based resample: emit one output frame per 1/20 s of source time,
    // duplicating or dropping source frames as needed.
    double nextOutSec = 0.0;
    int written = 0;
    cv::Mat frame;
    for (int idx = 0; cap.read(frame) && !frame.empty(); ++idx) {
        if (m_stop.load()) {
            writer.release();
            QFile::remove(outPath);
            emit opFinished(Preprocess, false, QStringLiteral("cancelled"), {});
            return;
        }
        const double t = idx / srcFps;
        while (t >= nextOutSec - 1e-9) {
            writer.write(frame(view));
            ++written;
            nextOutSec += 1.0 / kPreprocessFps;
        }
        if (idx % 50 == 0) {
            emit progressChanged(static_cast<double>(idx) / total,
                                 QStringLiteral("Re-encoding to 20 fps"));
        }
    }
    writer.release();

    QVariantMap result;
    result[QStringLiteral("path")] = outPath;
    result[QStringLiteral("frames")] = written;
    emit progressChanged(1.0, QStringLiteral("20 fps ready"));
    emit opFinished(Preprocess, true, {}, result);
}

void VideoOpsWorker::runChunks()
{
    // Prefer the 20 fps preprocessed video (already cropped) when it exists.
    const bool usePreprocessed = QFile::exists(m_preprocessedPath);
    const QString source = usePreprocessed ? m_preprocessedPath : m_sourcePath;

    cv::VideoCapture cap;
    if (!cap.open(source.toStdString())) {
        emit opFinished(Chunk, false, QStringLiteral("cannot open source video"), {});
        return;
    }
    double srcFps = cap.get(cv::CAP_PROP_FPS);
    if (srcFps <= 0.0 || srcFps > 240.0) srcFps = 30.0;
    const int total = std::max(1, static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)));
    const cv::Size srcSize(static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH)),
                           static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT)));

    // When chunking straight from the original, the view crop still applies.
    cv::Rect view(0, 0, srcSize.width, srcSize.height);
    if (!usePreprocessed && m_crop.isValid() && !m_crop.isEmpty()) {
        view = cv::Rect(m_crop.x(), m_crop.y(), m_crop.width(), m_crop.height())
               & cv::Rect(0, 0, srcSize.width, srcSize.height);
        if (view.area() <= 0)
            view = cv::Rect(0, 0, srcSize.width, srcSize.height);
    }
    const cv::Size size(view.width, view.height);

    const QString chunksDir = m_chunksDir;
    QDir().mkpath(chunksDir);
    // Remove stale chunks from a previous run.
    QDir dir(chunksDir);
    for (const QString &old : dir.entryList({QStringLiteral("video_part_*")}, QDir::Files))
        dir.remove(old);

    const int framesPerChunk = static_cast<int>(kChunkFps) * kChunkSeconds;
    cv::VideoWriter writer;
    int chunkNumber = 0;
    int writtenInChunk = 0;
    double nextOutSec = 0.0;
    cv::Mat frame;
    QJsonArray chunksIndex;   // start/end of every chunk -> chunks.json

    auto closeChunk = [&]() {
        if (writer.isOpened() && writtenInChunk > 0) {
            const double startSec = (chunkNumber - 1) * static_cast<double>(kChunkSeconds);
            QJsonObject entry;
            entry[QStringLiteral("number")]    = chunkNumber;
            entry[QStringLiteral("file")]      = chunkName(chunkNumber, "mp4");
            entry[QStringLiteral("frames")]    = writtenInChunk;
            entry[QStringLiteral("start_sec")] = startSec;
            entry[QStringLiteral("end_sec")]   = startSec + writtenInChunk / kChunkFps;
            chunksIndex.append(entry);
        }
        writer.release();
    };
    auto openNextChunk = [&]() -> bool {
        ++chunkNumber;
        writtenInChunk = 0;
        const QString path = chunksDir + QLatin1Char('/') + chunkName(chunkNumber, "mp4");
        return writer.open(path.toStdString(),
                           cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                           kChunkFps, size, true);
    };

    for (int idx = 0; cap.read(frame) && !frame.empty(); ++idx) {
        if (m_stop.load()) {
            writer.release();
            emit opFinished(Chunk, false, QStringLiteral("cancelled"), {});
            return;
        }
        const double t = idx / srcFps;
        while (t >= nextOutSec - 1e-9) {
            if (!writer.isOpened() || writtenInChunk >= framesPerChunk) {
                closeChunk();
                if (!openNextChunk()) {
                    emit opFinished(Chunk, false,
                                    QStringLiteral("cannot open chunk writer #%1").arg(chunkNumber), {});
                    return;
                }
            }
            writer.write(frame(view));
            ++writtenInChunk;
            nextOutSec += 1.0 / kChunkFps;
        }
        if (idx % 50 == 0) {
            emit progressChanged(static_cast<double>(idx) / total,
                                 QStringLiteral("Writing chunk %1").arg(chunkNumber));
        }
    }
    closeChunk();

    QJsonObject indexRoot;
    indexRoot[QStringLiteral("fps")]           = kChunkFps;
    indexRoot[QStringLiteral("chunk_seconds")] = kChunkSeconds;
    indexRoot[QStringLiteral("chunks")]        = chunksIndex;
    QFile indexFile(chunksDir + QStringLiteral("/chunks.json"));
    if (indexFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        indexFile.write(QJsonDocument(indexRoot).toJson(QJsonDocument::Indented));

    QVariantMap result;
    result[QStringLiteral("chunks")] = chunkNumber;
    emit progressChanged(1.0, QStringLiteral("%1 chunks ready").arg(chunkNumber));
    emit opFinished(Chunk, true, {}, result);
}

void VideoOpsWorker::runTrack()
{
    const QString chunksDir = m_chunksDir;
    const QStringList chunks = QDir(chunksDir).entryList(
        {QStringLiteral("video_part_*.mp4")}, QDir::Files, QDir::Name);
    if (chunks.isEmpty()) {
        emit opFinished(Track, false, QStringLiteral("no chunks found — create chunks first"), {});
        return;
    }

    YoloDetector detector;
    QString error;
    if (!detector.load(&error)) {
        emit opFinished(Track, false, error, {});
        return;
    }

    struct Tr { int id; cv::Rect box; int lastFrame; };
    int nextTrackId = 1;
    long long rowsWritten = 0;

    for (int c = 0; c < chunks.size(); ++c) {
        const QString chunkPath = chunksDir + QLatin1Char('/') + chunks.at(c);
        cv::VideoCapture cap;
        if (!cap.open(chunkPath.toStdString())) {
            emit opFinished(Track, false, QStringLiteral("cannot open %1").arg(chunks.at(c)), {});
            return;
        }
        const int chunkFrames = std::max(1, static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)));
        // Chunk number from the file name keeps CSV names aligned even if
        // some chunk were missing.
        const int number = chunks.at(c).mid(11, 3).toInt();
        const double chunkStartSec = (number - 1) * static_cast<double>(kChunkSeconds);
        const double chunkEndSec = chunkStartSec + chunkFrames / kChunkFps;
        const QString chunkRange = QString::number(chunkStartSec, 'f', 2) + QLatin1Char(',')
                                   + QString::number(chunkEndSec, 'f', 2);

        QFile csv(chunksDir + QLatin1Char('/') + chunkName(number, "csv"));
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit opFinished(Track, false, QStringLiteral("cannot write %1").arg(csv.fileName()), {});
            return;
        }
        QTextStream out(&csv);
        out << "frame,time_sec,track_id,x,y,w,h,conf,chunk_start_sec,chunk_end_sec\n";

        // Tracks do not carry across chunks: each CSV stands alone.
        std::vector<Tr> tracks;

        cv::Mat frame;
        for (int f = 0; cap.read(frame) && !frame.empty(); ++f) {
            if (m_stop.load()) {
                emit opFinished(Track, false, QStringLiteral("cancelled"), {});
                return;
            }
            const double absSec = chunkStartSec + f / kChunkFps;
            if (isExcluded(absSec))
                continue;   // pre-match / post-match / commercial: discarded

            const auto dets = detector.detect(frame);

            std::vector<bool> used(dets.size(), false);
            for (Tr &t : tracks) {
                if (f - t.lastFrame > kTrackMaxMiss)
                    continue;   // dead track: never re-acquires
                double best = kTrackIou;
                int bestIdx = -1;
                for (size_t d = 0; d < dets.size(); ++d) {
                    if (used[d]) continue;
                    const double v = iou(t.box, dets[d].box);
                    if (v > best) { best = v; bestIdx = static_cast<int>(d); }
                }
                if (bestIdx >= 0) {
                    used[bestIdx] = true;
                    t.box = dets[bestIdx].box;
                    t.lastFrame = f;
                }
            }
            for (size_t d = 0; d < dets.size(); ++d) {
                if (!used[d])
                    tracks.push_back({nextTrackId++, dets[d].box, f});
            }
            for (const Tr &t : tracks) {
                if (t.lastFrame != f)
                    continue;
                const auto it = std::find_if(dets.begin(), dets.end(),
                    [&](const YoloDetector::Detection &dd) { return dd.box == t.box; });
                const float conf = it != dets.end() ? it->conf : 0.f;
                out << f << ',' << QString::number(absSec, 'f', 2) << ','
                    << t.id << ',' << t.box.x << ',' << t.box.y << ','
                    << t.box.width << ',' << t.box.height << ','
                    << QString::number(conf, 'f', 2) << ','
                    << chunkRange << '\n';
                ++rowsWritten;
            }

            if (f % 10 == 0) {
                const double frac = (c + static_cast<double>(f) / chunkFrames) / chunks.size();
                emit progressChanged(frac, QStringLiteral("Tracking chunk %1 / %2")
                                               .arg(c + 1).arg(chunks.size()));
            }
        }
    }

    QVariantMap result;
    result[QStringLiteral("chunks")] = chunks.size();
    result[QStringLiteral("rows")] = static_cast<qlonglong>(rowsWritten);
    emit progressChanged(1.0, QStringLiteral("Tracking CSVs ready"));
    emit opFinished(Track, true, {}, result);
}
