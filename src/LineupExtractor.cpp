#include "LineupExtractor.h"

#include <opencv2/opencv.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QVariantMap>
#include <set>

LineupExtractor::LineupExtractor(QObject *parent)
    : QThread(parent)
{
}

LineupExtractor::~LineupExtractor()
{
    stopAndWait();
}

void LineupExtractor::configure(const QString &videoPath, const QString &outDir,
                                const QVector<Job> &jobs, const QRect &crop)
{
    m_videoPath = videoPath;
    m_outDir = outDir;
    m_jobs = jobs;
    m_crop = crop;
    m_stop.store(false);
}

void LineupExtractor::stopAndWait()
{
    m_stop.store(true);
    if (isRunning()) {
        wait();
    }
}

QString LineupExtractor::resolveOcrScript()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/scripts/ocr.ps1"),
        appDir + QStringLiteral("/../../scripts/ocr.ps1"),
        QStringLiteral("scripts/ocr.ps1"),
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c))
            return QFileInfo(c).absoluteFilePath();
    }
    return {};
}

QStringList LineupExtractor::ocrImage(const QString &scriptPath, const QString &imagePath,
                                      QString *errorOut)
{
    QProcess proc;
    proc.start(QStringLiteral("powershell.exe"),
               { QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"), QStringLiteral("-File"), scriptPath,
                 QStringLiteral("-ImagePath"), imagePath });
    if (!proc.waitForFinished(60000)) {
        proc.kill();
        if (errorOut) *errorOut = QStringLiteral("OCR timed out");
        return {};
    }
    if (proc.exitCode() != 0) {
        if (errorOut)
            *errorOut = QStringLiteral("OCR failed: %1")
                            .arg(QString::fromLocal8Bit(proc.readAllStandardError()).left(200));
        return {};
    }
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    return out.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
}

QVector<LineupExtractor::Word> LineupExtractor::ocrWords(const QString &imagePath,
                                                         QString *errorOut)
{
    const QString scriptPath = resolveOcrScript();
    if (scriptPath.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("scripts/ocr.ps1 not found");
        return {};
    }
    QProcess proc;
    proc.start(QStringLiteral("powershell.exe"),
               { QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"), QStringLiteral("-File"), scriptPath,
                 QStringLiteral("-ImagePath"), imagePath, QStringLiteral("-WithBoxes") });
    if (!proc.waitForFinished(60000)) {
        proc.kill();
        if (errorOut) *errorOut = QStringLiteral("OCR timed out");
        return {};
    }
    if (proc.exitCode() != 0) {
        if (errorOut)
            *errorOut = QStringLiteral("OCR failed: %1")
                            .arg(QString::fromLocal8Bit(proc.readAllStandardError()).left(200));
        return {};
    }

    const QStringList lines =
        QString::fromLocal8Bit(proc.readAllStandardOutput())
            .split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);

    double W = 0, H = 0;
    QVector<Word> words;
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (line.startsWith(QLatin1String("#SIZE"))) {
            if (parts.size() >= 3) { W = parts.at(1).toDouble(); H = parts.at(2).toDouble(); }
            continue;
        }
        if (parts.size() < 5 || W <= 0 || H <= 0)
            continue;
        const double x = parts.at(0).toDouble();
        const double y = parts.at(1).toDouble();
        const double w = parts.at(2).toDouble();
        const double h = parts.at(3).toDouble();
        // Text is the remainder (a token never contains a tab, but be safe).
        const QString text = QStringList(parts.mid(4)).join(QLatin1Char('\t')).trimmed();
        if (text.isEmpty())
            continue;
        words.append({ text, QRectF(x / W, y / H, w / W, h / H) });
    }
    return words;
}

void LineupExtractor::parsePlayers(const QStringList &lines, QVariantList *players)
{
    // Numbered rows like "10 MESSI", "#7 De Paul", or with a captain /
    // goalkeeper badge OCR'd as a stray leading glyph: "B 10 LIONEL MESSI".
    static const QRegularExpression numberedRe(
        QStringLiteral("^\\s*(?:[\\p{L}#*'•·]{1,2}\\s+)?(\\d{1,2})[\\s.:·•—-]+"
                       "([\\p{L}][\\p{L}'’. -]{1,30})\\s*$"),
        QRegularExpression::UseUnicodePropertiesOption);
    // Name-only rows (OCR missed the shirt number): at least two words.
    static const QRegularExpression nameOnlyRe(
        QStringLiteral("^\\s*([\\p{L}][\\p{L}'’.-]+(?:\\s+[\\p{L}'’.-]+)+)\\s*$"),
        QRegularExpression::UseUnicodePropertiesOption);
    // Broadcast furniture that is not a player.
    static const QStringList blocklist = {
        QStringLiteral("FIFA"), QStringLiteral("CUP"), QStringLiteral("WORLD"),
        QStringLiteral("ENTRENADOR"), QStringLiteral("COACH"), QStringLiteral("TÉCNICO"),
    };

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const QString upper = line.toUpper();
        bool blocked = false;
        for (const QString &word : blocklist) {
            if (upper.contains(word)) { blocked = true; break; }
        }
        if (blocked)
            continue;

        int number = 0;
        QString name;
        QRegularExpressionMatch m = numberedRe.match(line);
        if (m.hasMatch()) {
            number = m.captured(1).toInt();
            name = m.captured(2).simplified();
            if (number < 1 || number > 99)
                continue;
        } else {
            m = nameOnlyRe.match(line);
            if (!m.hasMatch())
                continue;
            name = m.captured(1).simplified();   // number stays 0 (edit later)
        }
        if (name.size() < 2)
            continue;

        QVariantMap player;
        player[QStringLiteral("number")] = number;
        player[QStringLiteral("name")] = name;
        players->append(player);
    }
}

QString LineupExtractor::detectTeamName(const QStringList &lines)
{
    // The team name is the header right above the first numbered player
    // row ("ARGENTINA" over "23 EMILIANO MARTÍNEZ"): walk back from that
    // row to the nearest all-caps line.
    static const QRegularExpression numberedRe(
        QStringLiteral("^\\s*(?:[\\p{L}#*'•·]{1,2}\\s+)?(\\d{1,2})[\\s.:·•—-]+"
                       "[\\p{L}][\\p{L}'’. -]{1,30}\\s*$"),
        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression headerRe(
        QStringLiteral("^[\\p{Lu}][\\p{Lu} .'-]{2,25}$"),
        QRegularExpression::UseUnicodePropertiesOption);
    static const QStringList blocklist = {
        QStringLiteral("FIFA"), QStringLiteral("CUP"), QStringLiteral("WORLD"),
        QStringLiteral("ENTRENADOR"), QStringLiteral("COACH"), QStringLiteral("TÉCNICO"),
        QStringLiteral("SUPLENTES"), QStringLiteral("BENCH"), QStringLiteral("VS"),
    };

    int firstPlayer = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (numberedRe.match(lines.at(i).trimmed()).hasMatch()) {
            firstPlayer = i;
            break;
        }
    }
    for (int i = firstPlayer - 1; i >= 0; --i) {
        const QString line = lines.at(i).trimmed();
        bool blocked = false;
        for (const QString &word : blocklist) {
            if (line.toUpper().contains(word)) { blocked = true; break; }
        }
        if (!blocked && headerRe.match(line).hasMatch())
            return line.simplified();
    }
    return {};
}

void LineupExtractor::run()
{
    const QString scriptPath = resolveOcrScript();
    if (scriptPath.isEmpty()) {
        emit finishedExtraction(false, QStringLiteral("scripts/ocr.ps1 not found"), {});
        return;
    }

    cv::VideoCapture cap;
    if (!cap.open(m_videoPath.toStdString())) {
        emit finishedExtraction(false, QStringLiteral("cannot open video"), {});
        return;
    }

    const QString outDir = m_outDir;
    QDir().mkpath(outDir);

    QVariantList teamA, teamB;
    QString teamNameA, teamNameB;
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_stop.load()) {
            emit finishedExtraction(false, QStringLiteral("cancelled"), {});
            return;
        }
        const Job &job = m_jobs.at(i);
        emit progressChanged(static_cast<double>(i) / m_jobs.size(),
                             QStringLiteral("OCR %1 (frame %2)").arg(job.type).arg(job.frame));

        cap.set(cv::CAP_PROP_POS_FRAMES, job.frame);
        cv::Mat frame;
        if (!cap.read(frame) || frame.empty())
            continue;
        if (m_crop.isValid() && !m_crop.isEmpty()) {
            const cv::Rect view = cv::Rect(m_crop.x(), m_crop.y(),
                                           m_crop.width(), m_crop.height())
                                  & cv::Rect(0, 0, frame.cols, frame.rows);
            if (view.area() > 0)
                frame = frame(view);
        }

        // BMP: the only codec guaranteed in this OpenCV build (no libpng).
        const QString bmp = outDir + QStringLiteral("/%1_f%2.bmp").arg(job.type).arg(job.frame);
        if (!cv::imwrite(bmp.toStdString(), frame))
            continue;

        QString ocrError;
        const QStringList lines = ocrImage(scriptPath, bmp, &ocrError);
        if (lines.isEmpty() && !ocrError.isEmpty()) {
            emit finishedExtraction(false, ocrError, {});
            return;
        }
        parsePlayers(lines, job.team == 0 ? &teamA : &teamB);

        // The team name only shows on the starting-lineup graphic.
        if (job.type.startsWith(QLatin1String("lineup"))) {
            QString &target = job.team == 0 ? teamNameA : teamNameB;
            if (target.isEmpty())
                target = detectTeamName(lines);
        }
    }

    // Dedupe keeping the first occurrence: by shirt number when known,
    // by name for entries whose number the OCR missed (number 0).
    auto dedupe = [](const QVariantList &in) {
        QVariantList out;
        std::set<int> seenNumbers;
        std::set<QString> seenNames;
        for (const QVariant &v : in) {
            const QVariantMap m = v.toMap();
            const int num = m.value(QStringLiteral("number")).toInt();
            const QString name = m.value(QStringLiteral("name")).toString().toUpper();
            if (!seenNames.insert(name).second)
                continue;
            if (num > 0 && !seenNumbers.insert(num).second)
                continue;
            out.append(v);
        }
        return out;
    };
    teamA = dedupe(teamA);
    teamB = dedupe(teamB);

    QVariantMap result;
    result[QStringLiteral("teamA")] = teamA;
    result[QStringLiteral("teamB")] = teamB;
    result[QStringLiteral("teamNameA")] = teamNameA;
    result[QStringLiteral("teamNameB")] = teamNameB;

    emit progressChanged(1.0, QStringLiteral("Lineups extracted"));
    emit finishedExtraction(true, {}, result);
}
