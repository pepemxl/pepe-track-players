#ifndef LINEUPEXTRACTOR_H
#define LINEUPEXTRACTOR_H

#include <QThread>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <atomic>

// Extracts player numbers/names from frames marked lineup_a/b and
// bench_a/b: grabs each marked frame, saves it to <matchDir>/lineups/,
// OCRs it with the Windows OCR engine (scripts/ocr.ps1 via PowerShell)
// and parses "<number> <name>" rows.
class LineupExtractor : public QThread
{
    Q_OBJECT

public:
    struct Job
    {
        QString type;    // lineup_a / bench_a / lineup_b / bench_b
        int     frame{0};
        int     team{0}; // 0 = A (home), 1 = B (away)
    };

    explicit LineupExtractor(QObject *parent = nullptr);
    ~LineupExtractor() override;

    // One OCR'd token: its text and bounding box normalised to [0,1] over the
    // source image. Used to recover where each number/name sits on a graphic.
    struct Word {
        QString text;
        QRectF  rect;   // normalised (0..1) box on the source image
    };
    // Runs the Windows OCR (scripts/ocr.ps1 -WithBoxes) over an image file and
    // returns every recognised word with its normalised bounding box. Empty
    // with *errorOut set on failure. Blocking; call off the UI thread.
    static QVector<Word> ocrWords(const QString &imagePath, QString *errorOut = nullptr);

    // outDir: where the grabbed frames land. crop: optional view rect
    // applied before OCR (multi-view videos).
    void configure(const QString &videoPath, const QString &outDir,
                   const QVector<Job> &jobs, const QRect &crop = QRect());
    void requestStop() { m_stop.store(true); }
    void stopAndWait();

signals:
    void progressChanged(double fraction, const QString &label);
    // result: { teamA: [{number,name}...], teamB: [...],
    //           teamNameA: str, teamNameB: str }
    void finishedExtraction(bool ok, const QString &error, const QVariantMap &result);

protected:
    void run() override;

private:
    static QString resolveOcrScript();
    static QStringList ocrImage(const QString &scriptPath, const QString &imagePath,
                                QString *errorOut);
    static void parsePlayers(const QStringList &lines, QVariantList *players);
    static QString detectTeamName(const QStringList &lines);

    QString m_videoPath;
    QString m_outDir;
    QVector<Job> m_jobs;
    QRect m_crop;
    std::atomic<bool> m_stop{false};
};

#endif
