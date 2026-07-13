#include "AppController.h"

#include "FrameProvider.h"
#include "HomographyManager.h"
#include "HomographyWorker.h"
#include "LineCalibrator.h"
#include "HomographySolver.h"
#include "ShotDetector.h"
#include "LineupPositionExtractor.h"
#include "MaskGenerator.h"
#include "MatchMetadata.h"
#include "RosterModel.h"
#include "TagsModel.h"
#include "TracksModel.h"
#include "TrackingManager.h"
#include "MatchManager.h"
#include "PlayerSamples.h"
#include "VideoEngine.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <cstring>

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

// QImage (any format) -> BGR cv::Mat (deep copy).
cv::Mat qimageToBgr(const QImage &img)
{
    if (img.isNull()) return {};
    const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    const cv::Mat view(rgb.height(), rgb.width(), CV_8UC3,
                       const_cast<uchar *>(rgb.bits()),
                       static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(view, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

// Turn an 8-bit 0/255 mask into a translucent tinted ARGB overlay that QML
// can paint straight over the video (masked pixels tinted, rest transparent).
QImage colorizeMask(const cv::Mat &mask, const QColor &tint, int alpha)
{
    if (mask.empty()) return {};
    QImage out(mask.cols, mask.rows, QImage::Format_ARGB32);
    const QRgb on = qRgba(tint.red(), tint.green(), tint.blue(), alpha);
    for (int y = 0; y < mask.rows; ++y) {
        const uchar *src = mask.ptr<uchar>(y);
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < mask.cols; ++x)
            dst[x] = src[x] ? on : 0u;
    }
    return out;
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
    m_maskProvider   = new FrameProvider();
    m_homeRoster    = new RosterModel(this);
    m_awayRoster    = new RosterModel(this);
    m_metadata      = new MatchMetadata(this);
    m_tags          = new TagsModel(this);
    m_homography    = new HomographyManager(this);
    m_tracking      = new TrackingManager(this);
    m_tracksModel   = new TracksModel(this);
    m_match         = new MatchManager(this);
    m_playerSamples = new PlayerSamples(this);

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
QObject *AppController::playerSamplesObj() const { return m_playerSamples; }

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
    // The project + player samples live under the match dir, which is only
    // resolved once the video info arrives (onVideoInfo -> m_match->setVideo),
    // so both are loaded there, not here.

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
    // Only map to pitch coordinates when the frame actually shows the pitch
    // (a non-pitch shot has no valid homography — phase F4).
    if (m_homography->verified() && m_pitchVisible) {
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
    m_lastFrame = frame;
    ++m_frameSerial;
    m_currentFrame = frameIndex;
    m_positionSec = posSec;
    // Keep the per-frame homography (and the working overlay points) in sync
    // with the displayed frame so the pitch overlay and tagging follow the
    // moving camera between calibration keyframes.
    m_homography->setCurrentFrame(frameIndex);
    updatePitchVisible();
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

    // Now that the match dir / video id are known, load the consolidated
    // project for this video (migrating a legacy <video>_project if present).
    m_playerSamples->setBaseDir(playerSamplesDir());
    loadProjectIfPresent();

    // Reflect a previously extracted lineup (e.g. from the headless CLI)
    // unless the saved project is newer — manual roster edits win.
    const QVariantMap lineups = m_match->loadLineups();
    if (!lineups.isEmpty()) {
        const QFileInfo project(projectJsonPath());
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

QString AppController::legacyProjectDir() const
{
    if (m_videoPath.isEmpty())
        return {};
    const QFileInfo info(m_videoPath);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral("_project"));
}

QString AppController::projectJsonPath() const
{
    const QString dir = m_match->matchDir();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/project.json");
}

QString AppController::videoKey() const
{
    return QString::number(m_match->videoId());
}

QString AppController::playerSamplesDir() const
{
    const QString dir = m_match->matchDir();
    return dir.isEmpty() ? QString()
                         : dir + QStringLiteral("/player_samples") + m_match->videoSuffix();
}

QString AppController::exportHomographiesPath() const
{
    const QString dir = m_match->matchDir();
    return dir.isEmpty() ? QString()
                         : dir + QStringLiteral("/homographies") + m_match->videoSuffix()
                               + QStringLiteral(".json");
}

QString AppController::greenMaskDir() const
{
    const QString dir = m_match->matchDir();
    return dir.isEmpty() ? QString()
                         : dir + QStringLiteral("/green_mask") + m_match->videoSuffix();
}

QString AppController::staticMaskDir() const
{
    const QString dir = m_match->matchDir();
    return dir.isEmpty() ? QString()
                         : dir + QStringLiteral("/static_mask") + m_match->videoSuffix();
}

QString AppController::lineupPositionsPath() const
{
    const QString dir = m_match->matchDir();
    return dir.isEmpty() ? QString()
                         : dir + QStringLiteral("/lineup_positions") + m_match->videoSuffix()
                               + QStringLiteral(".json");
}

void AppController::applyMatchJson(const QJsonObject &m)
{
    m_metadata->fromJson(m[QStringLiteral("metadata")].toObject());
    m_homeRoster->fromJson(m[QStringLiteral("homeRoster")].toArray());
    m_awayRoster->fromJson(m[QStringLiteral("awayRoster")].toArray());
}

void AppController::applyVideoJson(const QJsonObject &v)
{
    m_tags->fromJson(v[QStringLiteral("tags")].toArray());
    m_homography->fromJson(v[QStringLiteral("homography")].toObject());
}

void AppController::loadProjectIfPresent()
{
    // Bring a legacy <video>_project into the consolidated store first, so the
    // read below finds this video's section.
    migrateLegacyProjectIfNeeded();
    // Give pre-suffix mask dirs the current video's suffix, so old matches
    // keep showing their masks after the naming change.
    migrateMaskDirsIfNeeded();

    const QString jsonPath = projectJsonPath();
    if (!jsonPath.isEmpty()) {
        QFile file(jsonPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            applyMatchJson(root[QStringLiteral("match")].toObject());
            const QJsonObject videos = root[QStringLiteral("videos")].toObject();
            if (videos.contains(videoKey()))
                applyVideoJson(videos[videoKey()].toObject());
        }
    }

    // Attach a previously computed dense propagation track, if any.
    m_homography->clearPropagation();
    const QString dense = denseTrackPath();
    if (!dense.isEmpty() && QFile::exists(dense))
        m_homography->loadDenseTrack(dense);
    loadShotsIfPresent();

    // Previously extracted line-up positions, if any.
    m_lineupPositions.clear();
    m_lineupOcrLabel.clear();
    const QString lpPath = lineupPositionsPath();
    QFile lpFile(lpPath);
    if (!lpPath.isEmpty() && lpFile.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(lpFile.readAll()).object();
        m_lineupPositions[QStringLiteral("teamA")] =
            o[QStringLiteral("teamA")].toArray().toVariantList();
        m_lineupPositions[QStringLiteral("teamB")] =
            o[QStringLiteral("teamB")].toArray().toVariantList();
    }
    emit lineupPositionsChanged();

    m_dirty = false;
    emit dirtyChanged();
}

void AppController::migrateMaskDirsIfNeeded()
{
    const QString base = m_match->matchDir();
    const QString suffix = m_match->videoSuffix();
    if (base.isEmpty() || suffix.isEmpty())
        return;   // no suffix (unknown video id) → leave the flat dirs as-is

    // Pre-suffix layout had a single green_mask/ and static_mask/ per match.
    // Adopt them for the current video if it has no suffixed dir of its own.
    for (const char *kind : {"green_mask", "static_mask"}) {
        const QString flat = base + QLatin1Char('/') + QLatin1String(kind);
        const QString suffixed = flat + suffix;
        if (QDir(flat).exists() && !QDir(suffixed).exists())
            QDir().rename(flat, suffixed);
    }
}

void AppController::migrateLegacyProjectIfNeeded()
{
    const QString jsonPath = projectJsonPath();
    const QString ldir = legacyProjectDir();
    if (jsonPath.isEmpty() || ldir.isEmpty())
        return;

    // Skip if this video already lives in the consolidated store.
    QJsonObject root;
    {
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
        }
    }
    if (root[QStringLiteral("videos")].toObject().contains(videoKey()))
        return;

    // Nothing to migrate if there is no legacy project.json.
    QJsonObject legacy;
    {
        QFile f(QDir(ldir).filePath(QStringLiteral("project.json")));
        if (!f.open(QIODevice::ReadOnly))
            return;
        legacy = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }

    QDir().mkpath(m_match->matchDir());

    // Shared match-level data (only seed if not already present).
    QJsonObject matchObj = root[QStringLiteral("match")].toObject();
    if (!matchObj.contains(QStringLiteral("metadata")))
        matchObj[QStringLiteral("metadata")] = legacy[QStringLiteral("metadata")];
    if (!matchObj.contains(QStringLiteral("homeRoster")))
        matchObj[QStringLiteral("homeRoster")] = legacy[QStringLiteral("homeRoster")];
    if (!matchObj.contains(QStringLiteral("awayRoster")))
        matchObj[QStringLiteral("awayRoster")] = legacy[QStringLiteral("awayRoster")];
    root[QStringLiteral("match")] = matchObj;

    // Per-video section.
    QJsonObject videos = root[QStringLiteral("videos")].toObject();
    QJsonObject vid;
    vid[QStringLiteral("id")]         = m_match->videoId();
    vid[QStringLiteral("path")]       = m_videoPath;
    vid[QStringLiteral("role")]       = m_match->videoRole();
    vid[QStringLiteral("segment")]    = m_match->videoSegment();
    vid[QStringLiteral("tags")]       = legacy[QStringLiteral("tags")];
    vid[QStringLiteral("homography")] = legacy[QStringLiteral("homography")];
    videos[videoKey()] = vid;
    root[QStringLiteral("videos")] = videos;

    QFile out(jsonPath);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
    }

    // Copy per-video artifact files to their suffixed locations (non-destructive).
    auto copyTo = [](const QString &src, const QString &dst) {
        if (!dst.isEmpty() && QFile::exists(src) && !QFile::exists(dst))
            QFile::copy(src, dst);
    };
    copyTo(QDir(ldir).filePath(QStringLiteral("homography_dense.bin")), denseTrackPath());
    copyTo(QDir(ldir).filePath(QStringLiteral("shots.json")), shotsPath());

    // Player samples: json + the thumbnails folder.
    const QString psDst = playerSamplesDir();
    if (!psDst.isEmpty()) {
        QDir().mkpath(psDst + QStringLiteral("/player_samples"));
        copyTo(QDir(ldir).filePath(QStringLiteral("player_samples.json")),
               psDst + QStringLiteral("/player_samples.json"));
        const QDir thumbs(QDir(ldir).filePath(QStringLiteral("player_samples")));
        if (thumbs.exists())
            for (const QFileInfo &fi : thumbs.entryInfoList(QDir::Files))
                copyTo(fi.absoluteFilePath(),
                       psDst + QStringLiteral("/player_samples/") + fi.fileName());
    }

    // Set the legacy folder aside so it is not migrated again (recoverable).
    const QString migrated = ldir + QStringLiteral(".migrated");
    if (!QDir(migrated).exists())
        QDir().rename(ldir, migrated);
}

bool AppController::saveProject()
{
    const QString matchDir = m_match->matchDir();
    if (matchDir.isEmpty()) {
        m_lastError = QStringLiteral("Open a video before saving the project");
        emit errorChanged();
        return false;
    }
    QDir().mkpath(matchDir);
    const QDir dir(matchDir);
    const QString suffix = m_match->videoSuffix();

    // One project.json per match: merge into the existing file so the other
    // videos' sections (and any match-level data) are preserved.
    QJsonObject root;
    {
        QFile in(projectJsonPath());
        if (in.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(in.readAll()).object();
            in.close();
        }
    }

    // Shared match-level data.
    QJsonObject matchObj = root[QStringLiteral("match")].toObject();
    matchObj[QStringLiteral("metadata")]   = m_metadata->toJson();
    matchObj[QStringLiteral("homeRoster")] = m_homeRoster->toJson();
    matchObj[QStringLiteral("awayRoster")] = m_awayRoster->toJson();
    root[QStringLiteral("match")] = matchObj;

    // This video's section.
    QJsonObject videos = root[QStringLiteral("videos")].toObject();
    QJsonObject vid = videos[videoKey()].toObject();
    vid[QStringLiteral("id")]         = m_match->videoId();
    vid[QStringLiteral("path")]       = m_videoPath;
    vid[QStringLiteral("role")]       = m_match->videoRole();
    vid[QStringLiteral("segment")]    = m_match->videoSegment();
    vid[QStringLiteral("tags")]       = m_tags->toJson();
    vid[QStringLiteral("homography")] = m_homography->toJson();
    videos[videoKey()] = vid;
    root[QStringLiteral("videos")] = videos;

    QFile jsonFile(projectJsonPath());
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("Cannot write %1").arg(jsonFile.fileName());
        emit errorChanged();
        return false;
    }
    jsonFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    jsonFile.close();

    // Flat CSV exports for downstream analysis (per video).
    QFile tagsCsv(dir.filePath(QStringLiteral("tags") + suffix + QStringLiteral(".csv")));
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

    QFile tracksCsv(dir.filePath(QStringLiteral("tracks") + suffix + QStringLiteral(".csv")));
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

void AppController::deleteProject()
{
    if (!m_match->registered())
        return;

    // Stop any background work touching the files we are about to remove.
    m_tracking->stopInference();
    m_engine->stopProcessing();
    if (m_shotWorker && m_shotWorker->isRunning()) { m_shotWorker->requestStop(); m_shotWorker->wait(3000); }
    if (m_maskWorker && m_maskWorker->isRunning()) { m_maskWorker->requestStop(); m_maskWorker->wait(3000); }
    if (m_homoWorker && m_homoWorker->isRunning()) { m_homoWorker->requestStop(); m_homoWorker->wait(3000); }
    if (m_lineupPosWorker && m_lineupPosWorker->isRunning()) { m_lineupPosWorker->requestStop(); m_lineupPosWorker->wait(3000); }
    clearLineupPositions();
    closeSecondary();

    // The consolidated project + artifacts live in the match dir, removed by
    // m_match->deleteProject() below. Also clean up any legacy <video>_project
    // folder (and its .migrated backup) left over from before consolidation.
    const QString pdir = legacyProjectDir();
    if (!pdir.isEmpty()) {
        for (const QString &d : { pdir, pdir + QStringLiteral(".migrated") }) {
            QDir dd(d);
            if (dd.exists())
                dd.removeRecursively();
        }
    }

    // Remove the match artifacts + games.json entry and reset match state.
    m_match->deleteProject();

    // Return to the empty "open a video" state.
    m_videoLoaded = false;
    m_videoPath.clear();
    m_videoName.clear();
    m_playing = false;
    m_currentFrame = 0;
    m_positionSec = 0.0;
    m_dirty = false;
    m_lastFrame = QImage();
    m_shots.clear();
    m_pitchVisible = true;
    m_homography->clearPropagation();
    m_undoStack.clear();
    m_redoStack.clear();

    m_playerSamples->setBaseDir(QString());

    emit videoStateChanged();
    emit playingChanged();
    emit positionChanged();
    emit dirtyChanged();
    emit shotsChanged();
    emit pitchVisibleChanged();
    emit undoChanged();
}

void AppController::capturePlayerSample(int role, double vx, double vy,
                                        double vw, double vh)
{
    if (!m_videoLoaded || m_lastFrame.isNull()) {
        m_lastError = QStringLiteral("Abre un video antes de tomar muestras");
        emit errorChanged();
        return;
    }
    // Clamp the box to the frame.
    QRect r = QRectF(vx, vy, vw, vh).toRect().normalized();
    r &= QRect(0, 0, m_lastFrame.width(), m_lastFrame.height());
    if (r.width() < 4 || r.height() < 4) {
        m_lastError = QStringLiteral("La muestra es demasiado pequeña");
        emit errorChanged();
        return;
    }
    const QString dir = m_playerSamples->thumbDir();
    if (dir.isEmpty()) {
        m_lastError = QStringLiteral("El video no tiene carpeta de proyecto");
        emit errorChanged();
        return;
    }
    QDir().mkpath(dir);

    const QString key = m_playerSamples->roleKey(role);
    const QString fname = QStringLiteral("%1_%2.png")
                              .arg(key).arg(QDateTime::currentMSecsSinceEpoch());
    if (!m_lastFrame.copy(r).save(dir + QLatin1Char('/') + fname)) {
        m_lastError = QStringLiteral("No se pudo guardar la muestra");
        emit errorChanged();
        return;
    }
    m_playerSamples->add(role, m_currentFrame, QRectF(r),
                         QStringLiteral("player_samples/") + fname);
}

void AppController::captureFullFrameSample(int role)
{
    if (!m_videoLoaded || m_lastFrame.isNull()) {
        m_lastError = QStringLiteral("Abre un video antes de tomar muestras");
        emit errorChanged();
        return;
    }
    capturePlayerSample(role, 0, 0, m_lastFrame.width(), m_lastFrame.height());
}

void AppController::extractLineupPositions()
{
    if (m_lineupPosWorker && m_lineupPosWorker->isRunning())
        return;

    // Absolute paths of the captured line-up graphics, tagged by team.
    const QString base = m_playerSamples->baseDir();
    QVector<LineupPositionExtractor::Job> jobs;
    const int roles[2] = { PlayerSamples::TeamALineup, PlayerSamples::TeamBLineup };
    for (int t = 0; t < 2; ++t) {
        const QVariantList mine = m_playerSamples->forRole(roles[t]);
        for (const QVariant &v : mine) {
            const QString thumb = v.toMap().value(QStringLiteral("thumb")).toString();
            if (!thumb.isEmpty() && !base.isEmpty())
                jobs.append({ base + QLatin1Char('/') + thumb, t });
        }
    }
    if (jobs.isEmpty()) {
        m_lastError = QStringLiteral("Captura una imagen de alineación primero "
                                     "(sección Team line-ups)");
        emit errorChanged();
        return;
    }

    if (!m_lineupPosWorker) {
        m_lineupPosWorker = new LineupPositionExtractor(this);
        connect(m_lineupPosWorker, &LineupPositionExtractor::progressChanged, this,
                [this](double, const QString &label) {
                    m_lineupOcrLabel = label;
                    emit lineupPositionsChanged();
                });
        connect(m_lineupPosWorker, &LineupPositionExtractor::finishedExtraction, this,
                &AppController::onLineupPositionsFinished);
    }
    m_lineupPosWorker->configure(jobs);
    m_lineupOcrRunning = true;
    m_lineupOcrLabel = QStringLiteral("OCR…");
    emit lineupPositionsChanged();
    m_lineupPosWorker->start();
}

void AppController::cancelLineupPositions()
{
    if (m_lineupPosWorker && m_lineupPosWorker->isRunning())
        m_lineupPosWorker->requestStop();
}

void AppController::clearLineupPositions()
{
    m_lineupPositions.clear();
    const QString path = lineupPositionsPath();
    if (!path.isEmpty())
        QFile::remove(path);
    m_lineupOcrLabel.clear();
    emit lineupPositionsChanged();
}

void AppController::onLineupPositionsFinished(bool ok, const QString &error,
                                              const QVariantMap &result)
{
    m_lineupOcrRunning = false;
    if (!ok) {
        m_lineupOcrLabel = error;
        emit lineupPositionsChanged();
        if (!error.isEmpty() && error != QStringLiteral("cancelled")) {
            m_lastError = error;
            emit errorChanged();
        }
        return;
    }

    m_lineupPositions = result;
    const int nA = result.value(QStringLiteral("teamA")).toList().size();
    const int nB = result.value(QStringLiteral("teamB")).toList().size();
    m_lineupOcrLabel = QStringLiteral("Positions: %1 + %2").arg(nA).arg(nB);

    // Persist next to the other per-video artifacts.
    const QString path = lineupPositionsPath();
    if (!path.isEmpty()) {
        QJsonObject root;
        root[QStringLiteral("teamA")] =
            QJsonArray::fromVariantList(result.value(QStringLiteral("teamA")).toList());
        root[QStringLiteral("teamB")] =
            QJsonArray::fromVariantList(result.value(QStringLiteral("teamB")).toList());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
    emit lineupPositionsChanged();
}

QString AppController::denseTrackPath() const
{
    const QString dir = m_match->matchDir();
    if (dir.isEmpty())
        return {};
    return dir + QStringLiteral("/homography_dense") + m_match->videoSuffix()
           + QStringLiteral(".bin");
}

void AppController::propagateHomography()
{
    if (!m_videoLoaded || m_videoPath.isEmpty()) {
        m_lastError = QStringLiteral("Abre un video antes de propagar");
        emit errorChanged();
        return;
    }
    const QVector<HomographyManager::Keyframe> &kfs = m_homography->keyframeData();
    if (kfs.size() < 2) {
        m_lastError = QStringLiteral("Se necesitan al menos 2 keyframes para propagar");
        emit errorChanged();
        return;
    }
    if (m_homoWorker && m_homoWorker->isRunning())
        return;

    QVector<HomographyWorker::Keyframe> wkfs;
    wkfs.reserve(kfs.size());
    for (const HomographyManager::Keyframe &k : kfs) {
        HomographyWorker::Keyframe wk;
        wk.frame = k.frame;
        for (int j = 0; j < 4; ++j)
            wk.img[j] = k.image[j];
        wkfs.append(wk);
    }

    QDir().mkpath(m_match->matchDir());
    const QString outPath = denseTrackPath();

    // Per-frame player boxes (full-res px) over the keyframe span, from the
    // loaded chunk detections, so the flow ignores moving players. Empty when
    // no tracking data is loaded (the worker then tracks features everywhere).
    QHash<int, QVector<QRect>> playerBoxes;
    if (m_tracking->hasDetections() && m_fps > 0.0) {
        const int f0 = kfs.first().frame, f1 = kfs.last().frame;
        for (int f = f0; f <= f1; ++f) {
            const QVariantList dets = m_tracking->detectionsAt(f / m_fps);
            if (dets.isEmpty())
                continue;
            QVector<QRect> boxes;
            boxes.reserve(dets.size());
            for (const QVariant &v : dets) {
                const QVariantMap m = v.toMap();
                boxes.append(QRect(m.value(QStringLiteral("x")).toInt(),
                                   m.value(QStringLiteral("y")).toInt(),
                                   m.value(QStringLiteral("w")).toInt(),
                                   m.value(QStringLiteral("h")).toInt()));
            }
            playerBoxes.insert(f, boxes);
        }
    }

    if (!m_homoWorker) {
        m_homoWorker = new HomographyWorker(this);
        connect(m_homoWorker, &HomographyWorker::progressChanged, this,
                [this](double frac, const QString &label) {
                    m_homography->setPropProgress(frac, label);
                });
        connect(m_homoWorker, &HomographyWorker::finished, this,
                &AppController::onPropagationFinished);
    }

    m_homography->clearPropagation();
    m_homography->setPropagating(true, QStringLiteral("Iniciando propagación…"));
    m_homoWorker->configure(m_videoPath, wkfs, outPath, playerBoxes,
                            m_homography->graphicsRects(), aggregatedStaticMask());
    m_homoWorker->start();
}

void AppController::cancelPropagation()
{
    if (m_homoWorker && m_homoWorker->isRunning())
        m_homoWorker->requestStop();
}

void AppController::autoCalibrateHomography()
{
    if (!m_videoLoaded || m_lastFrame.isNull()) {
        m_lastError = QStringLiteral("Abre un video antes de auto-calibrar");
        emit errorChanged();
        return;
    }
    const cv::Mat Hinit = m_homography->homographyAt(m_currentFrame);
    if (Hinit.empty()) {
        m_lastError = QStringLiteral("Coloca los puntos A–D (o un keyframe) antes de auto-calibrar");
        emit errorChanged();
        return;
    }

    // QImage -> BGR cv::Mat (deep copy; the RGB view aliases QImage memory).
    const QImage rgb = m_lastFrame.convertToFormat(QImage::Format_RGB888);
    const cv::Mat rgbView(rgb.height(), rgb.width(), CV_8UC3,
                          const_cast<uchar *>(rgb.bits()),
                          static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgbView, bgr, cv::COLOR_RGB2BGR);

    // Player boxes (video pixels) to mask out, if chunk detections are loaded.
    std::vector<cv::Rect> boxes;
    for (const QVariant &v : m_tracking->detectionsAt(m_positionSec)) {
        const QVariantMap m = v.toMap();
        boxes.emplace_back(m.value(QStringLiteral("x")).toInt(),
                           m.value(QStringLiteral("y")).toInt(),
                           m.value(QStringLiteral("w")).toInt(),
                           m.value(QStringLiteral("h")).toInt());
    }

    const LineCalibrator::Result res = LineCalibrator::calibrate(bgr, boxes, Hinit);
    if (!res.ok) {
        m_lastError = QStringLiteral("Auto-calibración por líneas falló (%1 líneas, %2 inliers, %3px)")
                          .arg(res.lineCount).arg(res.inliers)
                          .arg(res.reprojErr, 0, 'f', 1);
        emit errorChanged();
        return;
    }
    m_homography->applyRefinedHomography(m_currentFrame, res.H, res.reprojErr);
    markDirty();
}

QString AppController::exportHomographies()
{
    if (!m_videoLoaded || m_totalFrames <= 0) {
        m_lastError = QStringLiteral("Open a video before exporting homographies");
        emit errorChanged();
        return {};
    }
    if (m_homography->keyframeCount() < 1) {
        m_lastError = QStringLiteral("Calibrate at least one keyframe before exporting");
        emit errorChanged();
        return {};
    }
    const QString dirPath = m_match->matchDir();
    if (dirPath.isEmpty()) {
        m_lastError = QStringLiteral("No match directory for this video");
        emit errorChanged();
        return {};
    }
    QDir().mkpath(dirPath);

    QJsonArray frames;
    for (int f = 0; f < m_totalFrames; ++f) {
        const cv::Mat H = m_homography->homographyAt(f);
        if (H.empty())
            continue;
        cv::Mat Hd;
        H.convertTo(Hd, CV_64F);
        QJsonArray h;
        for (int i = 0; i < 9; ++i)
            h.append(Hd.at<double>(i / 3, i % 3));
        QJsonObject row;
        row[QStringLiteral("f")]    = f;
        row[QStringLiteral("H")]    = h;
        row[QStringLiteral("conf")] = m_homography->confidenceAt(f);
        frames.append(row);
    }

    QJsonObject root;
    root[QStringLiteral("video")]  = m_videoPath;
    root[QStringLiteral("width")]  = m_videoWidth;
    root[QStringLiteral("height")] = m_videoHeight;
    root[QStringLiteral("fps")]    = m_fps;
    root[QStringLiteral("count")]  = frames.size();
    root[QStringLiteral("note")]   = QStringLiteral(
        "Row H is a 3x3 (row-major) homography mapping image pixels -> pitch "
        "meters (105x68). Its inverse maps pitch -> image.");
    root[QStringLiteral("frames")] = frames;

    const QString outPath = exportHomographiesPath();
    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("Cannot write %1").arg(outPath);
        emit errorChanged();
        return {};
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    return outPath;
}

// ---- phase F4: shot segmentation ------------------------------------------

QString AppController::shotsPath() const
{
    const QString dir = m_match->matchDir();
    if (dir.isEmpty())
        return {};
    return dir + QStringLiteral("/shots") + m_match->videoSuffix() + QStringLiteral(".json");
}

void AppController::loadShotsIfPresent()
{
    m_shots.clear();
    const QString path = shotsPath();
    if (!path.isEmpty() && QFile::exists(path))
        m_shots = ShotDetector::load(path);
    emit shotsChanged();
    updatePitchVisible();
}

bool AppController::pitchVisibleAt(int frame) const
{
    if (m_shots.isEmpty())
        return true;   // unknown -> don't block
    for (const ShotDetector::Shot &s : m_shots)
        if (frame >= s.startFrame && frame <= s.endFrame)
            return s.pitch;
    return true;        // outside analyzed range -> assume visible
}

void AppController::updatePitchVisible()
{
    const bool v = pitchVisibleAt(m_currentFrame);
    if (v != m_pitchVisible) {
        m_pitchVisible = v;
        emit pitchVisibleChanged();
    }
}

QVariantList AppController::shots() const
{
    QVariantList list;
    for (const ShotDetector::Shot &s : m_shots) {
        QVariantMap m;
        m[QStringLiteral("start")] = s.startFrame;
        m[QStringLiteral("end")] = s.endFrame;
        m[QStringLiteral("pitch")] = s.pitch;
        m[QStringLiteral("grass")] = s.grassMean;
        list.append(m);
    }
    return list;
}

void AppController::detectShots()
{
    if (!m_videoLoaded || m_videoPath.isEmpty()) {
        m_lastError = QStringLiteral("Abre un video antes de detectar tomas");
        emit errorChanged();
        return;
    }
    if (m_shotWorker && m_shotWorker->isRunning())
        return;

    // Restrict to the match window when the markers define one.
    int start = 0, end = 0;
    if (m_match->matchStartFrame() > 0 || m_match->matchEndFrame() > 0) {
        start = m_match->matchStartFrame();
        end = m_match->matchEndFrame();
    }

    QDir().mkpath(m_match->matchDir());
    if (!m_shotWorker) {
        m_shotWorker = new ShotDetector(this);
        connect(m_shotWorker, &ShotDetector::progressChanged, this,
                [this](double frac, const QString &label) {
                    m_shotProgress = frac;
                    m_shotLabel = label;
                    emit shotStateChanged();
                });
        connect(m_shotWorker, &ShotDetector::finished, this,
                &AppController::onShotDetectFinished);
    }
    m_shotDetecting = true;
    m_shotProgress = 0.0;
    m_shotLabel = QStringLiteral("Iniciando…");
    emit shotStateChanged();
    m_shotWorker->configure(m_videoPath, start, end, shotsPath());
    m_shotWorker->start();
}

void AppController::cancelShotDetection()
{
    if (m_shotWorker && m_shotWorker->isRunning())
        m_shotWorker->requestStop();
}

// ---- feature masks (Features tab) -----------------------------------------

void AppController::publishMask(const QImage &overlay, const QString &info)
{
    if (overlay.isNull()) {
        clearMaskPreview();
        return;
    }
    m_maskProvider->setImage(overlay);
    ++m_maskSerial;
    m_maskShown = true;
    m_maskInfo = info;
    emit maskChanged();
}

void AppController::clearMaskPreview()
{
    if (!m_maskShown && m_maskInfo.isEmpty())
        return;
    m_maskProvider->setImage(QImage());
    ++m_maskSerial;
    m_maskShown = false;
    m_maskInfo.clear();
    emit maskChanged();
}

void AppController::previewGreenMask()
{
    if (!m_videoLoaded || m_lastFrame.isNull()) {
        m_lastError = QStringLiteral("Abre un video antes de generar la máscara");
        emit errorChanged();
        return;
    }
    const cv::Mat bgr = qimageToBgr(m_lastFrame);
    const cv::Mat mask = MaskGenerator::greenMask(bgr);
    const double frac = mask.empty()
        ? 0.0 : double(cv::countNonZero(mask)) / (mask.rows * mask.cols);
    publishMask(colorizeMask(mask, QColor(0x30, 0xd9, 0x80), 120),
                QStringLiteral("Césped · %1% del cuadro · frame %2")
                    .arg(frac * 100.0, 0, 'f', 1).arg(m_currentFrame));
}

void AppController::showStaticMask(int chunkNumber)
{
    const QString dir = staticMaskDir();
    if (dir.isEmpty()) {
        m_lastError = QStringLiteral("El video no pertenece a un proyecto");
        emit errorChanged();
        return;
    }
    const QString path = dir
        + QStringLiteral("/video_part_%1/mask.png")
              .arg(chunkNumber, 3, 10, QLatin1Char('0'));
    const cv::Mat mask = cv::imread(path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (mask.empty()) {
        m_lastError = QStringLiteral("No hay máscara estática para la parte %1 "
                                     "(genérala primero)").arg(chunkNumber);
        emit errorChanged();
        clearMaskPreview();
        return;
    }
    const double frac = double(cv::countNonZero(mask)) / (mask.rows * mask.cols);
    publishMask(colorizeMask(mask, QColor(0xe3, 0x54, 0x49), 130),
                QStringLiteral("Estáticos · parte %1 · %2% del cuadro")
                    .arg(chunkNumber).arg(frac * 100.0, 0, 'f', 1));
}

void AppController::showStaticUnion()
{
    const QString dir = staticMaskDir();
    cv::Mat u = dir.isEmpty() ? cv::Mat()
        : MaskGenerator::unionStaticMasks(dir, m_homography->staticVoteFrac());

    // Canvas: the union's size if we have one, else the video frame size.
    const cv::Size sz = u.empty()
        ? cv::Size(std::max(1, m_videoWidth), std::max(1, m_videoHeight))
        : u.size();
    cv::Mat combined = u.empty() ? cv::Mat::zeros(sz, CV_8U) : u.clone();

    // OR in the manual logo boxes — exactly what the worker also excludes
    // (graphicsRects()), so the preview matches the real RANSAC exclusion.
    const QVector<QRectF> &g = m_homography->graphicsRects();
    for (const QRectF &r : g) {
        cv::Rect s(cvRound(r.x() * sz.width), cvRound(r.y() * sz.height),
                   cvRound(r.width() * sz.width), cvRound(r.height() * sz.height));
        s &= cv::Rect(0, 0, sz.width, sz.height);
        if (s.area() > 0) combined(s).setTo(255);
    }

    if (cv::countNonZero(combined) == 0) {
        m_lastError = QStringLiteral("No hay máscara estática ni cajas manuales todavía");
        emit errorChanged();
        clearMaskPreview();
        return;
    }
    const double frac = double(cv::countNonZero(combined)) / (combined.rows * combined.cols);
    publishMask(colorizeMask(combined, QColor(0xe3, 0x54, 0x49), 130),
                QStringLiteral("Combinada · %1% · %2 caja(s) · voto %3")
                    .arg(frac * 100.0, 0, 'f', 1).arg(g.size())
                    .arg(m_homography->staticVoteFrac(), 0, 'f', 2));
}

void AppController::chunkFrameAtCurrent(int &chunk, int &frameInChunk) const
{
    // Same mapping the tracking data uses: 10 fps global slots, 600 per chunk.
    const int slot = qMax(0, qRound(m_positionSec * 10.0));
    chunk = slot / 600 + 1;
    frameInChunk = slot % 600;
}

QString AppController::greenMaskPath(int chunk, int frameInChunk) const
{
    const QString dir = greenMaskDir();
    if (dir.isEmpty())
        return {};
    return dir + QStringLiteral("/video_part_%1/frame_%2.png")
                     .arg(chunk, 3, 10, QLatin1Char('0'))
                     .arg(frameInChunk, 5, 10, QLatin1Char('0'));
}

QString AppController::staticMaskPath(int chunk) const
{
    const QString dir = staticMaskDir();
    if (dir.isEmpty())
        return {};
    return dir + QStringLiteral("/video_part_%1/mask.png")
                     .arg(chunk, 3, 10, QLatin1Char('0'));
}

bool AppController::hasChunkMasksAtCurrent() const
{
    int chunk = 0, f = 0;
    chunkFrameAtCurrent(chunk, f);
    return QFile::exists(greenMaskPath(chunk, f)) || QFile::exists(staticMaskPath(chunk));
}

bool AppController::showChunkMasks()
{
    int chunk = 0, f = 0;
    chunkFrameAtCurrent(chunk, f);

    cv::Mat green = cv::imread(greenMaskPath(chunk, f).toStdString(), cv::IMREAD_GRAYSCALE);
    cv::Mat stat  = cv::imread(staticMaskPath(chunk).toStdString(), cv::IMREAD_GRAYSCALE);
    if (green.empty() && stat.empty()) {
        clearMaskPreview();
        return false;
    }

    // Canvas from whichever mask we have; align the other to it.
    const cv::Size sz = !green.empty() ? green.size() : stat.size();
    if (!stat.empty() && stat.size() != sz)
        cv::resize(stat, stat, sz, 0, 0, cv::INTER_NEAREST);
    if (!green.empty() && green.size() != sz)
        cv::resize(green, green, sz, 0, 0, cv::INTER_NEAREST);

    // Combined tinted overlay: grass green, static graphics red (graphics win
    // on overlap so the excluded regions stand out).
    QImage overlay(sz.width, sz.height, QImage::Format_ARGB32);
    const QRgb cg = qRgba(0x30, 0xd9, 0x80, 110);
    const QRgb cr = qRgba(0xe3, 0x54, 0x49, 140);
    int gN = 0, sN = 0;
    for (int y = 0; y < sz.height; ++y) {
        const uchar *gp = green.empty() ? nullptr : green.ptr<uchar>(y);
        const uchar *sp = stat.empty() ? nullptr : stat.ptr<uchar>(y);
        QRgb *dst = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = 0; x < sz.width; ++x) {
            if (sp && sp[x]) { dst[x] = cr; ++sN; }
            else if (gp && gp[x]) { dst[x] = cg; ++gN; }
            else dst[x] = 0u;
        }
    }
    const double area = double(sz.width) * sz.height;
    QString info = QStringLiteral("Precalc · parte %1 · frame %2 · césped %3%")
                       .arg(chunk).arg(f)
                       .arg(area > 0 ? gN / area * 100.0 : 0.0, 0, 'f', 1);
    if (sN > 0)
        info += QStringLiteral(" · gráficos %1%").arg(sN / area * 100.0, 0, 'f', 1);
    publishMask(overlay, info);
    return true;
}

void AppController::startMaskGen(int kind)
{
    if (m_maskWorker && m_maskWorker->isRunning())
        return;
    if (!m_videoLoaded) {
        m_lastError = QStringLiteral("Abre un video antes de generar máscaras");
        emit errorChanged();
        return;
    }
    const QString chunksDir = m_match->chunksDir();
    const QString matchDir = m_match->matchDir();
    if (matchDir.isEmpty() || m_match->chunkCount() <= 0
        || !QDir(chunksDir).exists()) {
        m_lastError = QStringLiteral("Crea los chunks primero (pestaña Chunks)");
        emit errorChanged();
        return;
    }

    if (!m_maskWorker) {
        m_maskWorker = new MaskGenerator(this);
        connect(m_maskWorker, &MaskGenerator::progressChanged, this,
                [this](double frac, const QString &label) {
                    m_maskGenProgress = frac;
                    m_maskGenLabel = label;
                    emit maskGenChanged();
                });
        connect(m_maskWorker, &MaskGenerator::finished, this,
                &AppController::onMaskGenFinished);
    }
    m_maskWorker->configure(
        kind == 0 ? MaskGenerator::Kind::Green : MaskGenerator::Kind::Static,
        chunksDir, {}, matchDir, m_match->videoSuffix());

    m_maskGenRunning = true;
    m_maskGenProgress = 0.0;
    m_maskGenKind = (kind == 0) ? QStringLiteral("green") : QStringLiteral("static");
    m_maskGenLabel = QStringLiteral("Iniciando…");
    emit maskGenChanged();
    m_maskWorker->start();
}

void AppController::generateGreenMasks()  { startMaskGen(0); }
void AppController::generateStaticMasks() { startMaskGen(1); }

void AppController::cancelMaskGen()
{
    if (m_maskWorker && m_maskWorker->isRunning())
        m_maskWorker->requestStop();
}

void AppController::onMaskGenFinished(bool ok, const QString &error, int written)
{
    m_maskGenRunning = false;
    m_maskGenLabel = ok
        ? QStringLiteral("Listo · %1 %2").arg(written)
              .arg(m_maskGenKind == QLatin1String("green")
                       ? QStringLiteral("máscaras") : QStringLiteral("partes"))
        : error;
    emit maskGenChanged();
    if (!ok && !error.isEmpty() && error != QStringLiteral("Cancelado")) {
        m_lastError = error;
        emit errorChanged();
    }
}

QVariantMap AppController::maskSummary() const
{
    QVariantMap m;
    const int chunks = m_match->chunkCount();
    m[QStringLiteral("chunks")] = chunks;
    int greenChunks = 0, staticChunks = 0, greenFrames = 0;
    if (!m_match->matchDir().isEmpty()) {
        const QDir gm(greenMaskDir());
        if (gm.exists()) {
            const QStringList parts = gm.entryList({QStringLiteral("video_part_*")},
                                                   QDir::Dirs | QDir::NoDotAndDotDot);
            greenChunks = parts.size();
            for (const QString &p : parts)
                greenFrames += QDir(gm.filePath(p))
                                   .entryList({QStringLiteral("frame_*.png")}, QDir::Files)
                                   .size();
        }
        const QDir sm(staticMaskDir());
        if (sm.exists())
            staticChunks = sm.entryList({QStringLiteral("video_part_*")},
                                        QDir::Dirs | QDir::NoDotAndDotDot).size();
    }
    m[QStringLiteral("greenChunks")]  = greenChunks;
    m[QStringLiteral("staticChunks")] = staticChunks;
    m[QStringLiteral("greenFrames")]  = greenFrames;
    return m;
}

QImage AppController::aggregatedStaticMask() const
{
    const QString dir = staticMaskDir();
    if (dir.isEmpty())
        return {};
    const cv::Mat out = MaskGenerator::unionStaticMasks(
        dir, m_homography->staticVoteFrac());
    if (out.empty())
        return {};
    QImage img(out.cols, out.rows, QImage::Format_Grayscale8);
    for (int y = 0; y < out.rows; ++y)
        memcpy(img.scanLine(y), out.ptr<uchar>(y), static_cast<size_t>(out.cols));
    return img;
}

QString AppController::solverBackend() const
{
    return QString::fromLatin1(homog::backendName(homog::Backend::Default));
}

void AppController::setSolverBackend(const QString &name)
{
    const homog::Backend b = (name == QLatin1String("custom"))
        ? homog::Backend::Custom : homog::Backend::OpenCV;
    if (homog::defaultBackend() == b)
        return;
    homog::setDefaultBackend(b);
    emit solverBackendChanged();
}

void AppController::onShotDetectFinished(bool ok, const QString &error, int shotCount)
{
    Q_UNUSED(shotCount);
    m_shotDetecting = false;
    m_shotLabel = ok ? QStringLiteral("Tomas detectadas") : error;
    emit shotStateChanged();
    if (!ok) {
        if (!error.isEmpty() && error != QStringLiteral("Cancelado")) {
            m_lastError = error;
            emit errorChanged();
        }
        return;
    }
    loadShotsIfPresent();
    markDirty();
}

void AppController::onPropagationFinished(bool ok, const QString &error,
                                          int startFrame, int count)
{
    Q_UNUSED(startFrame);
    Q_UNUSED(count);
    m_homography->setPropagating(false, ok ? QStringLiteral("Propagación completada")
                                           : error);
    if (!ok) {
        if (!error.isEmpty() && error != QStringLiteral("Cancelado")) {
            m_lastError = error;
            emit errorChanged();
        }
        return;
    }
    m_homography->loadDenseTrack(denseTrackPath());
    markDirty();
}
