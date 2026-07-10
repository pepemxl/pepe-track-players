#include "MatchManager.h"
#include "VideoOpsWorker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <algorithm>

MatchManager::MatchManager(QObject *parent)
    : QObject(parent)
{
    m_worker = new VideoOpsWorker(this);
    // Worker signals are emitted from its thread: queued delivery.
    connect(m_worker, &VideoOpsWorker::progressChanged,
            this, &MatchManager::onOpProgress, Qt::QueuedConnection);
    connect(m_worker, &VideoOpsWorker::opFinished,
            this, &MatchManager::onOpFinished, Qt::QueuedConnection);
}

MatchManager::~MatchManager()
{
    m_worker->stopAndWait();
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

void MatchManager::setVideo(const QString &path, double fps, int totalFrames)
{
    m_worker->stopAndWait();
    m_videoPath = path;
    m_fps = fps > 0.0 ? fps : 25.0;
    m_totalFrames = totalFrames;
    m_matchId = 0;
    m_status.clear();
    m_matchDir.clear();
    m_chunkCount = 0;
    m_preprocessedPath.clear();
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
    for (const QJsonValue &v : matches) {
        const QJsonObject o = v.toObject();
        maxId = std::max(maxId, o[QStringLiteral("id")].toInt());
        if (o[QStringLiteral("video")].toString() == m_videoPath) {
            m_matchId = o[QStringLiteral("id")].toInt();
            m_status = o[QStringLiteral("status")].toString();
            m_chunkCount = o[QStringLiteral("chunks")].toInt();
            m_preprocessedPath = o[QStringLiteral("preprocessed")].toString();
        }
    }

    if (m_matchId == 0) {
        m_matchId = maxId + 1;
        m_status = QStringLiteral("registered");
    }
    m_matchDir = matchesDir() + QLatin1Char('/') + matchDirName();
    // Migrate a folder created before ids were zero-padded.
    const QString legacyDir = matchesDir() + QStringLiteral("/match_%1").arg(m_matchId);
    if (legacyDir != m_matchDir && QDir(legacyDir).exists() && !QDir(m_matchDir).exists())
        QDir().rename(legacyDir, m_matchDir);
    QDir().mkpath(m_matchDir);

    // Normalize the preprocessed reference (it may carry a pre-padding dir).
    if (QFile::exists(m_matchDir + QStringLiteral("/preprocessed_20fps.mp4")))
        m_preprocessedPath = matchDirName() + QStringLiteral("/preprocessed_20fps.mp4");

    updateGamesEntry();
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

    QJsonObject entry;
    entry[QStringLiteral("id")]           = m_matchId;
    entry[QStringLiteral("video")]        = m_videoPath;
    entry[QStringLiteral("status")]       = m_status;
    entry[QStringLiteral("fps")]          = m_fps;
    entry[QStringLiteral("total_frames")] = m_totalFrames;
    entry[QStringLiteral("dir")]          = matchDirName();
    entry[QStringLiteral("preprocessed")] = m_preprocessedPath;
    entry[QStringLiteral("chunks")]       = m_chunkCount;
    entry[QStringLiteral("updated")]      = QDateTime::currentDateTime().toString(Qt::ISODate);

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

void MatchManager::loadMarkers()
{
    m_markers.clear();
    QFile file(m_matchDir + QStringLiteral("/markers.json"));
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
    QFile file(m_matchDir + QStringLiteral("/markers.json"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void MatchManager::addMarker(const QString &type, int frame)
{
    if (!registered())
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

void MatchManager::startOp(int op)
{
    if (!registered() || m_opRunning || m_worker->isRunning())
        return;
    m_worker->configure(static_cast<VideoOpsWorker::Op>(op),
                        m_videoPath, m_matchDir, commercialRangesSec());
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

void MatchManager::cancelOp()
{
    m_worker->requestStop();
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
        m_preprocessedPath = matchDirName() + QStringLiteral("/preprocessed_20fps.mp4");
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
