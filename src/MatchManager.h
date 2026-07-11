#ifndef MATCHMANAGER_H
#define MATCHMANAGER_H

#include <QDateTime>
#include <QJsonArray>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <utility>
#include <vector>

class VideoOpsWorker;
class LineupExtractor;

// Match ("project") registry + frame markers + offline video operations.
//
// A match can hold several videos (TV feed, tactical camera, panoramic
// camera, other). Every opened video gets an id inside its match in
// LOCAL_DATA/matches/games.json, and its artifacts live in the match dir
// LOCAL_DATA/matches/match_<id 4 ceros>/ with a per-video suffix:
//   preprocessed_20fps_<NN>.mp4, video_chunks_<NN>/, lineups_<NN>/,
//   lineups_<NN>.json, video_chunks_metadata_<NN>/, markers_<NN>.json,
//   track_assignments_<NN>.json
// A video may contain several camera views: an optional crop rect
// (top-left / bottom-right corners) selects the view; the 20 fps
// preprocess applies it, so chunks and tracking work in cropped space.
class MatchManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool registered READ registered NOTIFY matchChanged)
    Q_PROPERTY(int matchId READ matchId NOTIFY matchChanged)
    Q_PROPERTY(QString status READ status NOTIFY matchChanged)
    Q_PROPERTY(QString matchDir READ matchDir NOTIFY matchChanged)
    Q_PROPERTY(int chunkCount READ chunkCount NOTIFY matchChanged)
    Q_PROPERTY(int videoId READ videoId NOTIFY matchChanged)
    Q_PROPERTY(QString videoRole READ videoRole NOTIFY matchChanged)
    Q_PROPERTY(QString videoSegment READ videoSegment NOTIFY matchChanged)
    Q_PROPERTY(QVariantList videos READ videos NOTIFY matchChanged)
    Q_PROPERTY(bool hasCrop READ hasCrop NOTIFY matchChanged)
    Q_PROPERTY(QRect crop READ crop NOTIFY matchChanged)
    Q_PROPERTY(bool cropPending READ cropPending NOTIFY matchChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
    Q_PROPERTY(int matchStartFrame READ matchStartFrame NOTIFY markersChanged)
    Q_PROPERTY(int matchEndFrame READ matchEndFrame NOTIFY markersChanged)
    Q_PROPERTY(bool hasLineupMarkers READ hasLineupMarkers NOTIFY markersChanged)
    Q_PROPERTY(bool lineupsExtracted READ lineupsExtracted NOTIFY matchChanged)
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
    int videoId() const { return m_videoId; }
    QString videoRole() const { return m_videoRole; }
    QString videoSegment() const { return m_videoSegment; }
    QVariantList videos() const;
    bool hasCrop() const { return m_crop.isValid() && !m_crop.isEmpty(); }
    QRect crop() const { return m_crop; }
    bool cropPending() const { return m_cropPending; }
    QVariantList markers() const { return m_markers; }
    int matchStartFrame() const;
    int matchEndFrame() const;
    bool hasLineupMarkers() const;
    bool lineupsExtracted() const { return m_lineupsExtracted; }
    bool opRunning() const { return m_opRunning; }
    QString opLabel() const { return m_opLabel; }
    double opProgress() const { return m_opProgress; }
    QString lastError() const { return m_lastError; }

    // Per-video artifact paths (suffix "_<NN>" from the current video id).
    QString videoSuffix() const;
    QString preprocessedFile() const;
    QString chunksDir() const;
    QString chunksMetadataDir() const;
    QString lineupsDir() const;
    QString lineupsJsonPath() const;
    QString markersPath() const;
    QString assignmentsPath() const;

    // Projects menu support.
    Q_INVOKABLE QVariantList listProjects() const;
    // Creates an empty project (no videos yet) and makes it current; the
    // first video arrives later through prepareAddVideo(). Returns its id.
    Q_INVOKABLE int createProject();
    // The next setVideo() adds the video to the current match with a role
    // ("tv_feed" | "tactical" | "panoramic" | "other") and a segment
    // ("full" | "first_half" | "second_half" | "extra1" | "extra2" |
    //  "penalties" | "partial_first_half" | "partial_second_half")
    // instead of registering a new match.
    Q_INVOKABLE void prepareAddVideo(const QString &role,
                                     const QString &segment = QStringLiteral("full"));
    // The next setVideo() resolves to this exact project video entry.
    // Needed because the same file can appear several times in a project
    // (one entry per camera view/crop), so path lookup is ambiguous.
    Q_INVOKABLE void prepareOpenVideo(int matchId, int videoId);

    // View crop (original-video pixels). Cleared crop = full frame.
    Q_INVOKABLE void setCrop(int x, int y, int width, int height);
    Q_INVOKABLE void clearCrop();

    Q_INVOKABLE void addMarker(const QString &type, int frame);
    Q_INVOKABLE void removeMarker(int index);

    Q_INVOKABLE void preprocess();
    Q_INVOKABLE void createChunks();
    Q_INVOKABLE void trackChunks();
    // Runs the tracking op for a single chunk (video_part_<number>).
    Q_INVOKABLE void trackChunk(int number);
    Q_INVOKABLE void extractLineups();
    Q_INVOKABLE void cancelOp();

    // Chunks of the current video: [{number, file, frames, start_sec,
    // end_sec, hasCsv}], from chunks.json (or the chunk files).
    Q_INVOKABLE QVariantList chunksList() const;

    // Camera-sync points: real match minute -> frame, per video and
    // period ("1T"/"2T", minutes 0..45 every 5). One frame per
    // (period, minute); persisted in sync_points_<NN>.json.
    Q_INVOKABLE QVariantList syncPoints(int videoId) const;
    Q_INVOKABLE void setSyncPoint(int videoId, const QString &period,
                                  int minute, int frame);
    Q_INVOKABLE void removeSyncPoint(int videoId, const QString &period, int minute);

    // Video-time ranges excluded from tracking: before match_start, after
    // match_end, and every commercial range.
    std::vector<std::pair<double, double>> excludedRangesSec() const;
    QVector<QPair<int, int>> excludedFrameRanges() const;

    // Contents of lineups_<NN>.json (empty map if absent).
    QVariantMap loadLineups() const;
    QDateTime lineupsModified() const;

signals:
    void matchChanged();
    void markersChanged();
    void opStateChanged();
    void syncPointsChanged();
    // OCR result: { teamA: [{number,name}...], teamB: [...],
    //               teamNameA: str, teamNameB: str }
    void lineupsReady(const QVariantMap &result);

private:
    static QString dataRoot();
    QString matchesDir() const;
    QString gamesJsonPath() const;
    QString matchDirName() const;   // "match_<id>" with 4 leading zeros

    void registerOrLoad();
    void updateGamesEntry();
    void migrateLegacyArtifacts();
    void loadMarkers();
    void saveMarkers();
    QString syncPointsPathFor(int videoId) const;
    std::vector<std::pair<double, double>> commercialRangesSec() const;
    void startOp(int op, int onlyChunk = 0);
    void onOpProgress(double fraction, const QString &label);
    void onOpFinished(int op, bool ok, const QString &error, const QVariantMap &result);
    void onLineupsFinished(bool ok, const QString &error, const QVariantMap &result);
    int firstMarkerFrame(const QString &type) const;

    VideoOpsWorker  *m_worker{nullptr};
    LineupExtractor *m_lineup{nullptr};

    QString m_videoPath;
    double  m_fps{25.0};
    int     m_totalFrames{0};

    int     m_matchId{0};
    int     m_videoId{0};
    QString m_videoRole;
    QString m_videoSegment;
    QRect   m_crop;
    bool    m_cropPending{false};
    QString m_status;
    QString m_matchDir;
    int     m_chunkCount{0};
    QString m_preprocessedPath;
    QJsonArray m_videosJson;       // all video entries of the current match
    QVariantList m_markers;

    // Pending "add video to project" request (from the projects menu).
    int     m_pendingAddMatchId{0};
    QString m_pendingAddRole;
    QString m_pendingAddSegment;
    // Pending "open this exact project video" request.
    int     m_pendingOpenMatchId{0};
    int     m_pendingOpenVideoId{0};

    bool    m_opRunning{false};
    QString m_opLabel;
    double  m_opProgress{0.0};
    QString m_lastError;
    bool    m_lineupsExtracted{false};
};

#endif
