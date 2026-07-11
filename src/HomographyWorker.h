#ifndef HOMOGRAPHYWORKER_H
#define HOMOGRAPHYWORKER_H

#include <QThread>
#include <QString>
#include <QPointF>
#include <QVector>
#include <atomic>

// Offline inter-frame homography propagation (phase F2 of docs/homography.md).
//
// Given the manual calibration keyframes (the 4 reference image points at
// specific frames), it estimates the camera motion between consecutive
// frames with LK optical flow + a RANSAC homography — moving objects
// (players) are a minority on a wide pitch view, so they fall out as
// outliers without an explicit mask — and propagates the 4 reference points
// across every frame in [firstKeyframe, lastKeyframe]. Within each segment
// between two keyframes it blends the forward propagation (from the earlier
// keyframe) with the backward one (from the later keyframe) to spread the
// accumulated drift. The result — 4 image points per frame — is written to a
// compact binary file that HomographyManager loads.
class HomographyWorker : public QThread
{
    Q_OBJECT
public:
    struct Keyframe { int frame; QPointF img[4]; };

    explicit HomographyWorker(QObject *parent = nullptr);
    ~HomographyWorker() override;

    void configure(const QString &videoPath, const QVector<Keyframe> &keyframes,
                   const QString &outPath);
    void requestStop() { m_stop.store(true); }
    void stopAndWait();

signals:
    void progressChanged(double fraction, const QString &label);
    // On success, the file at outPath holds `count` frames of 4 points each,
    // starting at frame `startFrame`.
    void finished(bool ok, const QString &error, int startFrame, int count);

protected:
    void run() override;

private:
    QString m_videoPath;
    QString m_outPath;
    QVector<Keyframe> m_keyframes;   // sorted by frame
    std::atomic<bool> m_stop{false};
};

#endif
