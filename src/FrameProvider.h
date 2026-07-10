#ifndef FRAMEPROVIDER_H
#define FRAMEPROVIDER_H

#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

// Thread-safe holder for the latest decoded frame. QML pulls it through
// "image://videoframe/<serial>" where the serial busts the Image cache.
class FrameProvider : public QQuickImageProvider
{
public:
    FrameProvider()
        : QQuickImageProvider(QQuickImageProvider::Image) {}

    void setImage(const QImage &image)
    {
        QMutexLocker lock(&m_mutex);
        m_image = image;
    }

    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        QMutexLocker lock(&m_mutex);
        if (size) *size = m_image.size();
        return m_image;
    }

private:
    QMutex m_mutex;
    QImage m_image;
};

#endif
