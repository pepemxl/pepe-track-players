#ifndef TRACKINGMANAGER_H
#define TRACKINGMANAGER_H

#include <QThread>
#include <QHash>
#include <QMutex>
#include <QPair>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QVector>
#include <atomic>
#include <utility>
#include <vector>

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
    Q_PROPERTY(bool hasDetections READ hasDetections NOTIFY snapshotChanged)
    Q_PROPERTY(int inferredCount READ inferredCount NOTIFY snapshotChanged)
    Q_PROPERTY(QString modelName READ modelName CONSTANT)

public:
    static constexpr int kChipCount = 90;

    explicit TrackingManager(QObject *parent = nullptr);
    ~TrackingManager() override;

    void setSource(const QString &path);

    // Frame ranges to skip (pre-match, post-match, commercials). Thread-safe;
    // the worker snapshots them when a run starts.
    void setExclusions(const QVector<QPair<int, int>> &frameRanges);

    // Populate the tab from the persisted per-chunk tracking CSVs
    // (video_chunks/video_part_NNN.csv) so previous offline runs show up.
    // GUI thread only; no-op while a live run is in progress.
    void loadFromChunkCsvs(const QString &chunksDir, double durationSec,
                           const std::vector<std::pair<double, double>> &excludedSec);

    bool hasDetections() const { return !m_detsBySlot.isEmpty(); }

    // Detected boxes (video pixels) at a playback position, from the
    // loaded chunk CSVs. Slots are 0.1 s (chunk fps). Each entry carries
    // its track key ("NNN-Txx") and, when assigned, the player info.
    Q_INVOKABLE QVariantList detectionsAt(double sec) const;

    // Assign a roster player to a chunk-track: its boxes show the shirt
    // number and team color instead of the track id. Persisted in
    // <matchDir>/track_assignments.json.
    Q_INVOKABLE void assignTrack(const QString &key, int number,
                                 const QString &name, int team);
    Q_INVOKABLE void clearAssignment(const QString &key);
    QVariantMap assignmentFor(const QString &key) const { return m_assignments.value(key); }

    // Propagate manual assignments across chunk boundaries by spatial
    // continuity (last box of an identified track vs. first boxes of the
    // next chunk's tracks). allChunks=false keeps only inferences for the
    // chunk at currentSec. Writes the per-chunk metadata files. Returns
    // the number of inferred identities.
    Q_INVOKABLE int inferIdentities(bool allChunks, double currentSec);
    Q_INVOKABLE void clearInferred();
    int inferredCount() const { return m_inferred.size(); }

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

    struct Det
    {
        int   trackId{0};
        float x{0}, y{0}, w{0}, h{0}, conf{0};
    };
    QHash<int, QVector<Det>> m_detsBySlot;   // key = round(sec * 10)

    static QString trackKey(int chunkNumber, int trackId);
    static int chunkOfKey(const QString &key);
    void saveAssignments() const;
    void loadAssignments();
    QString metadataDir() const;
    void writeChunkMetadata(int chunkNumber) const;
    void refreshTrackRowNames();

    QHash<QString, QVariantMap> m_assignments;   // "NNN-Txx" -> {number,name,team}
    QHash<QString, QVariantMap> m_inferred;      // same shape, derived
    QString m_assignmentsPath;

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
