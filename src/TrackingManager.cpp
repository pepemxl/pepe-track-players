#include "TrackingManager.h"
#include "YoloDetector.h"

#include <opencv2/opencv.hpp>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRectF>
#include <QSet>
#include <algorithm>
#include <climits>
#include <numeric>
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

QString TrackingManager::trackKey(int chunkNumber, int trackId)
{
    // Same format as the Tracking-tab table ids.
    return QStringLiteral("%1-T%2").arg(chunkNumber, 3, 10, QLatin1Char('0'))
                                   .arg(trackId, 2, 10, QLatin1Char('0'));
}

QVariantList TrackingManager::detectionsAt(double sec) const
{
    const int slot = qRound(sec * 10.0);
    const auto it = m_detsBySlot.constFind(slot);
    if (it == m_detsBySlot.constEnd())
        return {};
    const int chunkNumber = slot / 600 + 1;

    QVariantList list;
    for (const Det &d : *it) {
        QVariantMap m;
        m[QStringLiteral("trackId")] = d.trackId;
        m[QStringLiteral("key")] = trackKey(chunkNumber, d.trackId);
        m[QStringLiteral("x")] = d.x;
        m[QStringLiteral("y")] = d.y;
        m[QStringLiteral("w")] = d.w;
        m[QStringLiteral("h")] = d.h;
        m[QStringLiteral("conf")] = d.conf;

        const QString key = m.value(QStringLiteral("key")).toString();
        const auto manual = m_assignments.constFind(key);
        const auto inferred = m_inferred.constFind(key);
        if (manual != m_assignments.constEnd() || inferred != m_inferred.constEnd()) {
            const QVariantMap &info = manual != m_assignments.constEnd()
                ? manual.value() : inferred.value();
            m[QStringLiteral("assigned")] = true;
            m[QStringLiteral("inferred")] = manual == m_assignments.constEnd();
            m[QStringLiteral("playerNumber")] = info.value(QStringLiteral("number"));
            m[QStringLiteral("playerName")] = info.value(QStringLiteral("name"));
            m[QStringLiteral("team")] = info.value(QStringLiteral("team"));
        } else {
            m[QStringLiteral("assigned")] = false;
            m[QStringLiteral("inferred")] = false;
        }
        list.append(m);
    }
    return list;
}

int TrackingManager::chunkOfKey(const QString &key)
{
    return key.left(3).toInt();
}

QString TrackingManager::metadataDir() const
{
    if (m_assignmentsPath.isEmpty())
        return {};
    return QFileInfo(m_assignmentsPath).path()
           + QStringLiteral("/video_chunks_metadata");
}

void TrackingManager::writeChunkMetadata(int chunkNumber) const
{
    const QString dirPath = metadataDir();
    if (dirPath.isEmpty())
        return;
    QDir().mkpath(dirPath);

    QJsonObject assignments;
    auto add = [&assignments, chunkNumber](const QHash<QString, QVariantMap> &src,
                                           const char *source) {
        for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
            if (chunkOfKey(it.key()) != chunkNumber)
                continue;
            QJsonObject o = QJsonObject::fromVariantMap(it.value());
            o[QStringLiteral("source")] = QLatin1String(source);
            assignments[it.key()] = o;
        }
    };
    add(m_assignments, "manual");
    add(m_inferred, "inferred");

    QJsonObject root;
    root[QStringLiteral("chunk")] = chunkNumber;
    root[QStringLiteral("file")] = QStringLiteral("video_part_%1.mp4")
                                       .arg(chunkNumber, 3, 10, QLatin1Char('0'));
    root[QStringLiteral("generated")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root[QStringLiteral("assignments")] = assignments;

    QFile file(dirPath + QStringLiteral("/video_metadata_part_%1.json")
                             .arg(chunkNumber, 3, 10, QLatin1Char('0')));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void TrackingManager::refreshTrackRowNames()
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        QVariantMap row = m_tracks.at(i).toMap();
        const QString key = row.value(QStringLiteral("trackId")).toString();
        QString name;
        const auto manual = m_assignments.constFind(key);
        const auto inferred = m_inferred.constFind(key);
        if (manual != m_assignments.constEnd()) {
            name = QStringLiteral("%1 · %2")
                       .arg(manual->value(QStringLiteral("number")).toInt())
                       .arg(manual->value(QStringLiteral("name")).toString());
        } else if (inferred != m_inferred.constEnd()) {
            name = QStringLiteral("≈ %1 · %2")
                       .arg(inferred->value(QStringLiteral("number")).toInt())
                       .arg(inferred->value(QStringLiteral("name")).toString());
        }
        row[QStringLiteral("name")] = name;
        m_tracks[i] = row;
    }
}

void TrackingManager::assignTrack(const QString &key, int number,
                                  const QString &name, int team)
{
    QVariantMap info;
    info[QStringLiteral("number")] = number;
    info[QStringLiteral("name")] = name;
    info[QStringLiteral("team")] = team;
    m_assignments[key] = info;
    m_inferred.remove(key);   // a manual assignment supersedes an inferred one
    saveAssignments();
    writeChunkMetadata(chunkOfKey(key));

    refreshTrackRowNames();
    emit snapshotChanged();
    emit tracksUpdated(m_tracks);
}

void TrackingManager::clearAssignment(const QString &key)
{
    if (m_assignments.remove(key) == 0)
        return;
    saveAssignments();
    writeChunkMetadata(chunkOfKey(key));

    refreshTrackRowNames();
    emit snapshotChanged();
    emit tracksUpdated(m_tracks);
}

int TrackingManager::inferIdentities(bool allChunks, double currentSec)
{
    if (m_detsBySlot.isEmpty() || m_assignments.isEmpty())
        return 0;

    // First/last box of every chunk-track, for boundary matching.
    struct Ends
    {
        int firstSlot{INT_MAX};
        int lastSlot{-1};
        QRectF firstBox, lastBox;
    };
    QHash<int, QHash<int, Ends>> perChunk;
    int maxChunk = 0;
    for (auto it = m_detsBySlot.constBegin(); it != m_detsBySlot.constEnd(); ++it) {
        const int slot = it.key();
        const int chunk = slot / 600 + 1;
        maxChunk = std::max(maxChunk, chunk);
        for (const Det &d : it.value()) {
            Ends &e = perChunk[chunk][d.trackId];
            const QRectF box(d.x, d.y, d.w, d.h);
            if (slot < e.firstSlot) { e.firstSlot = slot; e.firstBox = box; }
            if (slot > e.lastSlot)  { e.lastSlot = slot;  e.lastBox = box; }
        }
    }

    auto rectIou = [](const QRectF &a, const QRectF &b) {
        const QRectF inter = a.intersected(b);
        const double ia = inter.width() * inter.height();
        const double ua = a.width() * a.height() + b.width() * b.height() - ia;
        return ua > 0.0 ? ia / ua : 0.0;
    };

    constexpr int    kBoundaryWindow = 30;   // 3 s at chunk fps
    constexpr double kMinIou = 0.2;
    constexpr int    kMergeGap = 20;         // 2 s: max death->birth gap in-chunk
    constexpr double kMergeDist = 1.2;       // in units of box height
    constexpr double kMinHeightRatio = 0.55;
    constexpr double kMaxHeightRatio = 1.8;

    // Grow identities outwards from the manual assignments; a track that
    // straddles a chunk boundary keeps (almost) the same box, so IoU
    // across the boundary identifies its continuation.
    QHash<QString, QVariantMap> identity = m_assignments;

    // A player exists once at a time: all tracks holding one identity must
    // be disjoint in time. This stops chains from drifting onto other
    // players and flooding the track graph.
    auto identityId = [](const QVariantMap &info) {
        return QStringLiteral("%1|%2")
            .arg(info.value(QStringLiteral("team")).toInt())
            .arg(info.value(QStringLiteral("number")).toInt());
    };
    QHash<QString, QVector<QPair<int, int>>> occupied;   // identityId -> slot intervals
    auto endsOfKey = [&perChunk](const QString &key) -> const Ends * {
        const int chunk = chunkOfKey(key);
        const int trackId = key.mid(5).toInt();   // "NNN-Txx"
        const auto cIt = perChunk.constFind(chunk);
        if (cIt == perChunk.constEnd())
            return nullptr;
        const auto tIt = cIt->constFind(trackId);
        return tIt != cIt->constEnd() ? &tIt.value() : nullptr;
    };
    for (auto it = identity.constBegin(); it != identity.constEnd(); ++it) {
        if (const Ends *e = endsOfKey(it.key()))
            occupied[identityId(it.value())].append({e->firstSlot, e->lastSlot});
    }
    // Brief coexistence is tolerated (duplicate boxes during hand-offs);
    // sustained coexistence means a different player.
    constexpr int kMaxCoexistSlots = 15;   // 1.5 s
    auto fitsIdentity = [&](const QVariantMap &info, const Ends &e) -> bool {
        const auto it = occupied.constFind(identityId(info));
        if (it == occupied.constEnd())
            return true;
        for (const auto &iv : it.value()) {
            const int overlap = std::min(e.lastSlot, iv.second)
                                - std::max(e.firstSlot, iv.first);
            if (overlap > kMaxCoexistSlots)
                return false;
        }
        return true;
    };
    auto tryAssign = [&](const QString &key, const QVariantMap &info,
                         const Ends &e) -> bool {
        if (!fitsIdentity(info, e))
            return false;
        identity[key] = info;
        occupied[identityId(info)].append({e.firstSlot, e.lastSlot});
        return true;
    };

    auto propagate = [&](int fromChunk, int toChunk) -> bool {
        const int boundary = std::min(fromChunk, toChunk) * 600;
        const bool forward = fromChunk < toChunk;
        bool changed = false;
        const auto fromChunkIt = perChunk.constFind(fromChunk);
        const auto toChunkIt = perChunk.constFind(toChunk);
        if (fromChunkIt == perChunk.constEnd() || toChunkIt == perChunk.constEnd())
            return false;
        const auto &fromTracks = fromChunkIt.value();
        const auto &toTracks = toChunkIt.value();

        for (auto fromIt = fromTracks.constBegin(); fromIt != fromTracks.constEnd(); ++fromIt) {
            const auto idIt = identity.constFind(trackKey(fromChunk, fromIt.key()));
            if (idIt == identity.constEnd())
                continue;
            // Copy: tryAssign inserts into `identity`, which can rehash and
            // invalidate references into it.
            const QVariantMap id = idIt.value();
            const Ends &fe = fromIt.value();
            const QRectF fromBox = forward ? fe.lastBox : fe.firstBox;
            const int fromEdge = forward ? fe.lastSlot : fe.firstSlot;
            if (std::abs(fromEdge - boundary) > kBoundaryWindow)
                continue;

            double best = kMinIou;
            int bestTrack = -1;
            for (auto toIt = toTracks.constBegin(); toIt != toTracks.constEnd(); ++toIt) {
                if (identity.contains(trackKey(toChunk, toIt.key())))
                    continue;
                const Ends &te = toIt.value();
                const QRectF toBox = forward ? te.firstBox : te.lastBox;
                const int toEdge = forward ? te.firstSlot : te.lastSlot;
                if (std::abs(toEdge - boundary) > kBoundaryWindow
                        || !fitsIdentity(id, te))
                    continue;
                const double v = rectIou(fromBox, toBox);
                if (v > best) { best = v; bestTrack = toIt.key(); }
            }
            if (bestTrack >= 0
                    && tryAssign(trackKey(toChunk, bestTrack), id,
                                 toTracks.value(bestTrack))) {
                changed = true;
            }
        }
        return changed;
    };

    // Merge broken tracks inside one chunk: the detector often loses a
    // player briefly, killing the track and starting a new one nearby. An
    // identified track hands its identity to the closest compatible track
    // born right after it dies (and, symmetrically, to the one that died
    // right before it was born).
    auto mergeWithin = [&](int chunk) -> bool {
        bool changed = false;
        const auto chunkIt = perChunk.constFind(chunk);
        if (chunkIt == perChunk.constEnd())
            return false;
        const auto &tracks = chunkIt.value();

        auto compatible = [&](const QRectF &a, const QRectF &b) {
            const double ha = a.height(), hb = b.height();
            if (ha <= 0 || hb <= 0)
                return false;
            const double ratio = ha / hb;
            if (ratio < kMinHeightRatio || ratio > kMaxHeightRatio)
                return false;
            const double dx = a.center().x() - b.center().x();
            const double dy = a.center().y() - b.center().y();
            return std::hypot(dx, dy) <= kMergeDist * std::max(ha, hb);
        };
        auto centerDist = [](const QRectF &a, const QRectF &b) {
            return std::hypot(a.center().x() - b.center().x(),
                              a.center().y() - b.center().y());
        };

        for (auto it = tracks.constBegin(); it != tracks.constEnd(); ++it) {
            const auto idIt = identity.constFind(trackKey(chunk, it.key()));
            if (idIt == identity.constEnd())
                continue;
            const QVariantMap id = idIt.value();   // copy: see propagate()
            const Ends &e = it.value();

            // Forward: nearest unidentified track born just after this one dies.
            double bestDist = 1e18;
            int bestTrack = -1;
            for (auto jt = tracks.constBegin(); jt != tracks.constEnd(); ++jt) {
                if (jt.key() == it.key()
                        || identity.contains(trackKey(chunk, jt.key())))
                    continue;
                const Ends &f = jt.value();
                const int gap = f.firstSlot - e.lastSlot;
                if (gap < 0 || gap > kMergeGap || !compatible(e.lastBox, f.firstBox)
                        || !fitsIdentity(id, f))
                    continue;
                const double d = centerDist(e.lastBox, f.firstBox);
                if (d < bestDist) { bestDist = d; bestTrack = jt.key(); }
            }
            if (bestTrack >= 0
                    && tryAssign(trackKey(chunk, bestTrack), id,
                                 tracks.value(bestTrack))) {
                changed = true;
            }

            // Backward: nearest unidentified track that died just before
            // this one was born.
            bestDist = 1e18;
            bestTrack = -1;
            for (auto jt = tracks.constBegin(); jt != tracks.constEnd(); ++jt) {
                if (jt.key() == it.key()
                        || identity.contains(trackKey(chunk, jt.key())))
                    continue;
                const Ends &f = jt.value();
                const int gap = e.firstSlot - f.lastSlot;
                if (gap < 0 || gap > kMergeGap || !compatible(f.lastBox, e.firstBox)
                        || !fitsIdentity(id, f))
                    continue;
                const double d = centerDist(f.lastBox, e.firstBox);
                if (d < bestDist) { bestDist = d; bestTrack = jt.key(); }
            }
            if (bestTrack >= 0
                    && tryAssign(trackKey(chunk, bestTrack), id,
                                 tracks.value(bestTrack))) {
                changed = true;
            }
        }
        return changed;
    };

    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (int c = 1; c <= maxChunk; ++c)
            changed = mergeWithin(c) || changed;
        for (int c = 1; c < maxChunk; ++c)
            changed = propagate(c, c + 1) || changed;
        for (int c = maxChunk; c > 1; --c)
            changed = propagate(c, c - 1) || changed;
        if (!changed)
            break;
    }

    const int currentChunk = static_cast<int>(currentSec * 10.0) / 600 + 1;
    m_inferred.clear();
    for (auto it = identity.constBegin(); it != identity.constEnd(); ++it) {
        if (m_assignments.contains(it.key()))
            continue;
        if (!allChunks && chunkOfKey(it.key()) != currentChunk)
            continue;
        m_inferred.insert(it.key(), it.value());
    }

    QSet<int> chunksToWrite;
    if (allChunks) {
        for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it)
            chunksToWrite.insert(chunkOfKey(it.key()));
        for (auto it = m_inferred.constBegin(); it != m_inferred.constEnd(); ++it)
            chunksToWrite.insert(chunkOfKey(it.key()));
    } else {
        chunksToWrite.insert(currentChunk);
    }
    for (int c : chunksToWrite)
        writeChunkMetadata(c);

    refreshTrackRowNames();
    emit snapshotChanged();
    emit tracksUpdated(m_tracks);
    return m_inferred.size();
}

void TrackingManager::clearInferred()
{
    if (m_inferred.isEmpty())
        return;
    QSet<int> chunks;
    for (auto it = m_inferred.constBegin(); it != m_inferred.constEnd(); ++it)
        chunks.insert(chunkOfKey(it.key()));
    m_inferred.clear();
    for (int c : chunks)
        writeChunkMetadata(c);

    refreshTrackRowNames();
    emit snapshotChanged();
    emit tracksUpdated(m_tracks);
}

void TrackingManager::saveAssignments() const
{
    if (m_assignmentsPath.isEmpty())
        return;
    QJsonObject assignments;
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it)
        assignments[it.key()] = QJsonObject::fromVariantMap(it.value());
    QJsonObject root;
    root[QStringLiteral("assignments")] = assignments;
    QFile file(m_assignmentsPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void TrackingManager::loadAssignments()
{
    m_assignments.clear();
    QFile file(m_assignmentsPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject assignments = QJsonDocument::fromJson(file.readAll())
                                        .object()[QStringLiteral("assignments")].toObject();
    for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it)
        m_assignments[it.key()] = it.value().toObject().toVariantMap();
}

void TrackingManager::setSource(const QString &path)
{
    stopInference();
    m_sourcePath = path;
    m_detsBySlot.clear();
    m_assignments.clear();
    m_inferred.clear();
    m_assignmentsPath.clear();
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

void TrackingManager::loadFromChunkCsvs(const QString &chunksDir, double durationSec,
                                        const std::vector<std::pair<double, double>> &excludedSec)
{
    if (m_runningInference || durationSec <= 0.0)
        return;
    const QStringList csvs = QDir(chunksDir).entryList(
        {QStringLiteral("video_part_*.csv")}, QDir::Files, QDir::Name);
    if (csvs.isEmpty())
        return;
    m_detsBySlot.clear();
    m_inferred.clear();

    // Player-to-track assignments live next to the chunks, in the match dir.
    QDir matchDir(chunksDir);
    matchDir.cdUp();
    m_assignmentsPath = matchDir.filePath(QStringLiteral("track_assignments.json"));
    loadAssignments();

    constexpr double kChunkFps = 10.0;
    constexpr double kChunkSeconds = 60.0;

    // Exact frame counts per chunk when chunks.json exists.
    QHash<int, int> chunkFrameCounts;
    QFile idx(chunksDir + QStringLiteral("/chunks.json"));
    if (idx.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(idx.readAll())
                                   .object()[QStringLiteral("chunks")].toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            chunkFrameCounts[o[QStringLiteral("number")].toInt()]
                = o[QStringLiteral("frames")].toInt();
        }
    }

    auto isExcluded = [&excludedSec](double sec) {
        for (const auto &r : excludedSec) {
            if (sec >= r.first && sec <= r.second)
                return true;
        }
        return false;
    };

    QVariantList chips;
    for (int i = 0; i < kChipCount; ++i)
        chips.append(0);   // NotProcessed

    struct TrackAgg { int count{0}; double sumConf{0.0}; int first{-1}; int last{-1}; };
    QVariantList trackRows;
    int processed = 0;
    int lostFrames = 0;
    int playersTracked = 0;
    double sumConfAll = 0.0;
    long long nConfAll = 0;

    for (const QString &csvName : csvs) {
        const int number = csvName.mid(11, 3).toInt();
        const double chunkStartSec = (number - 1) * kChunkSeconds;

        QFile file(chunksDir + QLatin1Char('/') + csvName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        // frame -> (detections, best conf); track -> aggregate
        QHash<int, QPair<int, double>> frames;
        QHash<int, TrackAgg> tracks;
        int maxFrame = -1;

        // Header: map column names so old CSVs (without chunk_*) parse too.
        const QStringList header =
            QString::fromUtf8(file.readLine()).trimmed().split(QLatin1Char(','));
        const int iFrame = header.indexOf(QStringLiteral("frame"));
        const int iTrack = header.indexOf(QStringLiteral("track_id"));
        const int iConf  = header.indexOf(QStringLiteral("conf"));
        const int iX = header.indexOf(QStringLiteral("x"));
        const int iY = header.indexOf(QStringLiteral("y"));
        const int iW = header.indexOf(QStringLiteral("w"));
        const int iH = header.indexOf(QStringLiteral("h"));
        if (iFrame < 0 || iTrack < 0 || iConf < 0)
            continue;

        while (!file.atEnd()) {
            const QStringList cols =
                QString::fromUtf8(file.readLine()).trimmed().split(QLatin1Char(','));
            if (cols.size() <= std::max({iFrame, iTrack, iConf, iX, iY, iW, iH}))
                continue;
            const int f = cols.at(iFrame).toInt();
            const int trackId = cols.at(iTrack).toInt();
            const double conf = cols.at(iConf).toDouble();
            maxFrame = std::max(maxFrame, f);

            if (iX >= 0 && iH >= 0) {
                Det det;
                det.trackId = trackId;
                det.x = cols.at(iX).toFloat();
                det.y = cols.at(iY).toFloat();
                det.w = cols.at(iW).toFloat();
                det.h = cols.at(iH).toFloat();
                det.conf = static_cast<float>(conf);
                m_detsBySlot[(number - 1) * 600 + f].append(det);
            }

            auto &fr = frames[f];
            ++fr.first;
            fr.second = std::max(fr.second, conf);

            TrackAgg &agg = tracks[trackId];
            ++agg.count;
            agg.sumConf += conf;
            if (agg.first < 0) agg.first = f;
            agg.last = f;

            sumConfAll += conf;
            ++nConfAll;
        }

        const int frameCount = chunkFrameCounts.value(number, maxFrame + 1);
        for (int f = 0; f < frameCount; ++f) {
            const double t = chunkStartSec + f / kChunkFps;
            if (isExcluded(t))
                continue;
            ++processed;
            int state;
            const auto it = frames.constFind(f);
            if (it == frames.constEnd()) {
                state = 3;   // Lost
                ++lostFrames;
            } else {
                state = it->second < kLowConf ? 2 : 1;   // LowConf / Good
            }
            const int bucket = std::min(kChipCount - 1,
                                        static_cast<int>(t / durationSec * kChipCount));
            chips[bucket] = std::max(chips[bucket].toInt(), state);
        }

        playersTracked = std::max(playersTracked, static_cast<int>(tracks.size()));

        for (auto it = tracks.constBegin(); it != tracks.constEnd(); ++it) {
            const TrackAgg &agg = it.value();
            const int gaps = (agg.last - agg.first + 1) - agg.count;
            QVariantMap row;
            const QString key = trackKey(number, it.key());
            row[QStringLiteral("trackId")] = key;
            const auto assigned = m_assignments.constFind(key);
            row[QStringLiteral("name")] = assigned != m_assignments.constEnd()
                ? QStringLiteral("%1 · %2")
                      .arg(assigned->value(QStringLiteral("number")).toInt())
                      .arg(assigned->value(QStringLiteral("name")).toString())
                : QString();
            row[QStringLiteral("framesTracked")] = agg.count;
            row[QStringLiteral("avgConf")] =
                QString::number(std::min(agg.sumConf / std::max(1, agg.count), 0.99), 'f', 2);
            QString status = QStringLiteral("stable");
            int kind = 0;
            if (gaps > 10) {
                status = QStringLiteral("lost");
                kind = 2;
            } else if (gaps > 0) {
                status = QStringLiteral("flickering");
                kind = 1;
            }
            row[QStringLiteral("status")] = status;
            row[QStringLiteral("statusKind")] = kind;
            trackRows.append(row);
        }
    }

    std::sort(trackRows.begin(), trackRows.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("framesTracked")).toInt()
             > b.toMap().value(QStringLiteral("framesTracked")).toInt();
    });
    while (trackRows.size() > 200)
        trackRows.removeLast();

    m_completed = true;
    m_progress = 1.0;
    m_framesProcessed = processed;
    m_playersTracked = playersTracked;
    m_avgConfidence = nConfAll > 0 ? std::min(sumConfAll / nConfAll, 0.99) : 0.0;
    m_lostFrames = lostFrames;
    m_frameChips = chips;
    m_tracks = trackRows;

    qInfo("Tracking tab loaded from %lld chunk CSVs: %d frames, %d tracks, %d lost, "
          "%lld detection slots, %lld assignments",
          static_cast<long long>(csvs.size()), processed,
          static_cast<int>(trackRows.size()), lostFrames,
          static_cast<long long>(m_detsBySlot.size()),
          static_cast<long long>(m_assignments.size()));

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
        // Only the strongest 200 tracks travel to the GUI. Serializing every
        // track of a long run into each queued snapshot (and instantiating a
        // table row for each) is what used to balloon memory during a rerun.
        constexpr int kMaxRows = 200;
        std::vector<int> order(tracks.size());
        std::iota(order.begin(), order.end(), 0);
        const int keep = std::min<int>(static_cast<int>(order.size()), kMaxRows);
        std::partial_sort(order.begin(), order.begin() + keep, order.end(),
                          [&](int a, int b) {
                              return tracks[a].framesTracked > tracks[b].framesTracked;
                          });

        QVariantList list;
        for (int i = 0; i < keep; ++i) {
            const Track &t = tracks[order[i]];
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

            // Greedy IoU association, detections vs. existing tracks. Dead
            // tracks never re-acquire: the reappearing player is a new track
            // (matches the chunk tracker's behavior).
            std::vector<bool> detUsed(dets.size(), false);
            for (Track &t : tracks) {
                if (frameIdx - t.lastFrame > maxGapFrames) {
                    t.missing = true;
                    continue;
                }
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

            // Drop dead noise tracks (one or two spurious detections) so the
            // track list — and the per-frame matching cost — stays bounded
            // over a long run.
            if (frameIdx % 300 == 0 && !tracks.empty()) {
                tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                    [&](const Track &t) {
                        return frameIdx - t.lastFrame > maxGapFrames
                               && t.detections <= 2;
                    }), tracks.end());
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
