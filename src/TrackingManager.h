#ifndef TRACKINGMANAGER_H
#define TRACKINGMANAGER_H

#include <QThread>
#include <QMutex>
#include <QPair>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QVector>
#include <atomic>

// Offline inference pass over the whole video: pedestrian detection
// (OpenCV HOG, no model files needed) + greedy IoU track association.
// Same worker pattern as VideoEngine: run() owns its own cv::VideoCapture,
// results cross threads as queued QVariant signals.
class TrackingManager : public QThread
{
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunningInference NOTIFY runningChanged)
    Q_PROPERTY(bool completed READ completed NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY snapshotChanged)
    Q_PROPERTY(int framesProcessed READ framesProcessed NOTIFY snapshotChanged)
    Q_PROPERTY(int playersTracked READ playersTracked NOTIFY snapshotChanged)
    Q_PROPERTY(double avgConfidence READ avgConfidence NOTIFY snapshotChanged)
    Q_PROPERTY(int lostFrames READ lostFrames NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList frameChips READ frameChips NOTIFY snapshotChanged)
    Q_PROPERTY(QString modelName READ modelName CONSTANT)

public:
    static constexpr int kChipCount = 90;

    explicit TrackingManager(QObject *parent = nullptr);
    ~TrackingManager() override;

    void setSource(const QString &path);

    // Frame ranges to skip (pre-match, post-match, commercials). Thread-safe;
    // the worker snapshots them when a run starts.
    void setExclusions(const QVector<QPair<int, int>> &frameRanges);

    bool isRunningInference() const { return m_runningInference; }
    bool completed() const { return m_completed; }
    double progress() const { return m_progress; }
    int framesProcessed() const { return m_framesProcessed; }
    int playersTracked() const { return m_playersTracked; }
    double avgConfidence() const { return m_avgConfidence; }
    int lostFrames() const { return m_lostFrames; }
    QVariantList frameChips() const { return m_frameChips; }
    QString modelName() const { return QStringLiteral("yolov8n-coco · person"); }

    Q_INVOKABLE void toggleRun();
    void stopInference();

    QVariantList tracksSnapshot() const { return m_tracks; }

signals:
    void runningChanged();
    void snapshotChanged();
    void tracksUpdated(const QVariantList &tracks);
    void error(const QString &message);

    // Internal: emitted from the worker thread, delivered queued.
    void snapshotReady(double progress, const QVariantMap &stats,
                       const QVariantList &chips, const QVariantList &tracks);
    void runFinished(bool completedAll);

protected:
    void run() override;

private:
    void applySnapshot(double progress, const QVariantMap &stats,
                       const QVariantList &chips, const QVariantList &tracks);
    void handleRunFinished(bool completedAll);

    QString           m_sourcePath;
    std::atomic<bool> m_stopRequested{false};
    QMutex            m_exclMutex;
    QVector<QPair<int, int>> m_exclRanges;

    // GUI-thread state (updated only via queued applySnapshot).
    bool         m_runningInference{false};
    bool         m_completed{false};
    double       m_progress{0.0};
    int          m_framesProcessed{0};
    int          m_playersTracked{0};
    double       m_avgConfidence{0.0};
    int          m_lostFrames{0};
    QVariantList m_frameChips;
    QVariantList m_tracks;
};

#endif
