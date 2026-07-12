#ifndef SHOTDETECTOR_H
#define SHOTDETECTOR_H

#include <QThread>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <atomic>
#include <functional>

// Phase F4 of docs/homography.md — shot segmentation for feed_tv.
//
// A television feed cuts between cameras (wide, close-up, replay, graphics), so
// a homography can never be propagated across a cut and only the shots that
// actually show the pitch can be calibrated. This detects camera cuts by the
// drop in colour-histogram correlation between consecutive frames and measures
// each shot's pitch visibility from the fraction of grass (green) pixels, then
// labels every shot Pitch / NonPitch. The result feeds calibration (only Pitch
// shots), propagation (never crosses a cut) and confidence (NonPitch frames
// have no valid H).
class ShotDetector : public QThread
{
    Q_OBJECT
public:
    struct Shot
    {
        int    startFrame{0};
        int    endFrame{0};      // inclusive
        bool   pitch{false};     // enough grass visible to calibrate
        double grassMean{0.0};   // 0..1 median grass fraction over the shot
        double cutStrength{0.0}; // 1 - hist correlation at the opening cut
    };

    explicit ShotDetector(QObject *parent = nullptr);
    ~ShotDetector() override;

    void configure(const QString &videoPath, int startFrame, int endFrame,
                   const QString &outPath);
    void requestStop() { m_stop.store(true); }
    void stopAndWait();

    // Synchronous core, shared by the worker and the headless op. `stop` may be
    // null. `progress(fraction, label)` may be null.
    static QVector<Shot> detectSync(const QString &videoPath, int startFrame, int endFrame,
                                    const std::function<void(double, const QString &)> &progress,
                                    const std::atomic<bool> *stop);

    static bool save(const QString &path, const QVector<Shot> &shots);
    static QVector<Shot> load(const QString &path);
    static QJsonObject toJson(const QVector<Shot> &shots);

signals:
    void progressChanged(double fraction, const QString &label);
    void finished(bool ok, const QString &error, int shotCount);

protected:
    void run() override;

private:
    QString m_videoPath;
    QString m_outPath;
    int m_startFrame{0};
    int m_endFrame{0};
    std::atomic<bool> m_stop{false};
};

#endif
