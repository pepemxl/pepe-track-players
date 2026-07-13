#ifndef LINEUPPOSITIONEXTRACTOR_H
#define LINEUPPOSITIONEXTRACTOR_H

#include <QThread>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <atomic>

// OCRs the captured line-up graphics (Players tab, team_a/b_lineup samples) and
// pulls out each player's shirt number together with where it sits on the
// image — a formation graphic places number 9 up top and number 1 at the back,
// so the normalised (x, y) of the number is the player's position on the pitch.
//
// Unlike LineupExtractor (which OCRs video frames marked lineup_a/b for the
// roster text), this works on already-captured image files and keeps the
// per-token positions.
class LineupPositionExtractor : public QThread
{
    Q_OBJECT

public:
    struct Job {
        QString image;   // absolute path to a captured line-up image
        int     team{0}; // 0 = A (home), 1 = B (away)
    };

    explicit LineupPositionExtractor(QObject *parent = nullptr);
    ~LineupPositionExtractor() override;

    void configure(const QVector<Job> &jobs);
    void requestStop() { m_stop.store(true); }
    void stopAndWait();

    // Reusable core: keeps pure jersey-number tokens (1..99) from an OCR pass
    // as { number, x, y, w, h } maps (x/y = normalised box centre). Public so
    // the headless op can exercise it without a thread.
    static QVariantList numbersFromImage(const QString &image, QString *errorOut);

signals:
    void progressChanged(double fraction, const QString &label);
    // result: { teamA: [{number,x,y,w,h}...], teamB: [...] }
    void finishedExtraction(bool ok, const QString &error, const QVariantMap &result);

protected:
    void run() override;

private:
    static QVariantList dedupeByNumber(const QVariantList &in);

    QVector<Job> m_jobs;
    std::atomic<bool> m_stop{false};
};

#endif
