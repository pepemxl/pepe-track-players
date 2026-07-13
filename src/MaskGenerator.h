#ifndef MASKGENERATOR_H
#define MASKGENERATOR_H

#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>

#include <opencv2/opencv.hpp>

// Offline generator of per-frame binary feature masks used to guide the
// homography flow / RANSAC (which pixels are pitch, which are burnt-in
// broadcast graphics). Works on the already-chunked video (video_part_NNN.mp4)
// so the mask numbering matches the tracking space.
//
// Output layout (under the match dir):
//   green_mask/video_part_<NNN>/frame_<KKKKK>.png   one 8-bit mask per frame
//   static_mask/video_part_<NNN>/mask.png           one mask per chunk
//
// Masks are single-channel PNG (0 = background, 255 = feature): the most
// compact lossless format for a binary image and trivially reloadable.
class MaskGenerator : public QThread
{
    Q_OBJECT
public:
    enum class Kind { Green, Static };

    explicit MaskGenerator(QObject *parent = nullptr);
    ~MaskGenerator() override;

    // chunksDir holds the video_part_<NNN>.mp4 files; outDir is the match dir
    // under which green_mask<suffix>/ or static_mask<suffix>/ trees are written.
    // videoSuffix (e.g. "_01") keeps a match's several videos from colliding.
    // chunkNumbers selects which parts to process (empty = every part found).
    void configure(Kind kind, const QString &chunksDir,
                   const QVector<int> &chunkNumbers, const QString &outDir,
                   const QString &videoSuffix = QString());

    void requestStop() { m_stop.store(true); }
    void stopAndWait();

    // ---- reusable mask kernels (also used for the live GUI preview) --------

    // Grass/pitch mask of a BGR frame: green in HSV, morphologically closed
    // and reduced to the largest blob (the field). Returns an 8UC1 0/255 mask
    // at the input resolution.
    static cv::Mat greenMask(const cv::Mat &bgr);

    // Static-overlay mask for one chunk: pixels whose value barely changes
    // across the chunk (low temporal std) while not being pitch grass — i.e.
    // scoreboards, logos and banners burnt into the feed. 8UC1 0/255 mask at
    // the chunk's native resolution, or empty on failure.
    static cv::Mat staticMask(const QString &chunkPath,
                              const std::atomic<bool> *stop = nullptr);

    // Majority-vote union of the per-chunk static masks under staticDir
    // (each in a video_part_<NNN>/mask.png). A pixel is kept when it is set in
    // at least voteFrac of the chunk masks (but always >= 2 chunks) — a burnt-in
    // graphic recurs across chunks, a per-chunk fluke sits in only one. Kept
    // inclusive: over-masking a little background is cheap, missing a graphic
    // biases the motion estimate. 8UC1 0/255, empty if none.
    static cv::Mat unionStaticMasks(const QString &staticDir, double voteFrac = 0.15);

signals:
    void progressChanged(double fraction, const QString &label);
    void finished(bool ok, const QString &error, int itemsWritten);

protected:
    void run() override;

private:
    static QVector<int> discoverChunks(const QString &chunksDir);
    int runGreen();
    int runStatic();

    Kind         m_kind{Kind::Green};
    QString      m_chunksDir;
    QString      m_outDir;
    QString      m_suffix;   // e.g. "_01"; appended to green_mask/static_mask dir names
    QVector<int> m_chunks;
    std::atomic<bool> m_stop{false};
};

#endif
