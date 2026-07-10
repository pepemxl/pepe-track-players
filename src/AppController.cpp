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
#include <cmath>

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    m_engine        = new VideoEngine(this);
    m_frameProvider = new FrameProvider();
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
    connect(m_tracking, &TrackingManager::tracksUpdated,
            m_tracksModel, &TracksModel::setRows);

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

    m_videoPath = path;
    m_videoName = QFileInfo(path).fileName();
    m_playing = false;

    m_engine->setSource(path);
    m_engine->setPaused(true);   // show the first frame, then hold
    m_engine->start();

    m_tracking->setSource(path);
    loadProjectIfPresent();

    emit playingChanged();
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
    m_tags->addTag(tag);
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
    emit videoStateChanged();
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
