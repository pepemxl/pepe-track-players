#ifndef VIDEOOPSWORKER_H
#define VIDEOOPSWORKER_H

#include <QThread>
#include <QRect>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <utility>
#include <vector>

// Offline video operations for a registered match, one op per run():
//  - Preprocess: re-time the source video to 20 fps
//    -> <matchDir>/preprocessed_20fps.mp4
//  - Chunk: split into 1-minute chunks at 10 fps
//    -> <matchDir>/video_chunks/video_part_NNN.mp4
//  - Track: person detection + IoU tracks per chunk
//    -> <matchDir>/video_chunks/video_part_NNN.csv
//    (frames inside excluded ranges — before match start, after match
//    end, commercials — are skipped entirely)
class VideoOpsWorker : public QThread
{
    Q_OBJECT

public:
    enum Op { Preprocess = 0, Chunk = 1, Track = 2 };
    Q_ENUM(Op)

    explicit VideoOpsWorker(QObject *parent = nullptr);
    ~VideoOpsWorker() override;

    // crop: optional view rect (original pixels) applied by Preprocess —
    // and by Chunk when it falls back to the original source. excludedSec:
    // [start,end] ranges in seconds of video time that the Track op skips.
    // onlyChunk > 0 restricts the Track op to that single chunk number
    // (video_part_<onlyChunk>); 0 tracks every chunk.
    void configure(Op op, const QString &sourcePath,
                   const QString &preprocessedPath, const QString &chunksDir,
                   const QRect &crop,
                   const std::vector<std::pair<double, double>> &excludedSec,
                   int onlyChunk = 0);
    void requestStop() { m_stop.store(true); }
    void stopAndWait();

signals:
    void progressChanged(double fraction, const QString &label);
    void opFinished(int op, bool ok, const QString &error, const QVariantMap &result);

protected:
    void run() override;

private:
    void runPreprocess();
    void runChunks();
    void runTrack();
    bool isExcluded(double sec) const;

    Op      m_op{Preprocess};
    QString m_sourcePath;
    QString m_preprocessedPath;
    QString m_chunksDir;
    QRect   m_crop;
    std::vector<std::pair<double, double>> m_excluded;
    int     m_onlyChunk{0};
    std::atomic<bool> m_stop{false};
};

#endif
