#include "MatchManager.h"
#include "VideoOpsWorker.h"
#include "LineupExtractor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <algorithm>
#include <climits>

namespace {

const QStringList kRoles = {
    QStringLiteral("tv_feed"), QStringLiteral("tactical"),
    QStringLiteral("panoramic"), QStringLiteral("other"),
};

QString sanitizeRole(const QString &role)
{
    return kRoles.contains(role) ? role : QStringLiteral("other");
}

const QStringList kSegments = {
    QStringLiteral("full"), QStringLiteral("first_half"),
    QStringLiteral("second_half"), QStringLiteral("extra1"),
    QStringLiteral("extra2"), QStringLiteral("penalties"),
    QStringLiteral("partial_first_half"), QStringLiteral("partial_second_half"),
};

QString sanitizeSegment(const QString &segment)
{
    return kSegments.contains(segment) ? segment : QStringLiteral("full");
}

// "*_start"/"*_end" markers (other than commercials) open/close a play
// period; tracking only runs inside play periods.
bool isPeriodStart(const QString &type)
{
    return type.endsWith(QLatin1String("_start"))
           && type != QLatin1String("commercial_start");
}

bool isPeriodEnd(const QString &type)
{
    return type.endsWith(QLatin1String("_end"))
           && type != QLatin1String("commercial_end");
}

// Old entries carried a single "video" path with match-level status; new
// entries hold a "videos" array with per-video state.
QJsonObject normalizedMatchEntry(const QJsonObject &entry)
{
    QJsonObject out = entry;
    if (!out.contains(QStringLiteral("videos"))) {
        QJsonObject video;
        video[QStringLiteral("id")]           = 1;
        video[QStringLiteral("role")]         = QStringLiteral("tv_feed");
        video[QStringLiteral("segment")]      = QStringLiteral("full");
        video[QStringLiteral("path")]         = entry[QStringLiteral("video")].toString();
        video[QStringLiteral("status")]       = entry[QStringLiteral("status")].toString();
        video[QStringLiteral("fps")]          = entry[QStringLiteral("fps")].toDouble();
        video[QStringLiteral("total_frames")] = entry[QStringLiteral("total_frames")].toInt();
        video[QStringLiteral("preprocessed")] = entry[QStringLiteral("preprocessed")].toString();
        video[QStringLiteral("chunks")]       = entry[QStringLiteral("chunks")].toInt();
        QJsonArray videos;
        videos.append(video);
        out[QStringLiteral("videos")] = videos;
        out.remove(QStringLiteral("video"));
        out.remove(QStringLiteral("status"));
        out.remove(QStringLiteral("fps"));
        out.remove(QStringLiteral("total_frames"));
        out.remove(QStringLiteral("preprocessed"));
        out.remove(QStringLiteral("chunks"));
    }
    return out;
}

QRect cropFromJson(const QJsonObject &video)
{
    const QJsonObject c = video[QStringLiteral("crop")].toObject();
    return QRect(c[QStringLiteral("x")].toInt(), c[QStringLiteral("y")].toInt(),
                 c[QStringLiteral("w")].toInt(), c[QStringLiteral("h")].toInt());
}

} // namespace

MatchManager::MatchManager(QObject *parent)
    : QObject(parent)
{
    m_worker = new VideoOpsWorker(this);
    // Worker signals are emitted from its thread: queued delivery.
    connect(m_worker, &VideoOpsWorker::progressChanged,
            this, &MatchManager::onOpProgress, Qt::QueuedConnection);
    connect(m_worker, &VideoOpsWorker::opFinished,
            this, &MatchManager::onOpFinished, Qt::QueuedConnection);

    m_lineup = new LineupExtractor(this);
    connect(m_lineup, &LineupExtractor::progressChanged,
            this, &MatchManager::onOpProgress, Qt::QueuedConnection);
    connect(m_lineup, &LineupExtractor::finishedExtraction,
            this, &MatchManager::onLineupsFinished, Qt::QueuedConnection);
}

MatchManager::~MatchManager()
{
    m_worker->stopAndWait();
    m_lineup->stopAndWait();
}

QString MatchManager::dataRoot()
{
    const QString env = qEnvironmentVariable("PEPETRACK_DATA_DIR");
    if (!env.isEmpty())
        return env;
    // Dev layout: <root>/build/bin/app.exe -> <root>/LOCAL_DATA
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir root(appDir + QStringLiteral("/../.."));
    if (root.exists(QStringLiteral("CMakeLists.txt")) || root.exists(QStringLiteral("LOCAL_DATA")))
        return root.absoluteFilePath(QStringLiteral("LOCAL_DATA"));
    return appDir + QStringLiteral("/LOCAL_DATA");
}

QString MatchManager::matchesDir() const
{
    return dataRoot() + QStringLiteral("/matches");
}

QString MatchManager::gamesJsonPath() const
{
    return matchesDir() + QStringLiteral("/games.json");
}

QString MatchManager::matchDirName() const
{
    return QStringLiteral("match_%1").arg(m_matchId, 4, 10, QLatin1Char('0'));
}

QString MatchManager::videoSuffix() const
{
    return QStringLiteral("_%1").arg(m_videoId, 2, 10, QLatin1Char('0'));
}

QString MatchManager::preprocessedFile() const
{
    return m_matchDir + QStringLiteral("/preprocessed_20fps") + videoSuffix()
           + QStringLiteral(".mp4");
}

QString MatchManager::chunksDir() const
{
    return m_matchDir + QStringLiteral("/video_chunks") + videoSuffix();
}

QString MatchManager::chunksMetadataDir() const
{
    return m_matchDir + QStringLiteral("/video_chunks_metadata") + videoSuffix();
}

QString MatchManager::lineupsDir() const
{
    return m_matchDir + QStringLiteral("/lineups") + videoSuffix();
}

QString MatchManager::lineupsJsonPath() const
{
    return m_matchDir + QStringLiteral("/lineups") + videoSuffix()
           + QStringLiteral(".json");
}

QString MatchManager::markersPath() const
{
    return m_matchDir + QStringLiteral("/markers") + videoSuffix()
           + QStringLiteral(".json");
}

QString MatchManager::assignmentsPath() const
{
    return m_matchDir + QStringLiteral("/track_assignments") + videoSuffix()
           + QStringLiteral(".json");
}

void MatchManager::setVideo(const QString &path, double fps, int totalFrames)
{
    m_worker->stopAndWait();
    m_lineup->stopAndWait();
    // Normalize so CLI backslash paths match the registry entries.
    m_videoPath = QFileInfo(path).absoluteFilePath();
    m_fps = fps > 0.0 ? fps : 25.0;
    m_totalFrames = totalFrames;
    m_matchId = 0;
    m_videoId = 0;
    m_videoRole.clear();
    m_videoSegment.clear();
    m_crop = QRect();
    m_cropPending = false;
    m_status.clear();
    m_matchDir.clear();
    m_chunkCount = 0;
    m_preprocessedPath.clear();
    m_videosJson = QJsonArray();
    m_markers.clear();
    m_opRunning = false;
    m_opProgress = 0.0;
    m_opLabel.clear();
    m_lastError.clear();

    registerOrLoad();
    loadMarkers();

    emit matchChanged();
    emit markersChanged();
    emit opStateChanged();
}

void MatchManager::registerOrLoad()
{
    QDir().mkpath(matchesDir());

    QJsonArray matches;
    QFile file(gamesJsonPath());
    if (file.open(QIODevice::ReadOnly)) {
        matches = QJsonDocument::fromJson(file.readAll())
                      .object()[QStringLiteral("matches")].toArray();
        file.close();
    }

    int maxId = 0;
    for (const QJsonValue &v : matches)
        maxId = std::max(maxId, v.toObject()[QStringLiteral("id")].toInt());

    auto loadVideoFields = [this](const QJsonObject &entry, const QJsonObject &video,
                                  const QJsonArray &videos) {
        m_matchId = entry[QStringLiteral("id")].toInt();
        m_videoId = video[QStringLiteral("id")].toInt();
        m_videoRole = sanitizeRole(video[QStringLiteral("role")].toString());
        m_videoSegment = sanitizeSegment(video[QStringLiteral("segment")].toString());
        m_status = video[QStringLiteral("status")].toString();
        m_chunkCount = video[QStringLiteral("chunks")].toInt();
        m_preprocessedPath = video[QStringLiteral("preprocessed")].toString();
        m_crop = cropFromJson(video);
        m_videosJson = videos;
    };

    // 1) Explicit open of one project video by (match, video id): resolves
    //    duplicates when the same file holds several camera views.
    if (m_pendingOpenMatchId > 0) {
        for (const QJsonValue &v : matches) {
            const QJsonObject entry = normalizedMatchEntry(v.toObject());
            if (entry[QStringLiteral("id")].toInt() != m_pendingOpenMatchId)
                continue;
            const QJsonArray videos = entry[QStringLiteral("videos")].toArray();
            for (const QJsonValue &vv : videos) {
                const QJsonObject video = vv.toObject();
                if (video[QStringLiteral("id")].toInt() == m_pendingOpenVideoId
                        && video[QStringLiteral("path")].toString() == m_videoPath) {
                    loadVideoFields(entry, video, videos);
                    break;
                }
            }
            break;
        }
    }

    bool newVideo = false;
    // 2) Add to the requested match: always a fresh entry — the same path
    //    may repeat (another view of a multi-view file).
    if (m_matchId == 0 && m_pendingAddMatchId > 0) {
        for (const QJsonValue &v : matches) {
            const QJsonObject entry = normalizedMatchEntry(v.toObject());
            if (entry[QStringLiteral("id")].toInt() != m_pendingAddMatchId)
                continue;
            m_matchId = m_pendingAddMatchId;
            m_videosJson = entry[QStringLiteral("videos")].toArray();
            int maxVideoId = 0;
            for (const QJsonValue &vv : m_videosJson)
                maxVideoId = std::max(maxVideoId,
                                      vv.toObject()[QStringLiteral("id")].toInt());
            m_videoId = maxVideoId + 1;
            m_videoRole = sanitizeRole(m_pendingAddRole);
            m_videoSegment = sanitizeSegment(m_pendingAddSegment);
            m_status = QStringLiteral("registered");
            newVideo = true;
            break;
        }
    }

    // 3) Plain lookup by path (standalone open, CLI): first entry wins.
    if (m_matchId == 0) {
        for (const QJsonValue &v : matches) {
            const QJsonObject entry = normalizedMatchEntry(v.toObject());
            const QJsonArray videos = entry[QStringLiteral("videos")].toArray();
            for (const QJsonValue &vv : videos) {
                const QJsonObject video = vv.toObject();
                if (video[QStringLiteral("path")].toString() == m_videoPath) {
                    loadVideoFields(entry, video, videos);
                    break;
                }
            }
            if (m_matchId > 0)
                break;
        }
    }

    // 4) Brand-new match (project) with this as its first video.
    if (m_matchId == 0) {
        m_matchId = maxId + 1;
        m_videoId = 1;
        m_videoRole = QStringLiteral("tv_feed");
        m_videoSegment = QStringLiteral("full");
        m_status = QStringLiteral("registered");
        m_videosJson = QJsonArray();
        newVideo = true;
    }
    m_pendingAddMatchId = 0;
    m_pendingAddRole.clear();
    m_pendingAddSegment.clear();
    m_pendingOpenMatchId = 0;
    m_pendingOpenVideoId = 0;
    m_cropPending = newVideo;

    m_matchDir = matchesDir() + QLatin1Char('/') + matchDirName();
    // Migrate a folder created before ids were zero-padded.
    const QString legacyDir = matchesDir() + QStringLiteral("/match_%1").arg(m_matchId);
    if (legacyDir != m_matchDir && QDir(legacyDir).exists() && !QDir(m_matchDir).exists())
        QDir().rename(legacyDir, m_matchDir);
    QDir().mkpath(m_matchDir);

    migrateLegacyArtifacts();

    // Normalize the preprocessed reference.
    if (QFile::exists(preprocessedFile()))
        m_preprocessedPath = matchDirName() + QStringLiteral("/preprocessed_20fps")
                             + videoSuffix() + QStringLiteral(".mp4");

    m_lineupsExtracted = QFile::exists(lineupsJsonPath());

    loadMatchName();
    updateGamesEntry();
}

void MatchManager::loadMatchName()
{
    m_matchName.clear();
    if (m_matchId <= 0)
        return;
    QFile file(gamesJsonPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray matches = QJsonDocument::fromJson(file.readAll())
                                   .object()[QStringLiteral("matches")].toArray();
    for (const QJsonValue &v : matches) {
        const QJsonObject e = v.toObject();
        if (e[QStringLiteral("id")].toInt() == m_matchId) {
            m_matchName = e[QStringLiteral("name")].toString();
            return;
        }
    }
}

void MatchManager::renameProject(const QString &name)
{
    if (m_matchId <= 0)
        return;
    const QString trimmed = name.trimmed();
    if (trimmed == m_matchName)
        return;
    m_matchName = trimmed;
    updateGamesEntry();   // rewrites this match's entry, including the name
    emit matchChanged();
}

bool MatchManager::deleteProject()
{
    if (m_matchId <= 0)
        return false;
    m_worker->stopAndWait();
    m_lineup->stopAndWait();

    // Drop this match's entry from games.json.
    QFile file(gamesJsonPath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray matches = QJsonDocument::fromJson(file.readAll())
                                       .object()[QStringLiteral("matches")].toArray();
        file.close();
        QJsonArray kept;
        for (const QJsonValue &v : matches)
            if (v.toObject()[QStringLiteral("id")].toInt() != m_matchId)
                kept.append(v);
        QJsonObject root;
        root[QStringLiteral("matches")] = kept;
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
        }
    }

    // Delete the whole artifact directory (chunks, masks, lineups, ...).
    if (!m_matchDir.isEmpty()) {
        QDir dir(m_matchDir);
        if (dir.exists())
            dir.removeRecursively();
    }

    // Reset to a no-project state.
    m_matchId = 0;
    m_matchName.clear();
    m_videoId = 0;
    m_videoRole.clear();
    m_videoSegment.clear();
    m_crop = QRect();
    m_cropPending = false;
    m_status.clear();
    m_matchDir.clear();
    m_chunkCount = 0;
    m_preprocessedPath.clear();
    m_videosJson = QJsonArray();
    m_markers.clear();
    m_lineupsExtracted = false;
    m_videoPath.clear();

    emit matchChanged();
    emit markersChanged();
    return true;
}

void MatchManager::migrateLegacyArtifacts()
{
    // Files written before the per-video suffix existed belong to video 1.
    if (m_videoId != 1)
        return;
    const QList<QPair<QString, QString>> renames = {
        { QStringLiteral("/markers.json"),            QStringLiteral("/markers_01.json") },
        { QStringLiteral("/preprocessed_20fps.mp4"),  QStringLiteral("/preprocessed_20fps_01.mp4") },
        { QStringLiteral("/video_chunks"),            QStringLiteral("/video_chunks_01") },
        { QStringLiteral("/video_chunks_metadata"),   QStringLiteral("/video_chunks_metadata_01") },
        { QStringLiteral("/lineups"),                 QStringLiteral("/lineups_01") },
        { QStringLiteral("/lineups.json"),            QStringLiteral("/lineups_01.json") },
        { QStringLiteral("/track_assignments.json"),  QStringLiteral("/track_assignments_01.json") },
    };
    for (const auto &[from, to] : renames) {
        const QString src = m_matchDir + from;
        const QString dst = m_matchDir + to;
        if (QFileInfo::exists(src) && !QFileInfo::exists(dst))
            QDir().rename(src, dst);
    }
}

void MatchManager::updateGamesEntry()
{
    QJsonArray matches;
    QFile file(gamesJsonPath());
    if (file.open(QIODevice::ReadOnly)) {
        matches = QJsonDocument::fromJson(file.readAll())
                      .object()[QStringLiteral("matches")].toArray();
        file.close();
    }

    // Current video entry.
    QJsonObject video;
    video[QStringLiteral("id")]           = m_videoId;
    video[QStringLiteral("role")]         = m_videoRole;
    video[QStringLiteral("segment")]      = m_videoSegment;
    video[QStringLiteral("path")]         = m_videoPath;
    video[QStringLiteral("status")]       = m_status;
    video[QStringLiteral("fps")]          = m_fps;
    video[QStringLiteral("total_frames")] = m_totalFrames;
    video[QStringLiteral("preprocessed")] = m_preprocessedPath;
    video[QStringLiteral("chunks")]       = m_chunkCount;
    if (hasCrop()) {
        QJsonObject c;
        c[QStringLiteral("x")] = m_crop.x();
        c[QStringLiteral("y")] = m_crop.y();
        c[QStringLiteral("w")] = m_crop.width();
        c[QStringLiteral("h")] = m_crop.height();
        video[QStringLiteral("crop")] = c;
    }

    // Merge into the match's videos array.
    bool videoReplaced = false;
    for (int i = 0; i < m_videosJson.size(); ++i) {
        if (m_videosJson[i].toObject()[QStringLiteral("id")].toInt() == m_videoId) {
            m_videosJson[i] = video;
            videoReplaced = true;
            break;
        }
    }
    if (!videoReplaced)
        m_videosJson.append(video);

    QJsonObject entry;
    entry[QStringLiteral("id")]      = m_matchId;
    entry[QStringLiteral("dir")]     = matchDirName();
    if (!m_matchName.isEmpty())
        entry[QStringLiteral("name")] = m_matchName;
    entry[QStringLiteral("videos")]  = m_videosJson;
    entry[QStringLiteral("updated")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    bool replaced = false;
    for (int i = 0; i < matches.size(); ++i) {
        if (matches[i].toObject()[QStringLiteral("id")].toInt() == m_matchId) {
            matches[i] = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        matches.append(entry);

    QJsonObject root;
    root[QStringLiteral("matches")] = matches;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

QVariantList MatchManager::videos() const
{
    QVariantList list;
    for (const QJsonValue &v : m_videosJson) {
        const QJsonObject video = v.toObject();
        QVariantMap m;
        m[QStringLiteral("id")]      = video[QStringLiteral("id")].toInt();
        m[QStringLiteral("role")]    = video[QStringLiteral("role")].toString();
        m[QStringLiteral("segment")] = sanitizeSegment(video[QStringLiteral("segment")].toString());
        m[QStringLiteral("path")]    = video[QStringLiteral("path")].toString();
        m[QStringLiteral("status")]  = video[QStringLiteral("status")].toString();
        m[QStringLiteral("chunks")]  = video[QStringLiteral("chunks")].toInt();
        m[QStringLiteral("current")] = video[QStringLiteral("id")].toInt() == m_videoId;
        const QRect crop = cropFromJson(video);
        m[QStringLiteral("view")] = crop.isValid() && !crop.isEmpty()
            ? QStringLiteral("%1×%2").arg(crop.width()).arg(crop.height())
            : QString();
        list.append(m);
    }
    return list;
}

QVariantList MatchManager::listProjects() const
{
    QVariantList projects;
    QFile file(gamesJsonPath());
    if (!file.open(QIODevice::ReadOnly))
        return projects;
    const QJsonArray matches = QJsonDocument::fromJson(file.readAll())
                                   .object()[QStringLiteral("matches")].toArray();
    for (const QJsonValue &v : matches) {
        const QJsonObject entry = normalizedMatchEntry(v.toObject());
        QVariantMap m;
        m[QStringLiteral("id")]   = entry[QStringLiteral("id")].toInt();
        m[QStringLiteral("dir")]  = entry[QStringLiteral("dir")].toString();
        m[QStringLiteral("name")] = entry[QStringLiteral("name")].toString();
        QVariantList videos;
        for (const QJsonValue &vv : entry[QStringLiteral("videos")].toArray()) {
            const QJsonObject video = vv.toObject();
            QVariantMap vm;
            vm[QStringLiteral("id")]      = video[QStringLiteral("id")].toInt();
            vm[QStringLiteral("role")]    = video[QStringLiteral("role")].toString();
            vm[QStringLiteral("segment")] = sanitizeSegment(video[QStringLiteral("segment")].toString());
            vm[QStringLiteral("path")]    = video[QStringLiteral("path")].toString();
            vm[QStringLiteral("status")]  = video[QStringLiteral("status")].toString();
            videos.append(vm);
        }
        m[QStringLiteral("videos")] = videos;
        projects.append(m);
    }
    return projects;
}

int MatchManager::createProject()
{
    m_worker->stopAndWait();
    m_lineup->stopAndWait();
    QDir().mkpath(matchesDir());

    QJsonArray matches;
    QFile file(gamesJsonPath());
    if (file.open(QIODevice::ReadOnly)) {
        matches = QJsonDocument::fromJson(file.readAll())
                      .object()[QStringLiteral("matches")].toArray();
        file.close();
    }
    int maxId = 0;
    for (const QJsonValue &v : matches)
        maxId = std::max(maxId, v.toObject()[QStringLiteral("id")].toInt());

    // Empty project context: no current video until one is added.
    m_matchId = maxId + 1;
    m_matchName.clear();
    m_videoId = 0;
    m_videoPath.clear();
    m_videoRole.clear();
    m_videoSegment.clear();
    m_crop = QRect();
    m_cropPending = false;
    m_status.clear();
    m_chunkCount = 0;
    m_preprocessedPath.clear();
    m_videosJson = QJsonArray();
    m_markers.clear();
    m_lineupsExtracted = false;
    m_lastError.clear();

    m_matchDir = matchesDir() + QLatin1Char('/') + matchDirName();
    QDir().mkpath(m_matchDir);

    QJsonObject entry;
    entry[QStringLiteral("id")]      = m_matchId;
    entry[QStringLiteral("dir")]     = matchDirName();
    entry[QStringLiteral("videos")]  = QJsonArray();
    entry[QStringLiteral("updated")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    matches.append(entry);

    QJsonObject root;
    root[QStringLiteral("matches")] = matches;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    emit matchChanged();
    emit markersChanged();
    emit opStateChanged();
    return m_matchId;
}

void MatchManager::prepareAddVideo(const QString &role, const QString &segment)
{
    if (!registered())
        return;
    m_pendingAddMatchId = m_matchId;
    m_pendingAddRole = sanitizeRole(role);
    m_pendingAddSegment = sanitizeSegment(segment);
    m_pendingOpenMatchId = 0;
    m_pendingOpenVideoId = 0;
}

void MatchManager::prepareOpenVideo(int matchId, int videoId)
{
    m_pendingOpenMatchId = matchId;
    m_pendingOpenVideoId = videoId;
    m_pendingAddMatchId = 0;
    m_pendingAddRole.clear();
    m_pendingAddSegment.clear();
}

void MatchManager::setCrop(int x, int y, int width, int height)
{
    if (!registered() || m_videoId <= 0 || width <= 0 || height <= 0)
        return;
    m_crop = QRect(x, y, width, height);
    m_cropPending = false;
    // Downstream artifacts were produced with the previous view.
    if (m_status != QLatin1String("registered")) {
        m_status = QStringLiteral("registered");
        m_chunkCount = 0;
    }
    updateGamesEntry();
    emit matchChanged();
}

void MatchManager::clearCrop()
{
    if (!registered() || m_videoId <= 0)
        return;
    const bool had = hasCrop();
    m_crop = QRect();
    m_cropPending = false;
    if (had && m_status != QLatin1String("registered")) {
        m_status = QStringLiteral("registered");
        m_chunkCount = 0;
    }
    updateGamesEntry();
    emit matchChanged();
}

void MatchManager::loadMarkers()
{
    m_markers.clear();
    QFile file(markersPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll())
                                 .object()[QStringLiteral("markers")].toArray();
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        QVariantMap m;
        m[QStringLiteral("type")]  = o[QStringLiteral("type")].toString();
        m[QStringLiteral("frame")] = o[QStringLiteral("frame")].toInt();
        m_markers.append(m);
    }
}

void MatchManager::saveMarkers()
{
    QJsonArray array;
    for (const QVariant &v : m_markers) {
        const QVariantMap m = v.toMap();
        QJsonObject o;
        o[QStringLiteral("type")]  = m.value(QStringLiteral("type")).toString();
        o[QStringLiteral("frame")] = m.value(QStringLiteral("frame")).toInt();
        array.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("markers")] = array;
    root[QStringLiteral("fps")] = m_fps;
    QFile file(markersPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void MatchManager::addMarker(const QString &type, int frame)
{
    if (!registered() || m_videoId <= 0)
        return;
    QVariantMap m;
    m[QStringLiteral("type")]  = type;
    m[QStringLiteral("frame")] = frame;
    m_markers.append(m);
    std::sort(m_markers.begin(), m_markers.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("frame")).toInt()
             < b.toMap().value(QStringLiteral("frame")).toInt();
    });
    saveMarkers();
    emit markersChanged();
}

void MatchManager::removeMarker(int index)
{
    if (index < 0 || index >= m_markers.size())
        return;
    m_markers.removeAt(index);
    saveMarkers();
    emit markersChanged();
}

int MatchManager::firstMarkerFrame(const QString &type) const
{
    for (const QVariant &v : m_markers) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == type)
            return m.value(QStringLiteral("frame")).toInt();
    }
    return -1;
}

int MatchManager::matchStartFrame() const
{
    // First play-period start of any category (match, halves, extra
    // times, penalties). m_markers is sorted by frame.
    for (const QVariant &v : m_markers) {
        const QVariantMap m = v.toMap();
        if (isPeriodStart(m.value(QStringLiteral("type")).toString()))
            return m.value(QStringLiteral("frame")).toInt();
    }
    return -1;
}

int MatchManager::matchEndFrame() const
{
    for (int i = m_markers.size() - 1; i >= 0; --i) {
        const QVariantMap m = m_markers.at(i).toMap();
        if (isPeriodEnd(m.value(QStringLiteral("type")).toString()))
            return m.value(QStringLiteral("frame")).toInt();
    }
    return -1;
}

bool MatchManager::hasLineupMarkers() const
{
    static const QStringList types = {
        QStringLiteral("lineup_a"), QStringLiteral("lineup_b"),
        QStringLiteral("bench_a"), QStringLiteral("bench_b"),
    };
    for (const QString &t : types) {
        if (firstMarkerFrame(t) >= 0)
            return true;
    }
    return false;
}

std::vector<std::pair<double, double>> MatchManager::excludedRangesSec() const
{
    std::vector<std::pair<double, double>> ranges = commercialRangesSec();

    // Play windows from every "*_start"/"*_end" period pair (match, halves,
    // extra times, penalties): everything outside the union of play windows
    // is excluded — including gaps like half-time between "1T fin" and
    // "2T inicio". Without period markers, the whole video is play.
    std::vector<std::pair<double, double>> play;
    double open = -1.0;
    for (const QVariant &v : m_markers) {   // sorted by frame
        const QVariantMap m = v.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        const double sec = m.value(QStringLiteral("frame")).toInt() / m_fps;
        if (isPeriodStart(type)) {
            if (open < 0.0)
                open = sec;
        } else if (isPeriodEnd(type)) {
            if (open >= 0.0) {
                play.emplace_back(open, sec);
                open = -1.0;
            } else {
                play.emplace_back(0.0, sec);   // end without start: play from 0
            }
        }
    }
    if (open >= 0.0)
        play.emplace_back(open, 1e12);   // start without end: play to the end

    if (!play.empty()) {
        double cursor = 0.0;
        for (const auto &p : play) {
            if (p.first > cursor)
                ranges.emplace_back(cursor, p.first);
            cursor = std::max(cursor, p.second);
        }
        if (cursor < 1e11)
            ranges.emplace_back(cursor, 1e12);
    }
    return ranges;
}

QVector<QPair<int, int>> MatchManager::excludedFrameRanges() const
{
    QVector<QPair<int, int>> ranges;
    for (const auto &[startSec, endSec] : excludedRangesSec()) {
        const int a = static_cast<int>(startSec * m_fps);
        const int b = endSec >= 1e11 ? INT_MAX
                                     : static_cast<int>(endSec * m_fps);
        ranges.append({a, b});
    }
    return ranges;
}

std::vector<std::pair<double, double>> MatchManager::commercialRangesSec() const
{
    // Pair commercial_start/commercial_end markers in frame order; an
    // unclosed start runs to the end of the video.
    std::vector<std::pair<double, double>> ranges;
    double pendingStart = -1.0;
    for (const QVariant &v : m_markers) {
        const QVariantMap m = v.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        const double sec = m.value(QStringLiteral("frame")).toInt() / m_fps;
        if (type == QLatin1String("commercial_start")) {
            if (pendingStart < 0.0)
                pendingStart = sec;
        } else if (type == QLatin1String("commercial_end")) {
            if (pendingStart >= 0.0) {
                ranges.emplace_back(pendingStart, sec);
                pendingStart = -1.0;
            }
        }
    }
    if (pendingStart >= 0.0)
        ranges.emplace_back(pendingStart, m_totalFrames / m_fps);
    return ranges;
}

void MatchManager::startOp(int op, int onlyChunk)
{
    if (!registered() || m_videoId <= 0 || m_opRunning
            || m_worker->isRunning() || m_lineup->isRunning())
        return;
    m_worker->configure(static_cast<VideoOpsWorker::Op>(op),
                        m_videoPath, preprocessedFile(), chunksDir(), m_crop,
                        excludedRangesSec(), onlyChunk);
    m_opRunning = true;
    m_opProgress = 0.0;
    m_opLabel = QStringLiteral("Starting…");
    m_lastError.clear();
    emit opStateChanged();
    m_worker->start();
}

void MatchManager::preprocess()   { startOp(VideoOpsWorker::Preprocess); }
void MatchManager::createChunks() { startOp(VideoOpsWorker::Chunk); }
void MatchManager::trackChunks()  { startOp(VideoOpsWorker::Track); }

void MatchManager::trackChunk(int number)
{
    if (number > 0)
        startOp(VideoOpsWorker::Track, number);
}

QVariantList MatchManager::chunksList() const
{
    QVariantList list;
    if (m_matchDir.isEmpty() || m_videoId <= 0)
        return list;
    const QString dir = chunksDir();

    // Prefer the chunks.json index; fall back to scanning the files.
    QFile idx(dir + QStringLiteral("/chunks.json"));
    if (idx.open(QIODevice::ReadOnly)) {
        const QJsonArray chunks = QJsonDocument::fromJson(idx.readAll())
                                      .object()[QStringLiteral("chunks")].toArray();
        for (const QJsonValue &v : chunks) {
            const QJsonObject o = v.toObject();
            QVariantMap m = o.toVariantMap();
            const int number = o[QStringLiteral("number")].toInt();
            m[QStringLiteral("hasCsv")] = QFile::exists(
                dir + QStringLiteral("/video_part_%1.csv")
                          .arg(number, 3, 10, QLatin1Char('0')));
            list.append(m);
        }
        return list;
    }

    const QStringList files = QDir(dir).entryList(
        {QStringLiteral("video_part_*.mp4")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        const int number = f.mid(11, 3).toInt();
        QVariantMap m;
        m[QStringLiteral("number")] = number;
        m[QStringLiteral("file")] = f;
        m[QStringLiteral("start_sec")] = (number - 1) * 60.0;
        m[QStringLiteral("end_sec")] = number * 60.0;
        m[QStringLiteral("frames")] = 600;
        m[QStringLiteral("hasCsv")] = QFile::exists(
            dir + QStringLiteral("/video_part_%1.csv")
                      .arg(number, 3, 10, QLatin1Char('0')));
        list.append(m);
    }
    return list;
}

QString MatchManager::syncPointsPathFor(int videoId) const
{
    return m_matchDir + QStringLiteral("/sync_points_%1.json")
                            .arg(videoId, 2, 10, QLatin1Char('0'));
}

QVariantList MatchManager::syncPoints(int videoId) const
{
    QVariantList list;
    if (m_matchDir.isEmpty() || videoId <= 0)
        return list;
    QFile file(syncPointsPathFor(videoId));
    if (!file.open(QIODevice::ReadOnly))
        return list;
    const QJsonArray points = QJsonDocument::fromJson(file.readAll())
                                  .object()[QStringLiteral("points")].toArray();
    for (const QJsonValue &v : points)
        list.append(v.toObject().toVariantMap());
    return list;
}

void MatchManager::setSyncPoint(int videoId, const QString &period,
                                int minute, int frame)
{
    if (m_matchDir.isEmpty() || videoId <= 0)
        return;
    QVariantList points = syncPoints(videoId);
    // Upsert this (period, minute), and keep frames unique: a given video
    // frame marks exactly one minute, so drop any other slot that already
    // pointed at this frame (otherwise two minutes could share a frame).
    for (int i = points.size() - 1; i >= 0; --i) {
        const QVariantMap p = points.at(i).toMap();
        const bool sameSlot = p.value(QStringLiteral("period")).toString() == period
                              && p.value(QStringLiteral("minute")).toInt() == minute;
        const bool sameFrame = p.value(QStringLiteral("frame")).toInt() == frame;
        if (sameSlot || sameFrame)
            points.removeAt(i);
    }
    QVariantMap p;
    p[QStringLiteral("period")] = period;
    p[QStringLiteral("minute")] = minute;
    p[QStringLiteral("frame")] = frame;
    points.append(p);

    QJsonArray array;
    for (const QVariant &v : points)
        array.append(QJsonObject::fromVariantMap(v.toMap()));
    QJsonObject root;
    root[QStringLiteral("video")] = videoId;
    root[QStringLiteral("points")] = array;
    QFile file(syncPointsPathFor(videoId));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();   // flush before notifying: listeners re-read this file
    }
    emit syncPointsChanged();
}

void MatchManager::removeSyncPoint(int videoId, const QString &period, int minute)
{
    QVariantList points = syncPoints(videoId);
    bool changed = false;
    for (int i = points.size() - 1; i >= 0; --i) {
        const QVariantMap p = points.at(i).toMap();
        if (p.value(QStringLiteral("period")).toString() == period
                && p.value(QStringLiteral("minute")).toInt() == minute) {
            points.removeAt(i);
            changed = true;
        }
    }
    if (!changed)
        return;
    QJsonArray array;
    for (const QVariant &v : points)
        array.append(QJsonObject::fromVariantMap(v.toMap()));
    QJsonObject root;
    root[QStringLiteral("video")] = videoId;
    root[QStringLiteral("points")] = array;
    QFile file(syncPointsPathFor(videoId));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();   // flush before notifying: listeners re-read this file
    }
    emit syncPointsChanged();
}

void MatchManager::extractLineups()
{
    if (!registered() || m_videoId <= 0 || m_opRunning
            || m_worker->isRunning() || m_lineup->isRunning())
        return;

    QVector<LineupExtractor::Job> jobs;
    for (const QVariant &v : m_markers) {
        const QVariantMap m = v.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        const int frame = m.value(QStringLiteral("frame")).toInt();
        if (type == QLatin1String("lineup_a") || type == QLatin1String("bench_a"))
            jobs.append({type, frame, 0});
        else if (type == QLatin1String("lineup_b") || type == QLatin1String("bench_b"))
            jobs.append({type, frame, 1});
    }
    if (jobs.isEmpty()) {
        m_lastError = QStringLiteral("no lineup/bench markers set");
        emit opStateChanged();
        return;
    }

    m_lineup->configure(m_videoPath, lineupsDir(), jobs, m_crop);
    m_opRunning = true;
    m_opProgress = 0.0;
    m_opLabel = QStringLiteral("Extracting lineups…");
    m_lastError.clear();
    emit opStateChanged();
    m_lineup->start();
}

void MatchManager::onLineupsFinished(bool ok, const QString &error, const QVariantMap &result)
{
    m_opRunning = false;
    if (!ok) {
        m_lastError = error;
        emit opStateChanged();
        return;
    }

    const QVariantList teamA = result.value(QStringLiteral("teamA")).toList();
    const QVariantList teamB = result.value(QStringLiteral("teamB")).toList();

    QJsonObject root;
    QJsonArray a, b;
    for (const QVariant &v : teamA) a.append(QJsonObject::fromVariantMap(v.toMap()));
    for (const QVariant &v : teamB) b.append(QJsonObject::fromVariantMap(v.toMap()));
    root[QStringLiteral("teamA")] = a;
    root[QStringLiteral("teamB")] = b;
    root[QStringLiteral("teamNameA")] = result.value(QStringLiteral("teamNameA")).toString();
    root[QStringLiteral("teamNameB")] = result.value(QStringLiteral("teamNameB")).toString();
    QFile file(lineupsJsonPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    m_lineupsExtracted = true;

    m_opLabel = QStringLiteral("Lineups: %1 + %2 players").arg(teamA.size()).arg(teamB.size());
    emit matchChanged();
    emit opStateChanged();
    emit lineupsReady(result);
}

QVariantMap MatchManager::loadLineups() const
{
    QFile file(lineupsJsonPath());
    if (m_matchDir.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    QVariantMap result;
    result[QStringLiteral("teamA")] = o[QStringLiteral("teamA")].toArray().toVariantList();
    result[QStringLiteral("teamB")] = o[QStringLiteral("teamB")].toArray().toVariantList();
    result[QStringLiteral("teamNameA")] = o[QStringLiteral("teamNameA")].toString();
    result[QStringLiteral("teamNameB")] = o[QStringLiteral("teamNameB")].toString();
    return result;
}

QDateTime MatchManager::lineupsModified() const
{
    return QFileInfo(lineupsJsonPath()).lastModified();
}

void MatchManager::cancelOp()
{
    m_worker->requestStop();
    m_lineup->requestStop();
}

void MatchManager::onOpProgress(double fraction, const QString &label)
{
    m_opProgress = fraction;
    m_opLabel = label;
    emit opStateChanged();
}

void MatchManager::onOpFinished(int op, bool ok, const QString &error, const QVariantMap &result)
{
    m_opRunning = false;
    if (!ok) {
        m_lastError = error;
        emit opStateChanged();
        return;
    }

    switch (op) {
    case VideoOpsWorker::Preprocess:
        m_preprocessedPath = matchDirName() + QStringLiteral("/preprocessed_20fps")
                             + videoSuffix() + QStringLiteral(".mp4");
        m_status = QStringLiteral("preprocessed");
        break;
    case VideoOpsWorker::Chunk:
        m_chunkCount = result.value(QStringLiteral("chunks")).toInt();
        m_status = QStringLiteral("chunked");
        break;
    case VideoOpsWorker::Track:
        m_status = QStringLiteral("tracked");
        break;
    }
    updateGamesEntry();
    emit matchChanged();
    emit opStateChanged();
}
