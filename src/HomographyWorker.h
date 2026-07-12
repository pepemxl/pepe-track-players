#ifndef HOMOGRAPHYWORKER_H
#define HOMOGRAPHYWORKER_H

#include <QThread>
#include <QString>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QHash>
#include <QImage>
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
//
// Optionally, per-frame player boxes (from YOLO detections, in full-res image
// pixels) mask out moving objects when detecting the tracked features, so the
// camera-motion estimate rests only on the static background (phase F5).
class HomographyWorker : public QThread
{
    Q_OBJECT
public:
    struct Keyframe { int frame; QPointF img[4]; };

    explicit HomographyWorker(QObject *parent = nullptr);
    ~HomographyWorker() override;

    // playerBoxes maps a frame index to that frame's player boxes (full-res
    // image pixels); empty for frames with no detections, or omit entirely.
    // graphicsRegions are static on-screen overlays (scoreboard, logos) in
    // NORMALIZED [0,1] coordinates, masked out on every frame.
    // staticMask is an optional grayscale screen-space mask (any resolution;
    // non-zero = burnt-in graphic) auto-detected from the static-mask feature;
    // it is resized to the working frame and masked out on every frame, just
    // like graphicsRegions but pixel-accurate rather than rectangular.
    void configure(const QString &videoPath, const QVector<Keyframe> &keyframes,
                   const QString &outPath,
                   const QHash<int, QVector<QRect>> &playerBoxes = {},
                   const QVector<QRectF> &graphicsRegions = {},
                   const QImage &staticMask = {});
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
    QHash<int, QVector<QRect>> m_playerBoxes;  // frame -> boxes (full-res px)
    QVector<QRectF> m_graphics;      // static on-screen regions (normalized)
    QImage m_staticMask;             // auto-detected static-graphic mask (grayscale)
    std::atomic<bool> m_stop{false};
};

#endif
