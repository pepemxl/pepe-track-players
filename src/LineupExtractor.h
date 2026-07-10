#ifndef LINEUPEXTRACTOR_H
#define LINEUPEXTRACTOR_H

#include <QThread>
#include <QString>
#include <QVariantList>
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

    void configure(const QString &videoPath, const QString &matchDir,
                   const QVector<Job> &jobs);
    void requestStop() { m_stop.store(true); }
    void stopAndWait();

signals:
    void progressChanged(double fraction, const QString &label);
    void finishedExtraction(bool ok, const QString &error,
                            const QVariantList &teamA, const QVariantList &teamB);

protected:
    void run() override;

private:
    static QString resolveOcrScript();
    static QStringList ocrImage(const QString &scriptPath, const QString &imagePath,
                                QString *errorOut);
    static void parsePlayers(const QStringList &lines, QVariantList *players);

    QString m_videoPath;
    QString m_matchDir;
    QVector<Job> m_jobs;
    std::atomic<bool> m_stop{false};
};

#endif
