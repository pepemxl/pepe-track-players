#ifndef VIDEOENGINE_H
#define VIDEOENGINE_H

#include <opencv2/opencv.hpp>
#include <QThread>
#include <QImage>
#include <QString>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>

// File-playback engine. Same worker pattern as videopp's VideoProcessor:
// a QThread whose run() owns the cv::VideoCapture, controlled from the GUI
// thread through atomics and a wait condition (never blocking the GUI).
class VideoEngine : public QThread
{
    Q_OBJECT

public:
    explicit VideoEngine(QObject *parent = nullptr);
    ~VideoEngine() override;

    // Caller must stop the worker before changing the source.
    void setSource(const QString &path);
    QString sourcePath() const { return m_sourcePath; }

    void stopProcessing();

    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }

    // Frame-accurate controls (safe from any thread).
    void seekToFrame(int frame);
    void stepFrames(int delta);   // relative to the currently displayed frame

    void setSpeed(double speed);  // clamped to [0.1, 8.0]

    // Fixed playback rate in frames shown per second (0 = video's native
    // fps, optionally scaled by setSpeed).
    void setPlaybackFps(double fps);
    double playbackFps() const { return m_playbackFps.load(); }

    double fps() const { return m_fps.load(); }
    int totalFrames() const { return m_totalFrames.load(); }

signals:
    void frameReady(const QImage &frame, int frameIndex, double posSec);
    void videoInfo(int width, int height, int totalFrames, double fps);
    void endReached();
    void error(const QString &message);
    void finished();

protected:
    void run() override;

private:
    QString             m_sourcePath;
    std::atomic<bool>   m_stopped{false};
    std::atomic<bool>   m_paused{true};
    std::atomic<int>    m_seekFrame{-1};   // -1 = no absolute seek pending
    std::atomic<int>    m_stepDelta{0};
    std::atomic<double> m_speed{1.0};
    std::atomic<double> m_playbackFps{0.0};   // 0 = native rate
    std::atomic<double> m_fps{0.0};
    std::atomic<int>    m_totalFrames{0};
    QMutex              m_pauseMutex;
    QWaitCondition      m_pauseCond;
};

#endif
