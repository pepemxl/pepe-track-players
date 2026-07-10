#include "AppController.h"
#include "FrameProvider.h"
#include "MatchManager.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QVariantMap>
#include <cstdio>

#include <opencv2/videoio.hpp>

// Headless mode: run the lineup OCR for an already-marked video and print
// the players ("pepe_track_players.exe --extract-lineups <video>").
static int runExtractLineups(const QString &videoPath)
{
    MatchManager match;
    match.setVideo(videoPath, 25.0, 0);   // fps/frames irrelevant for OCR jobs

    QObject::connect(&match, &MatchManager::lineupsReady,
                     [](const QVariantMap &result) {
        auto dump = [&result](const char *team, const char *listKey, const char *nameKey) {
            const QVariantList list = result.value(QLatin1String(listKey)).toList();
            const QString name = result.value(QLatin1String(nameKey)).toString();
            std::fprintf(stdout, "%s%s%s (%d players)\n", team,
                         name.isEmpty() ? "" : " — ",
                         name.toUtf8().constData(),
                         static_cast<int>(list.size()));
            for (const QVariant &v : list) {
                const QVariantMap m = v.toMap();
                std::fprintf(stdout, "  %2d  %s\n",
                             m.value(QStringLiteral("number")).toInt(),
                             m.value(QStringLiteral("name")).toString().toUtf8().constData());
            }
        };
        dump("TEAM A", "teamA", "teamNameA");
        dump("TEAM B", "teamB", "teamNameB");
        std::fflush(stdout);
        QCoreApplication::exit(0);
    });
    QObject::connect(&match, &MatchManager::opStateChanged, [&match]() {
        if (!match.opRunning() && !match.lastError().isEmpty()) {
            std::fprintf(stderr, "error: %s\n", match.lastError().toUtf8().constData());
            std::fflush(stderr);
            QCoreApplication::exit(1);
        }
    });

    match.extractLineups();
    if (!match.opRunning())
        return 1;   // no markers / failed to start
    return QCoreApplication::exec();
}

// Headless: print the video-time ranges both tracking paths will skip
// (pre-match, post-match, commercials) for an already-marked video.
static int runDumpExclusions(const QString &videoPath)
{
    cv::VideoCapture cap(videoPath.toStdString());
    double fps = cap.isOpened() ? cap.get(cv::CAP_PROP_FPS) : 25.0;
    if (fps <= 0.0 || fps > 240.0) fps = 25.0;
    const int total = cap.isOpened()
        ? static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)) : 0;
    cap.release();

    MatchManager match;
    match.setVideo(videoPath, fps, total);
    std::fprintf(stdout, "match #%d, fps %.2f, %d frames\n",
                 match.matchId(), fps, total);
    std::fprintf(stdout, "match window: frame %d .. %d\n",
                 match.matchStartFrame(), match.matchEndFrame());
    for (const auto &[a, b] : match.excludedRangesSec()) {
        std::fprintf(stdout, "excluded: %.2fs .. %s\n",
                     a, b >= 1e11 ? "end" : qPrintable(QString::number(b, 'f', 2) + "s"));
    }
    std::fflush(stdout);
    return 0;
}

// Headless video op ("chunk" or "track") on an already-registered video.
// Honors the marker exclusions.
static int runMatchOp(const QString &videoPath, const QString &op)
{
    cv::VideoCapture cap(videoPath.toStdString());
    double fps = cap.isOpened() ? cap.get(cv::CAP_PROP_FPS) : 25.0;
    if (fps <= 0.0 || fps > 240.0) fps = 25.0;
    const int total = cap.isOpened()
        ? static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)) : 0;
    cap.release();

    MatchManager match;
    match.setVideo(videoPath, fps, total);
    if (op == QLatin1String("track") && match.chunkCount() <= 0) {
        std::fprintf(stderr, "error: no chunks for this video — run --create-chunks first\n");
        return 1;
    }
    std::fprintf(stdout, "match #%d, %d chunks, window frame %d .. %d\n",
                 match.matchId(), match.chunkCount(),
                 match.matchStartFrame(), match.matchEndFrame());
    std::fflush(stdout);

    QObject::connect(&match, &MatchManager::opStateChanged, [&match]() {
        static int lastBucket = -1;
        if (match.opRunning()) {
            const int pct = static_cast<int>(match.opProgress() * 100);
            if (pct / 5 != lastBucket) {
                lastBucket = pct / 5;
                std::fprintf(stdout, "%3d%%  %s\n", pct, qPrintable(match.opLabel()));
                std::fflush(stdout);
            }
        } else if (!match.lastError().isEmpty()) {
            std::fprintf(stderr, "error: %s\n", qPrintable(match.lastError()));
            std::fflush(stderr);
            QCoreApplication::exit(1);
        } else {
            std::fprintf(stdout, "done\n");
            std::fflush(stdout);
            QCoreApplication::exit(0);
        }
    });

    if (op == QLatin1String("track"))
        match.trackChunks();
    else
        match.createChunks();
    if (!match.opRunning())
        return 1;
    return QCoreApplication::exec();
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PepeTrack"));
    app.setOrganizationName(QStringLiteral("pepemxl"));

    {
        const QStringList args = app.arguments();
        if (args.size() >= 3 && args.at(1) == QLatin1String("--extract-lineups"))
            return runExtractLineups(args.at(2));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--dump-exclusions"))
            return runDumpExclusions(args.at(2));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--track-chunks"))
            return runMatchOp(args.at(2), QStringLiteral("track"));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--create-chunks"))
            return runMatchOp(args.at(2), QStringLiteral("chunk"));
    }

    // Basic style so QML can fully restyle the controls.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AppController controller;

    QQmlApplicationEngine engine;
    // The engine takes ownership of the provider.
    engine.addImageProvider(QStringLiteral("videoframe"), controller.frameProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("PepeTrack", "Main");

    // Optional: open a video passed on the command line.
    const QStringList args = app.arguments();
    if (args.size() > 1) {
        controller.openVideo(QUrl::fromLocalFile(args.at(1)));
    }

    return app.exec();
}
