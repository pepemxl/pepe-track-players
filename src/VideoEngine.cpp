#include "VideoEngine.h"

#include <QMutexLocker>
#include <algorithm>

namespace {

QImage matToImage(const cv::Mat &bgr)
{
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

} // namespace

VideoEngine::VideoEngine(QObject *parent)
    : QThread(parent)
{
}

VideoEngine::~VideoEngine()
{
    stopProcessing();
}

void VideoEngine::setSource(const QString &path)
{
    m_sourcePath = path;
}

void VideoEngine::stopProcessing()
{
    m_stopped.store(true);
    m_pauseCond.wakeAll();
    if (isRunning()) {
        wait();
    }
}

void VideoEngine::setPaused(bool paused)
{
    m_paused.store(paused);
    if (!paused) {
        m_pauseCond.wakeAll();
    }
}

void VideoEngine::seekToFrame(int frame)
{
    if (frame < 0) frame = 0;
    m_seekFrame.store(frame);
    m_pauseCond.wakeAll();
}

void VideoEngine::stepFrames(int delta)
{
    if (delta == 0) return;
    int prev = m_stepDelta.load();
    while (!m_stepDelta.compare_exchange_weak(prev, prev + delta)) {}
    m_pauseCond.wakeAll();
}

void VideoEngine::setSpeed(double speed)
{
    m_speed.store(std::clamp(speed, 0.1, 8.0));
}

void VideoEngine::setPlaybackFps(double fps)
{
    if (fps < 0.0) fps = 0.0;
    m_playbackFps.store(std::min(fps, 240.0));
}

void VideoEngine::run()
{
    m_stopped.store(false);
    m_seekFrame.store(-1);
    m_stepDelta.store(0);

    cv::VideoCapture cap;
    if (!cap.open(m_sourcePath.toStdString()) || !cap.isOpened()) {
        emit error(QStringLiteral("Failed to open video: %1").arg(m_sourcePath));
        emit finished();
        return;
    }

    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0.0 || fps > 240.0) fps = 30.0;
    m_fps.store(fps);
    const double baseFrameDelayMs = 1000.0 / fps;

    const int total = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    m_totalFrames.store(total);
    const int width  = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    emit videoInfo(width, height, total, fps);

    while (!m_stopped.load()) {
        // Apply pending absolute / relative seeks.
        const int seek = m_seekFrame.exchange(-1);
        if (seek >= 0) {
            int target = seek;
            if (total > 0) target = std::min(target, total - 1);
            cap.set(cv::CAP_PROP_POS_FRAMES, target);
        }
        const int step = m_stepDelta.exchange(0);
        if (step != 0) {
            const int displayed = static_cast<int>(cap.get(cv::CAP_PROP_POS_FRAMES)) - 1;
            int target = displayed + step;
            if (target < 0) target = 0;
            if (total > 0) target = std::min(target, total - 1);
            cap.set(cv::CAP_PROP_POS_FRAMES, target);
        }

        cv::Mat frame;
        if (!cap.read(frame) || frame.empty()) {
            // End of file (or decode hiccup): stay alive so seeks still work.
            m_paused.store(true);
            emit endReached();
            QMutexLocker lock(&m_pauseMutex);
            while (m_paused.load() && !m_stopped.load()
                   && m_seekFrame.load() < 0 && m_stepDelta.load() == 0) {
                m_pauseCond.wait(&m_pauseMutex);
            }
            continue;
        }

        const int frameIndex = static_cast<int>(cap.get(cv::CAP_PROP_POS_FRAMES)) - 1;
        emit frameReady(matToImage(frame), frameIndex, frameIndex / fps);

        // Pause is checked AFTER emitting so a seek-while-paused shows the
        // new frame immediately (videopp pattern).
        if (m_paused.load()) {
            QMutexLocker lock(&m_pauseMutex);
            while (m_paused.load() && !m_stopped.load()
                   && m_seekFrame.load() < 0 && m_stepDelta.load() == 0) {
                m_pauseCond.wait(&m_pauseMutex);
            }
        } else {
            const double playbackFps = m_playbackFps.load();
            int delay;
            if (playbackFps > 0.0) {
                delay = static_cast<int>(1000.0 / playbackFps);
            } else {
                const double s = m_speed.load();
                delay = static_cast<int>(baseFrameDelayMs / (s > 0.0 ? s : 1.0));
            }
            if (delay < 1) delay = 1;
            QThread::msleep(delay);
        }
    }

    cap.release();
    emit finished();
}
