#include "LineupPositionExtractor.h"

#include "LineupExtractor.h"

#include <QRegularExpression>
#include <set>

LineupPositionExtractor::LineupPositionExtractor(QObject *parent)
    : QThread(parent)
{
}

LineupPositionExtractor::~LineupPositionExtractor()
{
    stopAndWait();
}

void LineupPositionExtractor::configure(const QVector<Job> &jobs)
{
    m_jobs = jobs;
    m_stop.store(false);
}

void LineupPositionExtractor::stopAndWait()
{
    m_stop.store(true);
    if (isRunning())
        wait();
}

QVariantList LineupPositionExtractor::numbersFromImage(const QString &image, QString *errorOut)
{
    const QVector<LineupExtractor::Word> words = LineupExtractor::ocrWords(image, errorOut);
    // A shirt number is a bare 1-2 digit token in [1, 99]. This rejects the
    // clock ("90:00"), the formation label ("4-1-3-2") and names.
    static const QRegularExpression numRe(QStringLiteral("^\\d{1,2}$"));

    QVariantList out;
    for (const LineupExtractor::Word &w : words) {
        if (!numRe.match(w.text).hasMatch())
            continue;
        const int n = w.text.toInt();
        if (n < 1 || n > 99)
            continue;
        QVariantMap p;
        p[QStringLiteral("number")] = n;
        p[QStringLiteral("x")] = w.rect.x() + w.rect.width()  / 2.0;   // box centre
        p[QStringLiteral("y")] = w.rect.y() + w.rect.height() / 2.0;
        p[QStringLiteral("w")] = w.rect.width();
        p[QStringLiteral("h")] = w.rect.height();
        out.append(p);
    }
    return out;
}

QVariantList LineupPositionExtractor::dedupeByNumber(const QVariantList &in)
{
    QVariantList out;
    std::set<int> seen;
    for (const QVariant &v : in) {
        const int n = v.toMap().value(QStringLiteral("number")).toInt();
        if (seen.insert(n).second)
            out.append(v);
    }
    return out;
}

void LineupPositionExtractor::run()
{
    QVariantList teamA, teamB;
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_stop.load()) {
            emit finishedExtraction(false, QStringLiteral("cancelled"), {});
            return;
        }
        const Job &job = m_jobs.at(i);
        emit progressChanged(static_cast<double>(i) / qMax(1, m_jobs.size()),
                             QStringLiteral("OCR line-up %1/%2")
                                 .arg(i + 1).arg(m_jobs.size()));

        QString err;
        const QVariantList nums = numbersFromImage(job.image, &err);
        if (nums.isEmpty() && !err.isEmpty()) {
            emit finishedExtraction(false, err, {});
            return;
        }
        (job.team == 0 ? teamA : teamB) += nums;
    }

    QVariantMap result;
    result[QStringLiteral("teamA")] = dedupeByNumber(teamA);
    result[QStringLiteral("teamB")] = dedupeByNumber(teamB);

    emit progressChanged(1.0, QStringLiteral("Line-up positions extracted"));
    emit finishedExtraction(true, {}, result);
}
