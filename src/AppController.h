#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include "ShotDetector.h"

class VideoEngine;
class FrameProvider;
class RosterModel;
class MatchMetadata;
class TagsModel;
class HomographyManager;
class TrackingManager;
class TracksModel;
class MatchManager;
class HomographyWorker;
class MaskGenerator;
class PlayerSamples;

// Facade the QML layer talks to. Owns the video worker, the models and
// the managers; forwards frames into the image provider.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool videoLoaded READ videoLoaded NOTIFY videoStateChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoStateChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoStateChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoStateChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY videoStateChanged)
    Q_PROPERTY(double durationSec READ durationSec NOTIFY videoStateChanged)
    Q_PROPERTY(double fps READ fps NOTIFY videoStateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY positionChanged)
    Q_PROPERTY(double positionSec READ positionSec NOTIFY positionChanged)
    Q_PROPERTY(int frameSerial READ frameSerial NOTIFY frameSerialChanged)
    Q_PROPERTY(double playbackFps READ playbackFps WRITE setPlaybackFps NOTIFY playbackFpsChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)

    // Shot segmentation (phase F4). pitchVisible is false on frames that fall
    // in a non-pitch shot (close-up / graphic / crowd), where H is invalid.
    Q_PROPERTY(bool hasShots READ hasShots NOTIFY shotsChanged)
    Q_PROPERTY(int shotCount READ shotCount NOTIFY shotsChanged)
    Q_PROPERTY(bool pitchVisible READ pitchVisible NOTIFY pitchVisibleChanged)
    Q_PROPERTY(bool shotDetecting READ shotDetecting NOTIFY shotStateChanged)
    Q_PROPERTY(double shotProgress READ shotProgress NOTIFY shotStateChanged)
    Q_PROPERTY(QString shotLabel READ shotLabel NOTIFY shotStateChanged)

    // Homography estimator backend: "opencv" (cv::findHomography) or "custom"
    // (our own RANSAC). Configurable at runtime; also via PEPE_HOMOG_BACKEND.
    Q_PROPERTY(QString solverBackend READ solverBackend WRITE setSolverBackend NOTIFY solverBackendChanged)

    // Feature masks (Features tab): grass / static-graphic masks that guide
    // the homography flow and RANSAC. maskSerial busts the overlay Image
    // cache; maskShown is true while a preview mask is loaded.
    Q_PROPERTY(int maskSerial READ maskSerial NOTIFY maskChanged)
    Q_PROPERTY(bool maskShown READ maskShown NOTIFY maskChanged)
    Q_PROPERTY(QString maskInfo READ maskInfo NOTIFY maskChanged)
    Q_PROPERTY(bool maskGenRunning READ maskGenRunning NOTIFY maskGenChanged)
    Q_PROPERTY(double maskGenProgress READ maskGenProgress NOTIFY maskGenChanged)
    Q_PROPERTY(QString maskGenLabel READ maskGenLabel NOTIFY maskGenChanged)
    Q_PROPERTY(QString maskGenKind READ maskGenKind NOTIFY maskGenChanged)

    Q_PROPERTY(QObject *metadata READ metadataObj CONSTANT)
    Q_PROPERTY(QObject *homeRoster READ homeRosterObj CONSTANT)
    Q_PROPERTY(QObject *awayRoster READ awayRosterObj CONSTANT)
    Q_PROPERTY(QObject *tags READ tagsObj CONSTANT)
    Q_PROPERTY(QObject *homography READ homographyObj CONSTANT)
    Q_PROPERTY(QObject *tracking READ trackingObj CONSTANT)
    Q_PROPERTY(QObject *tracksModel READ tracksModelObj CONSTANT)
    Q_PROPERTY(QObject *match READ matchObj CONSTANT)
    Q_PROPERTY(QObject *playerSamples READ playerSamplesObj CONSTANT)

    // Secondary player (camera-sync section): a second project video
    // playing side by side with the main one.
    Q_PROPERTY(bool secLoaded READ secLoaded NOTIFY secStateChanged)
    Q_PROPERTY(QString secVideoName READ secVideoName NOTIFY secStateChanged)
    Q_PROPERTY(int secVideoId READ secVideoId NOTIFY secStateChanged)
    Q_PROPERTY(bool secPlaying READ secPlaying NOTIFY secPlayingChanged)
    Q_PROPERTY(int secCurrentFrame READ secCurrentFrame NOTIFY secPositionChanged)
    Q_PROPERTY(double secPositionSec READ secPositionSec NOTIFY secPositionChanged)
    Q_PROPERTY(int secTotalFrames READ secTotalFrames NOTIFY secStateChanged)
    Q_PROPERTY(double secFps READ secFps NOTIFY secStateChanged)
    Q_PROPERTY(int secFrameSerial READ secFrameSerial NOTIFY secFrameSerialChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    FrameProvider *frameProvider() const { return m_frameProvider; }
    FrameProvider *frameProvider2() const { return m_frameProvider2; }
    FrameProvider *maskProvider() const { return m_maskProvider; }

    bool secLoaded() const { return m_secLoaded; }
    QString secVideoName() const { return m_secVideoName; }
    int secVideoId() const { return m_secVideoId; }
    bool secPlaying() const { return m_secPlaying; }
    int secCurrentFrame() const { return m_secCurrentFrame; }
    double secPositionSec() const { return m_secPositionSec; }
    int secTotalFrames() const { return m_secTotalFrames; }
    double secFps() const { return m_secFps; }
    int secFrameSerial() const { return m_secFrameSerial; }

    Q_INVOKABLE void openSecondary(int videoId, const QString &path);
    Q_INVOKABLE void closeSecondary();
    Q_INVOKABLE void toggleSecPlay();
    Q_INVOKABLE void seekSecFrac(double frac);
    Q_INVOKABLE void seekSecFrame(int frame);

    bool videoLoaded() const { return m_videoLoaded; }
    QString videoName() const { return m_videoName; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    int totalFrames() const { return m_totalFrames; }
    double durationSec() const { return m_durationSec; }
    double fps() const { return m_fps; }
    bool playing() const { return m_playing; }
    int currentFrame() const { return m_currentFrame; }
    double positionSec() const { return m_positionSec; }
    int frameSerial() const { return m_frameSerial; }
    double playbackFps() const { return m_playbackFps; }
    void setPlaybackFps(double fps);
    bool dirty() const { return m_dirty; }
    QString lastError() const { return m_lastError; }

    QObject *metadataObj() const;
    QObject *homeRosterObj() const;
    QObject *awayRosterObj() const;
    QObject *tagsObj() const;
    QObject *homographyObj() const;
    QObject *trackingObj() const;
    QObject *tracksModelObj() const;
    QObject *matchObj() const;
    QObject *playerSamplesObj() const;

    Q_INVOKABLE void openVideo(const QUrl &url);
    Q_INVOKABLE void openVideoFile(const QString &path);
    // Opens one specific project video entry (needed when the same file
    // appears several times as different camera views).
    Q_INVOKABLE void openProjectVideo(int matchId, int videoId, const QString &path);
    // Registers the video in the current project with a role
    // ("tv_feed" | "tactical" | "panoramic" | "other") and a segment
    // ("full", "first_half", "second_half", "extra1", "extra2",
    //  "penalties", "partial_first_half", "partial_second_half").
    Q_INVOKABLE void addVideoToProject(const QUrl &url, const QString &role,
                                       const QString &segment);
    // View crop from two corners in video pixels (multi-view videos).
    Q_INVOKABLE void setVideoCrop(double x1, double y1, double x2, double y2);
    Q_INVOKABLE void clearVideoCrop();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seekFrac(double frac);
    Q_INVOKABLE void seekFrame(int frame);
    Q_INVOKABLE void stepFrames(int delta);
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE QString timecode(double sec) const;

    // vx/vy in video pixel coordinates; team 0 = home, 1 = away.
    Q_INVOKABLE void addTag(double vx, double vy, int team, int rosterRow);

    // Tagging mutations routed here so undo/redo can capture them.
    Q_INVOKABLE void removeTag(int row);
    Q_INVOKABLE void assignTrack(const QString &key, int number,
                                 const QString &name, int team);
    Q_INVOKABLE void clearTrackAssignment(const QString &key);

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE bool saveProject();

    // Deletes the current project (artifacts + games.json entry) and returns
    // the app to the empty "open a video" state.
    Q_INVOKABLE void deleteProject();

    // Captures a reference appearance sample for a role (PlayerSamples::Role):
    // crops the current frame at the given video-pixel box, saves a thumbnail
    // and records it for the current video.
    Q_INVOKABLE void capturePlayerSample(int role, double vx, double vy,
                                         double vw, double vh);

    // Phase F2: run the inter-frame optical-flow propagation over the manual
    // keyframes and load the resulting dense per-frame homography track.
    Q_INVOKABLE void propagateHomography();
    Q_INVOKABLE void cancelPropagation();

    // Phase F3: auto-calibrate the homography at the current frame by snapping
    // the detected pitch lines to the field model, starting from the current
    // (manual/interpolated) H, and store the result as a verified keyframe.
    Q_INVOKABLE void autoCalibrateHomography();

    // Export the per-frame homography (image->pitch 3x3) for every frame to
    // <project>/homographies.json. Returns the written path, or an empty
    // string on failure (see lastError).
    Q_INVOKABLE QString exportHomographies();

    // Phase F4 shot segmentation.
    bool hasShots() const { return !m_shots.isEmpty(); }
    int shotCount() const { return m_shots.size(); }
    bool pitchVisible() const { return m_pitchVisible; }
    bool shotDetecting() const { return m_shotDetecting; }
    double shotProgress() const { return m_shotProgress; }
    QString shotLabel() const { return m_shotLabel; }
    Q_INVOKABLE void detectShots();
    Q_INVOKABLE void cancelShotDetection();

    QString solverBackend() const;
    void setSolverBackend(const QString &name);
    // Shots for the timeline: [{start,end,pitch,grass}] in frames.
    Q_INVOKABLE QVariantList shots() const;

    // ---- Feature masks (Features tab) -------------------------------------
    int maskSerial() const { return m_maskSerial; }
    bool maskShown() const { return m_maskShown; }
    QString maskInfo() const { return m_maskInfo; }
    bool maskGenRunning() const { return m_maskGenRunning; }
    double maskGenProgress() const { return m_maskGenProgress; }
    QString maskGenLabel() const { return m_maskGenLabel; }
    QString maskGenKind() const { return m_maskGenKind; }

    // Live preview of the grass mask for the current frame.
    Q_INVOKABLE void previewGreenMask();
    // Load and show a previously generated static-graphic mask for a chunk.
    Q_INVOKABLE void showStaticMask(int chunkNumber);
    // Show the combined RANSAC-exclusion mask (majority-vote union of the
    // per-chunk static masks OR the manual logo boxes) at the current voteFrac.
    Q_INVOKABLE void showStaticUnion();
    // Load the precalculated masks (grass for the current frame + static for the
    // current chunk) that were persisted to disk and overlay them. Maps the
    // current playback position to a chunk/frame like the tracking data does.
    // Returns true if any saved mask was found and shown.
    Q_INVOKABLE bool showChunkMasks();
    // Are there any precalculated masks on disk for the current chunk/frame?
    Q_INVOKABLE bool hasChunkMasksAtCurrent() const;
    Q_INVOKABLE void clearMaskPreview();

    // Batch generation over the current video's chunks.
    Q_INVOKABLE void generateGreenMasks();
    Q_INVOKABLE void generateStaticMasks();
    Q_INVOKABLE void cancelMaskGen();

    // {chunks, greenChunks, staticChunks, greenFrames} for the UI summary.
    Q_INVOKABLE QVariantMap maskSummary() const;

signals:
    void videoStateChanged();
    void playingChanged();
    void positionChanged();
    void frameSerialChanged();
    void playbackFpsChanged();
    void dirtyChanged();
    void undoChanged();
    void secStateChanged();
    void secPlayingChanged();
    void secPositionChanged();
    void secFrameSerialChanged();
    void errorChanged();
    void shotsChanged();
    void shotStateChanged();
    void pitchVisibleChanged();
    void solverBackendChanged();
    void maskChanged();
    void maskGenChanged();

private:
    void onFrameReady(const QImage &frame, int frameIndex, double posSec);
    void onVideoInfo(int width, int height, int totalFrames, double fps);
    void markDirty();
    void loadProjectIfPresent();
    void applyLineups(const QVariantMap &lineups);
    QString projectDir() const;

    void pushCommand(const QVariantMap &cmd);
    void applyCommand(const QVariantMap &cmd, bool isUndo);

    void onPropagationFinished(bool ok, const QString &error, int startFrame, int count);
    QString denseTrackPath() const;

    void onShotDetectFinished(bool ok, const QString &error, int shotCount);
    QString shotsPath() const;
    void loadShotsIfPresent();
    bool pitchVisibleAt(int frame) const;
    void updatePitchVisible();

    void startMaskGen(int kind);   // 0 = green, 1 = static
    void onMaskGenFinished(bool ok, const QString &error, int written);
    void publishMask(const class QImage &overlay, const QString &info);
    // Maps the current playback second to the chunk number and the frame index
    // within that chunk (10 fps chunks of 600 frames), as the tracking uses.
    void chunkFrameAtCurrent(int &chunk, int &frameInChunk) const;
    QString greenMaskPath(int chunk, int frameInChunk) const;
    QString staticMaskPath(int chunk) const;
    // Screen-space union of the per-chunk static-graphic masks (majority vote),
    // for excluding burnt-in graphics from the propagation flow. Null if none.
    QImage aggregatedStaticMask() const;

    VideoEngine       *m_engine{nullptr};
    FrameProvider     *m_frameProvider{nullptr};   // owned by the QML engine
    VideoEngine       *m_engine2{nullptr};
    FrameProvider     *m_frameProvider2{nullptr};  // owned by the QML engine
    FrameProvider     *m_maskProvider{nullptr};    // owned by the QML engine
    RosterModel       *m_homeRoster{nullptr};
    RosterModel       *m_awayRoster{nullptr};
    MatchMetadata     *m_metadata{nullptr};
    TagsModel         *m_tags{nullptr};
    HomographyManager *m_homography{nullptr};
    TrackingManager   *m_tracking{nullptr};
    TracksModel       *m_tracksModel{nullptr};
    MatchManager      *m_match{nullptr};
    PlayerSamples     *m_playerSamples{nullptr};
    HomographyWorker  *m_homoWorker{nullptr};
    ShotDetector      *m_shotWorker{nullptr};
    MaskGenerator     *m_maskWorker{nullptr};

    bool    m_videoLoaded{false};
    QString m_videoPath;
    QString m_videoName;
    int     m_videoWidth{0};
    int     m_videoHeight{0};
    int     m_totalFrames{0};
    double  m_durationSec{0.0};
    double  m_fps{25.0};
    bool    m_playing{false};
    QImage  m_lastFrame;          // most recent decoded frame (for line calibration)
    int     m_currentFrame{0};
    double  m_positionSec{0.0};
    int     m_frameSerial{0};
    double  m_playbackFps{0.0};   // 0 = native rate
    bool    m_dirty{false};
    QString m_lastError;

    QVector<QVariantMap> m_undoStack;
    QVector<QVariantMap> m_redoStack;

    QVector<ShotDetector::Shot> m_shots;   // sorted by startFrame
    bool    m_pitchVisible{true};
    bool    m_shotDetecting{false};
    double  m_shotProgress{0.0};
    QString m_shotLabel;

    int     m_maskSerial{0};
    bool    m_maskShown{false};
    QString m_maskInfo;
    bool    m_maskGenRunning{false};
    double  m_maskGenProgress{0.0};
    QString m_maskGenLabel;
    QString m_maskGenKind;

    bool    m_secLoaded{false};
    QString m_secVideoName;
    int     m_secVideoId{0};
    bool    m_secPlaying{false};
    int     m_secCurrentFrame{0};
    double  m_secPositionSec{0.0};
    int     m_secTotalFrames{0};
    double  m_secFps{25.0};
    int     m_secFrameSerial{0};
};

#endif
