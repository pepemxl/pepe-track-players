#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QUrl>

class VideoEngine;
class FrameProvider;
class RosterModel;
class MatchMetadata;
class TagsModel;
class HomographyManager;
class TrackingManager;
class TracksModel;
class MatchManager;

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

    Q_PROPERTY(QObject *metadata READ metadataObj CONSTANT)
    Q_PROPERTY(QObject *homeRoster READ homeRosterObj CONSTANT)
    Q_PROPERTY(QObject *awayRoster READ awayRosterObj CONSTANT)
    Q_PROPERTY(QObject *tags READ tagsObj CONSTANT)
    Q_PROPERTY(QObject *homography READ homographyObj CONSTANT)
    Q_PROPERTY(QObject *tracking READ trackingObj CONSTANT)
    Q_PROPERTY(QObject *tracksModel READ tracksModelObj CONSTANT)
    Q_PROPERTY(QObject *match READ matchObj CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    FrameProvider *frameProvider() const { return m_frameProvider; }

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

    Q_INVOKABLE void openVideo(const QUrl &url);
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seekFrac(double frac);
    Q_INVOKABLE void seekFrame(int frame);
    Q_INVOKABLE void stepFrames(int delta);
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE QString timecode(double sec) const;

    // vx/vy in video pixel coordinates; team 0 = home, 1 = away.
    Q_INVOKABLE void addTag(double vx, double vy, int team, int rosterRow);

    Q_INVOKABLE bool saveProject();

signals:
    void videoStateChanged();
    void playingChanged();
    void positionChanged();
    void frameSerialChanged();
    void playbackFpsChanged();
    void dirtyChanged();
    void errorChanged();

private:
    void onFrameReady(const QImage &frame, int frameIndex, double posSec);
    void onVideoInfo(int width, int height, int totalFrames, double fps);
    void markDirty();
    void loadProjectIfPresent();
    QString projectDir() const;

    VideoEngine       *m_engine{nullptr};
    FrameProvider     *m_frameProvider{nullptr};   // owned by the QML engine
    RosterModel       *m_homeRoster{nullptr};
    RosterModel       *m_awayRoster{nullptr};
    MatchMetadata     *m_metadata{nullptr};
    TagsModel         *m_tags{nullptr};
    HomographyManager *m_homography{nullptr};
    TrackingManager   *m_tracking{nullptr};
    TracksModel       *m_tracksModel{nullptr};
    MatchManager      *m_match{nullptr};

    bool    m_videoLoaded{false};
    QString m_videoPath;
    QString m_videoName;
    int     m_videoWidth{0};
    int     m_videoHeight{0};
    int     m_totalFrames{0};
    double  m_durationSec{0.0};
    double  m_fps{25.0};
    bool    m_playing{false};
    int     m_currentFrame{0};
    double  m_positionSec{0.0};
    int     m_frameSerial{0};
    double  m_playbackFps{0.0};   // 0 = native rate
    bool    m_dirty{false};
    QString m_lastError;
};

#endif
