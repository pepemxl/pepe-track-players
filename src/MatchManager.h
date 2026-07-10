#ifndef MATCHMANAGER_H
#define MATCHMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <utility>
#include <vector>

class VideoOpsWorker;

// Per-video match registry + frame markers + offline video operations.
//
// Every opened video gets an id in LOCAL_DATA/matches/games.json and a data
// folder LOCAL_DATA/matches/match_<id>/. Frame markers (match start/end,
// lineups, benches, commercial ranges) live in that folder as markers.json;
// commercial ranges are excluded from chunk tracking.
class MatchManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool registered READ registered NOTIFY matchChanged)
    Q_PROPERTY(int matchId READ matchId NOTIFY matchChanged)
    Q_PROPERTY(QString status READ status NOTIFY matchChanged)
    Q_PROPERTY(QString matchDir READ matchDir NOTIFY matchChanged)
    Q_PROPERTY(int chunkCount READ chunkCount NOTIFY matchChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
    Q_PROPERTY(bool opRunning READ opRunning NOTIFY opStateChanged)
    Q_PROPERTY(QString opLabel READ opLabel NOTIFY opStateChanged)
    Q_PROPERTY(double opProgress READ opProgress NOTIFY opStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY opStateChanged)

public:
    explicit MatchManager(QObject *parent = nullptr);
    ~MatchManager() override;

    // Called by AppController when a video finishes opening.
    void setVideo(const QString &path, double fps, int totalFrames);

    bool registered() const { return m_matchId > 0; }
    int matchId() const { return m_matchId; }
    QString status() const { return m_status; }
    QString matchDir() const { return m_matchDir; }
    int chunkCount() const { return m_chunkCount; }
    QVariantList markers() const { return m_markers; }
    bool opRunning() const { return m_opRunning; }
    QString opLabel() const { return m_opLabel; }
    double opProgress() const { return m_opProgress; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void addMarker(const QString &type, int frame);
    Q_INVOKABLE void removeMarker(int index);

    Q_INVOKABLE void preprocess();
    Q_INVOKABLE void createChunks();
    Q_INVOKABLE void trackChunks();
    Q_INVOKABLE void cancelOp();

signals:
    void matchChanged();
    void markersChanged();
    void opStateChanged();

private:
    static QString dataRoot();
    QString matchesDir() const;
    QString gamesJsonPath() const;
    QString matchDirName() const;   // "match_<id>" with 4 leading zeros

    void registerOrLoad();
    void updateGamesEntry();
    void loadMarkers();
    void saveMarkers();
    std::vector<std::pair<double, double>> commercialRangesSec() const;
    void startOp(int op);
    void onOpProgress(double fraction, const QString &label);
    void onOpFinished(int op, bool ok, const QString &error, const QVariantMap &result);

    VideoOpsWorker *m_worker{nullptr};

    QString m_videoPath;
    double  m_fps{25.0};
    int     m_totalFrames{0};

    int     m_matchId{0};
    QString m_status;
    QString m_matchDir;
    int     m_chunkCount{0};
    QString m_preprocessedPath;
    QVariantList m_markers;

    bool    m_opRunning{false};
    QString m_opLabel;
    double  m_opProgress{0.0};
    QString m_lastError;
};

#endif
