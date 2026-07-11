#include "AppController.h"

#include "FrameProvider.h"
#include "HomographyManager.h"
#include "MatchMetadata.h"
#include "RosterModel.h"
#include "TagsModel.h"
#include "TracksModel.h"
#include "TrackingManager.h"
#include "MatchManager.h"
#include "VideoEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace {

QVariantMap tagToMap(const TagEvent &t)
{
    QVariantMap m;
    m[QStringLiteral("id")]           = t.id;
    m[QStringLiteral("frame")]        = t.frame;
    m[QStringLiteral("timecode")]     = t.timecode;
    m[QStringLiteral("playerNumber")] = t.playerNumber;
    m[QStringLiteral("playerName")]   = t.playerName;
    m[QStringLiteral("team")]         = t.team;
    m[QStringLiteral("x")]            = t.x;
    m[QStringLiteral("y")]            = t.y;
    m[QStringLiteral("pitchX")]       = t.pitchX;
    m[QStringLiteral("pitchY")]       = t.pitchY;
    m[QStringLiteral("hasPitch")]     = t.hasPitch;
    return m;
}

TagEvent tagFromMap(const QVariantMap &m)
{
    TagEvent t;
    t.id           = m.value(QStringLiteral("id")).toInt();
    t.frame        = m.value(QStringLiteral("frame")).toInt();
    t.timecode     = m.value(QStringLiteral("timecode")).toString();
    t.playerNumber = m.value(QStringLiteral("playerNumber")).toInt();
    t.playerName   = m.value(QStringLiteral("playerName")).toString();
    t.team         = m.value(QStringLiteral("team")).toInt();
    t.x            = m.value(QStringLiteral("x")).toDouble();
    t.y            = m.value(QStringLiteral("y")).toDouble();
    t.pitchX       = m.value(QStringLiteral("pitchX")).toDouble();
    t.pitchY       = m.value(QStringLiteral("pitchY")).toDouble();
    t.hasPitch     = m.value(QStringLiteral("hasPitch")).toBool();
    return t;
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    m_engine        = new VideoEngine(this);
    m_frameProvider = new FrameProvider();
    m_engine2        = new VideoEngine(this);
    m_frameProvider2 = new FrameProvider();
    m_homeRoster    = new RosterModel(this);
    m_awayRoster    = new RosterModel(this);
    m_metadata      = new MatchMetadata(this);
    m_tags          = new TagsModel(this);
    m_homography    = new HomographyManager(this);
    m_tracking      = new TrackingManager(this);
    m_tracksModel   = new TracksModel(this);
    m_match         = new MatchManager(this);

    // Seed rosters so the app is usable before any project exists.
    m_homeRoster->setPlayers({
        {1,  QStringLiteral("A. Peña"),     QStringLiteral("GK")},
        {4,  QStringLiteral("M. Alvarado"), QStringLiteral("CB")},
        {5,  QStringLiteral("R. Solano"),   QStringLiteral("CDM")},
        {7,  QStringLiteral("D. Restrepo"), QStringLiteral("RW")},
        {9,  QStringLiteral("J. Ferreira"), QStringLiteral("ST")},
        {11, QStringLiteral("C. Ibarra"),   QStringLiteral("LW")},
    });
    m_awayRoster->setPlayers({
        {12, QStringLiteral("V. Duarte"),  QStringLiteral("GK")},
        {3,  QStringLiteral("F. Quiroga"), QStringLiteral("LB")},
        {6,  QStringLiteral("E. Salazar"), QStringLiteral("CB")},
        {8,  QStringLiteral("N. Basile"),  QStringLiteral("CM")},
        {10, QStringLiteral("L. Mercado"), QStringLiteral("CAM")},
        {22, QStringLiteral("H. Cortez"),  QStringLiteral("ST")},
    });

    connect(m_engine, &VideoEngine::frameReady, this, &AppController::onFrameReady,
            Qt::QueuedConnection);
    connect(m_engine, &VideoEngine::videoInfo, this, &AppController::onVideoInfo,
            Qt::QueuedConnection);
    connect(m_engine, &VideoEngine::endReached, this, [this]() {
        if (m_playing) {
            m_playing = false;
            emit playingChanged();
        }
    }, Qt::QueuedConnection);
    connect(m_engine, &VideoEngine::error, this, [this](const QString &msg) {
        m_lastError = msg;
        emit errorChanged();
    }, Qt::QueuedConnection);
    // Secondary (camera-sync) player.
    connect(m_engine2, &VideoEngine::frameReady, this,
            [this](const QImage &frame, int frameIndex, double posSec) {
        m_frameProvider2->setImage(frame);
        ++m_secFrameSerial;
        m_secCurrentFrame = frameIndex;
        m_secPositionSec = posSec;
        emit secFrameSerialChanged();
        emit secPositionChanged();
    }, Qt::QueuedConnection);
    connect(m_engine2, &VideoEngine::videoInfo, this,
            [this](int, int, int totalFrames, double fps) {
        m_secTotalFrames = totalFrames;
        m_secFps = fps;
        m_secLoaded = true;
        emit secStateChanged();
    }, Qt::QueuedConnection);
    connect(m_engine2, &VideoEngine::endReached, this, [this]() {
        if (m_secPlaying) {
            m_secPlaying = false;
            emit secPlayingChanged();
        }
    }, Qt::QueuedConnection);

    connect(m_tracking, &TrackingManager::tracksUpdated,
            m_tracksModel, &TracksModel::setRows);

    // Keep the interactive tracker in sync with the match markers: frames
    // before match start, after match end and inside commercials are
    // excluded from both tracking paths.
    auto pushExclusions = [this]() {
        m_tracking->setExclusions(m_match->excludedFrameRanges());
    };
    connect(m_match, &MatchManager::markersChanged, this, pushExclusions);
    connect(m_match, &MatchManager::matchChanged, this, pushExclusions);

    // Show persisted chunk-tracking results in the Tracking tab, both when
    // a tracked video is opened and right after a Track chunks op finishes.
    auto refreshFromChunks = [this]() {
        const QRect crop = m_match->crop();
        m_tracking->setDetectionOffset(m_match->hasCrop() ? crop.topLeft() : QPoint());
        if (m_match->status() == QLatin1String("tracked")) {
            m_tracking->loadFromChunkCsvs(
                m_match->chunksDir(), m_match->chunksMetadataDir(),
                m_match->assignmentsPath(),
                m_durationSec, m_match->excludedRangesSec());
        }
    };
    connect(m_match, &MatchManager::matchChanged, this, refreshFromChunks);

    // OCR'd lineups replace the corresponding roster and team names.
    connect(m_match, &MatchManager::lineupsReady,
            this, &AppController::applyLineups);

    // Anything the user edits makes the project dirty.
    connect(m_homeRoster, &RosterModel::edited, this, &AppController::markDirty);
    connect(m_awayRoster, &RosterModel::edited, this, &AppController::markDirty);
    connect(m_metadata, &MatchMetadata::changed, this, &AppController::markDirty);
    connect(m_tags, &TagsModel::edited, this, &AppController::markDirty);
    connect(m_homography, &HomographyManager::edited, this, &AppController::markDirty);
}

AppController::~AppController()
{
    m_tracking->stopInference();
    m_engine->stopProcessing();
    m_engine2->stopProcessing();
}

QObject *AppController::metadataObj() const   { return m_metadata; }
QObject *AppController::homeRosterObj() const { return m_homeRoster; }
QObject *AppController::awayRosterObj() const { return m_awayRoster; }
QObject *AppController::tagsObj() const       { return m_tags; }
QObject *AppController::homographyObj() const { return m_homography; }
QObject *AppController::trackingObj() const   { return m_tracking; }
QObject *AppController::tracksModelObj() const { return m_tracksModel; }
QObject *AppController::matchObj() const      { return m_match; }

void AppController::openVideo(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty())
        return;

    m_engine->stopProcessing();
    m_tracking->stopInference();
    closeSecondary();   // it belongs to the previous project context

    m_videoPath = path;
    m_videoName = QFileInfo(path).fileName();
    m_playing = false;

    // Undo history belongs to the previous video's tagging session.
    m_undoStack.clear();
    m_redoStack.clear();
    emit undoChanged();

    m_engine->setSource(path);
    m_engine->setPaused(true);   // show the first frame, then hold
    m_engine->start();

    m_tracking->setSource(path);
    loadProjectIfPresent();

    emit playingChanged();
}

void AppController::openVideoFile(const QString &path)
{
    openVideo(QUrl::fromLocalFile(path));
}

void AppController::openProjectVideo(int matchId, int videoId, const QString &path)
{
    m_match->prepareOpenVideo(matchId, videoId);
    openVideo(QUrl::fromLocalFile(path));
}

void AppController::openSecondary(int videoId, const QString &path)
{
    m_engine2->stopProcessing();
    m_secVideoId = videoId;
    m_secVideoName = QFileInfo(path).fileName();
    m_secLoaded = false;
    m_secPlaying = false;
    m_secCurrentFrame = 0;
    m_secPositionSec = 0.0;
    m_engine2->setSource(path);
    m_engine2->setPaused(true);   // show the first frame, then hold
    m_engine2->start();
    emit secStateChanged();
    emit secPlayingChanged();
    emit secPositionChanged();
}

void AppController::closeSecondary()
{
    m_engine2->stopProcessing();
    m_secLoaded = false;
    m_secPlaying = false;
    m_secVideoId = 0;
    m_secVideoName.clear();
    emit secStateChanged();
    emit secPlayingChanged();
}

void AppController::toggleSecPlay()
{
    if (!m_secLoaded)
        return;
    if (m_secPlaying) {
        m_engine2->setPaused(true);
        m_secPlaying = false;
    } else {
        if (m_secTotalFrames > 0 && m_secCurrentFrame >= m_secTotalFrames - 1)
            m_engine2->seekToFrame(0);
        m_engine2->setPaused(false);
        m_secPlaying = true;
    }
    emit secPlayingChanged();
}

void AppController::seekSecFrac(double frac)
{
    if (!m_secLoaded || m_secTotalFrames <= 0)
        return;
    frac = std::clamp(frac, 0.0, 1.0);
    m_engine2->seekToFrame(static_cast<int>(frac * (m_secTotalFrames - 1)));
}

void AppController::seekSecFrame(int frame)
{
    if (m_secLoaded)
        m_engine2->seekToFrame(frame);
}

void AppController::addVideoToProject(const QUrl &url, const QString &role,
                                      const QString &segment)
{
    m_match->prepareAddVideo(role, segment);
    openVideo(url);
}

void AppController::setVideoCrop(double x1, double y1, double x2, double y2)
{
    const int x = static_cast<int>(std::min(x1, x2));
    const int y = static_cast<int>(std::min(y1, y2));
    const int w = static_cast<int>(std::abs(x2 - x1));
    const int h = static_cast<int>(std::abs(y2 - y1));
    if (w < 16 || h < 16)
        return;   // degenerate selection
    m_match->setCrop(x, y, w, h);
}

void AppController::clearVideoCrop()
{
    m_match->clearCrop();
}

void AppController::togglePlay()
{
    if (!m_videoLoaded)
        return;
    if (m_playing) {
        m_engine->setPaused(true);
        m_playing = false;
    } else {
        if (m_totalFrames > 0 && m_currentFrame >= m_totalFrames - 1) {
            m_engine->seekToFrame(0);
        }
        m_engine->setPaused(false);
        m_playing = true;
    }
    emit playingChanged();
}

void AppController::pause()
{
    if (m_playing) {
        m_engine->setPaused(true);
        m_playing = false;
        emit playingChanged();
    }
}

void AppController::seekFrac(double frac)
{
    if (!m_videoLoaded || m_totalFrames <= 0)
        return;
    frac = std::clamp(frac, 0.0, 1.0);
    m_engine->seekToFrame(static_cast<int>(frac * (m_totalFrames - 1)));
}

void AppController::seekFrame(int frame)
{
    if (m_videoLoaded)
        m_engine->seekToFrame(frame);
}

void AppController::stepFrames(int delta)
{
    if (m_videoLoaded)
        m_engine->stepFrames(delta);
}

void AppController::seekRelative(double seconds)
{
    if (!m_videoLoaded)
        return;
    int target = m_currentFrame
                 + static_cast<int>(seconds * (m_fps > 0.0 ? m_fps : 25.0));
    if (target < 0) target = 0;
    if (m_totalFrames > 0 && target >= m_totalFrames) target = m_totalFrames - 1;
    m_engine->seekToFrame(target);
}

void AppController::setPlaybackFps(double fps)
{
    if (fps < 0.0) fps = 0.0;
    if (qFuzzyCompare(m_playbackFps + 1.0, fps + 1.0))
        return;
    m_playbackFps = fps;
    m_engine->setPlaybackFps(fps);
    emit playbackFpsChanged();
}

QString AppController::timecode(double sec) const
{
    if (sec < 0.0) sec = 0.0;
    const int h = static_cast<int>(sec / 3600.0);
    const int m = static_cast<int>(sec / 60.0) % 60;
    const int s = static_cast<int>(sec) % 60;
    const int f = static_cast<int>(std::fmod(sec, 1.0) * (m_fps > 0.0 ? m_fps : 25.0));
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

void AppController::addTag(double vx, double vy, int team, int rosterRow)
{
    if (!m_videoLoaded)
        return;
    RosterModel *roster = (team == 0) ? m_homeRoster : m_awayRoster;
    const QVariantMap player = roster->get(rosterRow);
    if (player.isEmpty())
        return;

    TagEvent tag;
    tag.frame        = m_currentFrame;
    tag.timecode     = timecode(m_positionSec);
    tag.playerNumber = player.value(QStringLiteral("number")).toInt();
    tag.playerName   = player.value(QStringLiteral("name")).toString();
    tag.team         = team;
    tag.x            = vx;
    tag.y            = vy;
    if (m_homography->verified()) {
        const QPointF p = m_homography->imageToPitch(vx, vy);
        tag.pitchX = p.x();
        tag.pitchY = p.y();
        tag.hasPitch = true;
    }
    tag.id = m_tags->addTag(tag);

    QVariantMap cmd;
    cmd[QStringLiteral("op")] = QStringLiteral("addTag");
    cmd[QStringLiteral("tag")] = tagToMap(tag);
    pushCommand(cmd);
}

void AppController::removeTag(int row)
{
    if (row < 0 || row >= m_tags->tags().size())
        return;
    const TagEvent tag = m_tags->tags().at(row);
    m_tags->removeTag(row);

    QVariantMap cmd;
    cmd[QStringLiteral("op")] = QStringLiteral("removeTag");
    cmd[QStringLiteral("row")] = row;
    cmd[QStringLiteral("tag")] = tagToMap(tag);
    pushCommand(cmd);
}

void AppController::assignTrack(const QString &key, int number,
                                const QString &name, int team)
{
    const QVariantMap prev = m_tracking->assignmentFor(key);
    m_tracking->assignTrack(key, number, name, team);

    QVariantMap next;
    next[QStringLiteral("number")] = number;
    next[QStringLiteral("name")] = name;
    next[QStringLiteral("team")] = team;

    QVariantMap cmd;
    cmd[QStringLiteral("op")] = QStringLiteral("assign");
    cmd[QStringLiteral("key")] = key;
    cmd[QStringLiteral("prev")] = prev;
    cmd[QStringLiteral("next")] = next;
    pushCommand(cmd);
}

void AppController::clearTrackAssignment(const QString &key)
{
    const QVariantMap prev = m_tracking->assignmentFor(key);
    if (prev.isEmpty())
        return;
    m_tracking->clearAssignment(key);

    QVariantMap cmd;
    cmd[QStringLiteral("op")] = QStringLiteral("clearAssign");
    cmd[QStringLiteral("key")] = key;
    cmd[QStringLiteral("prev")] = prev;
    pushCommand(cmd);
}

void AppController::pushCommand(const QVariantMap &cmd)
{
    m_undoStack.append(cmd);
    while (m_undoStack.size() > 100)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    emit undoChanged();
}

void AppController::applyCommand(const QVariantMap &cmd, bool isUndo)
{
    const QString op = cmd.value(QStringLiteral("op")).toString();
    const QString key = cmd.value(QStringLiteral("key")).toString();
    const QVariantMap prev = cmd.value(QStringLiteral("prev")).toMap();
    const QVariantMap next = cmd.value(QStringLiteral("next")).toMap();

    auto assignFrom = [this, &key](const QVariantMap &info) {
        if (info.isEmpty()) {
            m_tracking->clearAssignment(key);
        } else {
            m_tracking->assignTrack(key,
                                    info.value(QStringLiteral("number")).toInt(),
                                    info.value(QStringLiteral("name")).toString(),
                                    info.value(QStringLiteral("team")).toInt());
        }
    };

    if (op == QLatin1String("addTag")) {
        const TagEvent tag = tagFromMap(cmd.value(QStringLiteral("tag")).toMap());
        if (isUndo)
            m_tags->removeById(tag.id);
        else
            m_tags->insertTag(0, tag);
    } else if (op == QLatin1String("removeTag")) {
        const TagEvent tag = tagFromMap(cmd.value(QStringLiteral("tag")).toMap());
        if (isUndo)
            m_tags->insertTag(cmd.value(QStringLiteral("row")).toInt(), tag);
        else
            m_tags->removeById(tag.id);
    } else if (op == QLatin1String("assign")) {
        assignFrom(isUndo ? prev : next);
    } else if (op == QLatin1String("clearAssign")) {
        if (isUndo)
            assignFrom(prev);
        else
            m_tracking->clearAssignment(key);
    }
}

void AppController::undo()
{
    if (m_undoStack.isEmpty())
        return;
    const QVariantMap cmd = m_undoStack.takeLast();
    applyCommand(cmd, true);
    m_redoStack.append(cmd);
    emit undoChanged();
}

void AppController::redo()
{
    if (m_redoStack.isEmpty())
        return;
    const QVariantMap cmd = m_redoStack.takeLast();
    applyCommand(cmd, false);
    m_undoStack.append(cmd);
    emit undoChanged();
}

void AppController::onFrameReady(const QImage &frame, int frameIndex, double posSec)
{
    m_frameProvider->setImage(frame);
    ++m_frameSerial;
    m_currentFrame = frameIndex;
    m_positionSec = posSec;
    emit frameSerialChanged();
    emit positionChanged();
}

void AppController::onVideoInfo(int width, int height, int totalFrames, double fps)
{
    m_videoWidth  = width;
    m_videoHeight = height;
    m_totalFrames = totalFrames;
    m_fps         = fps;
    m_durationSec = fps > 0.0 ? totalFrames / fps : 0.0;
    m_videoLoaded = true;
    m_homography->setImageSize(width, height);
    m_match->setVideo(m_videoPath, fps, totalFrames);

    // Reflect a previously extracted lineup (e.g. from the headless CLI)
    // unless the saved project is newer — manual roster edits win.
    const QVariantMap lineups = m_match->loadLineups();
    if (!lineups.isEmpty()) {
        const QFileInfo project(QDir(projectDir()).filePath(QStringLiteral("project.json")));
        if (!project.exists() || project.lastModified() < m_match->lineupsModified())
            applyLineups(lineups);
    }

    emit videoStateChanged();
}

void AppController::applyLineups(const QVariantMap &lineups)
{
    auto toPlayers = [](const QVariantList &list) {
        QVector<Player> players;
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            players.append({m.value(QStringLiteral("number")).toInt(),
                            m.value(QStringLiteral("name")).toString(),
                            QStringLiteral("—")});
        }
        return players;
    };
    const QVariantList teamA = lineups.value(QStringLiteral("teamA")).toList();
    const QVariantList teamB = lineups.value(QStringLiteral("teamB")).toList();
    const QString nameA = lineups.value(QStringLiteral("teamNameA")).toString();
    const QString nameB = lineups.value(QStringLiteral("teamNameB")).toString();

    if (!teamA.isEmpty()) m_homeRoster->setPlayers(toPlayers(teamA));
    if (!teamB.isEmpty()) m_awayRoster->setPlayers(toPlayers(teamB));
    if (!nameA.isEmpty()) m_metadata->setHomeTeam(nameA);
    if (!nameB.isEmpty()) m_metadata->setAwayTeam(nameB);
    if (!teamA.isEmpty() || !teamB.isEmpty())
        markDirty();
}

void AppController::markDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged();
    }
}

QString AppController::projectDir() const
{
    if (m_videoPath.isEmpty())
        return {};
    const QFileInfo info(m_videoPath);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral("_project"));
}

void AppController::loadProjectIfPresent()
{
    const QString dir = projectDir();
    if (dir.isEmpty())
        return;
    QFile file(QDir(dir).filePath(QStringLiteral("project.json")));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_metadata->fromJson(root[QStringLiteral("metadata")].toObject());
    m_homeRoster->fromJson(root[QStringLiteral("homeRoster")].toArray());
    m_awayRoster->fromJson(root[QStringLiteral("awayRoster")].toArray());
    m_tags->fromJson(root[QStringLiteral("tags")].toArray());
    m_homography->fromJson(root[QStringLiteral("homography")].toObject());
    m_dirty = false;
    emit dirtyChanged();
}

bool AppController::saveProject()
{
    const QString dirPath = projectDir();
    if (dirPath.isEmpty()) {
        m_lastError = QStringLiteral("Open a video before saving the project");
        emit errorChanged();
        return false;
    }
    QDir().mkpath(dirPath);
    const QDir dir(dirPath);

    QJsonObject root;
    root[QStringLiteral("video")]      = m_videoPath;
    root[QStringLiteral("metadata")]   = m_metadata->toJson();
    root[QStringLiteral("homeRoster")] = m_homeRoster->toJson();
    root[QStringLiteral("awayRoster")] = m_awayRoster->toJson();
    root[QStringLiteral("tags")]       = m_tags->toJson();
    root[QStringLiteral("homography")] = m_homography->toJson();

    QFile jsonFile(dir.filePath(QStringLiteral("project.json")));
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("Cannot write %1").arg(jsonFile.fileName());
        emit errorChanged();
        return false;
    }
    jsonFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    jsonFile.close();

    // Flat CSV exports for downstream analysis.
    QFile tagsCsv(dir.filePath(QStringLiteral("tags.csv")));
    if (tagsCsv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&tagsCsv);
        out << "frame,timecode,team,player_number,player_name,x,y,pitch_x,pitch_y\n";
        for (const TagEvent &t : m_tags->tags()) {
            out << t.frame << ',' << t.timecode << ','
                << (t.team == 0 ? "home" : "away") << ','
                << t.playerNumber << ",\"" << t.playerName << "\","
                << t.x << ',' << t.y << ',';
            if (t.hasPitch)
                out << t.pitchX << ',' << t.pitchY;
            else
                out << ',';
            out << '\n';
        }
    }

    QFile tracksCsv(dir.filePath(QStringLiteral("tracks.csv")));
    if (tracksCsv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&tracksCsv);
        out << "track_id,frames_tracked,avg_conf,status\n";
        for (const QVariant &v : m_tracksModel->rows()) {
            const QVariantMap row = v.toMap();
            out << row.value(QStringLiteral("trackId")).toString() << ','
                << row.value(QStringLiteral("framesTracked")).toInt() << ','
                << row.value(QStringLiteral("avgConf")).toString() << ','
                << row.value(QStringLiteral("status")).toString() << '\n';
        }
    }

    m_dirty = false;
    emit dirtyChanged();
    return true;
}
