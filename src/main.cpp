#include "AppController.h"
#include "FrameProvider.h"
#include "HomographyManager.h"
#include "HomographyWorker.h"
#include "LineCalibrator.h"
#include "MatchManager.h"
#include "TrackingManager.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QVariantMap>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
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
// Optional matchId/videoId resolve one specific project video entry
// (duplicated paths = several camera views of the same file).
static int runDumpExclusions(const QString &videoPath, int matchId = 0, int videoId = 0)
{
    cv::VideoCapture cap(videoPath.toStdString());
    double fps = cap.isOpened() ? cap.get(cv::CAP_PROP_FPS) : 25.0;
    if (fps <= 0.0 || fps > 240.0) fps = 25.0;
    const int total = cap.isOpened()
        ? static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)) : 0;
    cap.release();

    MatchManager match;
    if (matchId > 0 && videoId > 0)
        match.prepareOpenVideo(matchId, videoId);
    match.setVideo(videoPath, fps, total);
    std::fprintf(stdout, "match #%d video #%d (%s, %s), fps %.2f, %d frames\n",
                 match.matchId(), match.videoId(),
                 qPrintable(match.videoRole()), qPrintable(match.videoSegment()),
                 fps, total);
    if (match.hasCrop()) {
        const QRect c = match.crop();
        std::fprintf(stdout, "view crop: %dx%d @ (%d,%d)\n",
                     c.width(), c.height(), c.x(), c.y());
    }
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
    else if (op == QLatin1String("preprocess"))
        match.preprocess();
    else
        match.createChunks();
    if (!match.opRunning())
        return 1;
    return QCoreApplication::exec();
}

// Headless: propagate the manual track assignments across all chunks and
// write the per-chunk metadata files.
static int runInferIds(const QString &videoPath)
{
    cv::VideoCapture cap(videoPath.toStdString());
    double fps = cap.isOpened() ? cap.get(cv::CAP_PROP_FPS) : 25.0;
    if (fps <= 0.0 || fps > 240.0) fps = 25.0;
    const int total = cap.isOpened()
        ? static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)) : 0;
    cap.release();

    MatchManager match;
    match.setVideo(videoPath, fps, total);
    TrackingManager tracking;
    tracking.setSource(videoPath);
    tracking.loadFromChunkCsvs(match.chunksDir(), match.chunksMetadataDir(),
                               match.assignmentsPath(),
                               total / fps, match.excludedRangesSec());
    if (!tracking.hasDetections()) {
        std::fprintf(stderr, "error: no chunk tracking data\n");
        return 1;
    }
    const int inferred = tracking.inferIdentities(true, 0.0);
    std::fprintf(stdout, "inferred %d identities from manual assignments\n", inferred);
    std::fflush(stdout);
    return 0;
}

// Headless: run the live Tracking-tab inference (same code path, queued
// snapshots included) so memory/CPU behavior can be profiled externally.
static int runLiveTracking(const QString &videoPath)
{
    cv::VideoCapture cap(videoPath.toStdString());
    double fps = cap.isOpened() ? cap.get(cv::CAP_PROP_FPS) : 25.0;
    if (fps <= 0.0 || fps > 240.0) fps = 25.0;
    const int total = cap.isOpened()
        ? static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)) : 0;
    cap.release();

    MatchManager match;
    match.setVideo(videoPath, fps, total);
    TrackingManager tracking;
    tracking.setSource(videoPath);
    tracking.setExclusions(match.excludedFrameRanges());

    QObject::connect(&tracking, &TrackingManager::snapshotChanged, [&tracking]() {
        std::fprintf(stdout, "progress %.1f%%  tracked %d\n",
                     tracking.progress() * 100.0, tracking.playersTracked());
        std::fflush(stdout);
    });
    QObject::connect(&tracking, &TrackingManager::runningChanged, [&tracking]() {
        if (!tracking.isRunningInference())
            QCoreApplication::exit(0);
    });
    tracking.toggleRun();
    return QCoreApplication::exec();
}

// Headless: register a video as a NEW entry of an existing project
// ("pepe --add-video <video> <matchId> <role> <segment>"). The same file
// may repeat (another camera view). Mirrors the GUI flow: the project
// must be resolvable from the path (or already contain it).
static int runAddVideo(const QString &videoPath, int matchId,
                       const QString &role, const QString &segment)
{
    cv::VideoCapture cap(videoPath.toStdString());
    double fps = cap.isOpened() ? cap.get(cv::CAP_PROP_FPS) : 25.0;
    if (fps <= 0.0 || fps > 240.0) fps = 25.0;
    const int total = cap.isOpened()
        ? static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)) : 0;
    cap.release();

    MatchManager match;
    match.setVideo(videoPath, fps, total);   // make a project current
    if (match.matchId() != matchId) {
        std::fprintf(stderr, "error: path resolves to match #%d, not #%d\n",
                     match.matchId(), matchId);
        return 1;
    }
    match.prepareAddVideo(role, segment);    // same flow as the GUI menu
    match.setVideo(videoPath, fps, total);
    std::fprintf(stdout, "match #%d video #%d (%s, %s)\n",
                 match.matchId(), match.videoId(),
                 qPrintable(match.videoRole()), qPrintable(match.videoSegment()));
    std::fflush(stdout);
    return 0;
}

// Headless: run the phase-F2 inter-frame homography propagation on an
// already-calibrated video (>=2 keyframes in <video>_project/project.json)
// and report the dense track ("pepe --propagate-homography <video>").
static int runPropagateHomography(const QString &videoPath)
{
    const QFileInfo info(videoPath);
    const QString dir = info.dir().filePath(info.completeBaseName() + QStringLiteral("_project"));
    QFile pf(QDir(dir).filePath(QStringLiteral("project.json")));
    if (!pf.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "error: no project.json at %s\n", qPrintable(dir));
        return 1;
    }
    const QJsonObject root = QJsonDocument::fromJson(pf.readAll()).object();
    pf.close();

    HomographyManager hm;
    hm.fromJson(root[QStringLiteral("homography")].toObject());
    const auto &kfs = hm.keyframeData();
    if (kfs.size() < 2) {
        std::fprintf(stderr, "error: need >=2 keyframes (have %d)\n",
                     static_cast<int>(kfs.size()));
        return 1;
    }
    std::fprintf(stdout, "%d keyframes, span frame %d .. %d\n",
                 static_cast<int>(kfs.size()), kfs.first().frame, kfs.last().frame);

    QVector<HomographyWorker::Keyframe> wkfs;
    for (const auto &k : kfs) {
        HomographyWorker::Keyframe wk;
        wk.frame = k.frame;
        for (int j = 0; j < 4; ++j) wk.img[j] = k.image[j];
        wkfs.append(wk);
    }
    const QString outPath = QDir(dir).filePath(QStringLiteral("homography_dense.bin"));

    HomographyWorker worker;
    QObject::connect(&worker, &HomographyWorker::progressChanged,
                     [](double frac, const QString &label) {
        static int last = -1;
        const int pct = static_cast<int>(frac * 100);
        if (pct / 10 != last) { last = pct / 10;
            std::fprintf(stdout, "%3d%%  %s\n", pct, qPrintable(label)); std::fflush(stdout); }
    });
    QObject::connect(&worker, &HomographyWorker::finished, &hm,
                     [&](bool ok, const QString &err, int start, int count) {
        if (!ok) {
            std::fprintf(stderr, "error: %s\n", qPrintable(err));
            QCoreApplication::exit(1);
            return;
        }
        std::fprintf(stdout, "dense track: %d frames from %d -> %s\n",
                     count, start, qPrintable(outPath));
        hm.loadDenseTrack(outPath);

        // Sanity: dense endpoints must match the keyframes; sample the span
        // to show propagated vs linear-interpolated divergence.
        auto lerp = [&](int f, QPointF out[4]) {
            const auto &a = wkfs.first(); const auto &b = wkfs.last();
            const double t = double(f - a.frame) / double(b.frame - a.frame);
            for (int j = 0; j < 4; ++j) out[j] = a.img[j] * (1.0 - t) + b.img[j] * t;
        };
        const int f0 = wkfs.first().frame, f1 = wkfs.last().frame;
        for (int s = 0; s <= 4; ++s) {
            const int f = f0 + (f1 - f0) * s / 4;
            QPointF li[4]; lerp(f, li);
            // Propagated point A via imageToPitchAt round-trip is indirect;
            // instead read back the dense point A by mapping pitch(0,0).
            const QPointF pit = hm.imageToPitchAt(f, li[0].x(), li[0].y());
            std::fprintf(stdout, "  frame %5d  linearA=(%.1f,%.1f)  pitchOfLinearA=(%.2f,%.2f)m\n",
                         f, li[0].x(), li[0].y(), pit.x(), pit.y());
        }
        std::fflush(stdout);
        QCoreApplication::exit(0);
    });

    worker.configure(videoPath, wkfs, outPath);
    worker.start();
    return QCoreApplication::exec();
}

// Headless: line-based calibration on one frame, dumping debug overlays to a
// directory ("pepe --calibrate-lines <video> <frame> <outDir> [ax ay bx by cx
// cy dx dy]"). With the 8 optional image coords (A,B,C,D -> pitch corners) it
// builds the initial H and reports the refinement; without them it just dumps
// the frame + detected-line overlay so points can be picked.
static int runCalibrateLines(const QStringList &args)
{
    const QString videoPath = args.at(2);
    const int frame = args.at(3).toInt();
    const QString outDir = args.at(4);
    QDir().mkpath(outDir);

    cv::VideoCapture cap(videoPath.toStdString());
    if (!cap.isOpened()) { std::fprintf(stderr, "error: cannot open video\n"); return 1; }
    cap.set(cv::CAP_PROP_POS_FRAMES, frame);
    cv::Mat bgr;
    if (!cap.read(bgr) || bgr.empty()) { std::fprintf(stderr, "error: cannot read frame\n"); return 1; }

    const std::vector<cv::Vec4f> lines = LineCalibrator::detectLines(bgr, {});
    std::fprintf(stdout, "frame %d  %dx%d  detected %d line segments\n",
                 frame, bgr.cols, bgr.rows, static_cast<int>(lines.size()));

    cv::Mat overlay = bgr.clone();
    for (const cv::Vec4f &l : lines)
        cv::line(overlay, {cvRound(l[0]), cvRound(l[1])}, {cvRound(l[2]), cvRound(l[3])},
                 {0, 255, 0}, 2, cv::LINE_AA);

    auto drawModel = [&](const cv::Mat &H, const cv::Scalar &color) {
        if (H.empty()) return;
        const cv::Mat Hinv = H.inv();
        for (const cv::Vec4f &s : LineCalibrator::pitchModelSegments()) {
            std::vector<cv::Point2f> pit{{s[0], s[1]}, {s[2], s[3]}}, im;
            cv::perspectiveTransform(pit, im, Hinv);
            cv::line(overlay, im[0], im[1], color, 2, cv::LINE_AA);
        }
    };

    // Optional init correspondences: 4x (imgX imgY pitchX pitchY) after outDir.
    if (args.size() >= 21) {
        HomographyManager hm;
        hm.setImageSize(bgr.cols, bgr.rows);
        const char *ids[4] = {"A", "B", "C", "D"};
        for (int i = 0; i < 4; ++i) {
            const int b = 5 + i * 4;
            hm.setPitchPoint(QString::fromLatin1(ids[i]),
                             args.at(b + 2).toDouble(), args.at(b + 3).toDouble());
            hm.setImagePoint(QString::fromLatin1(ids[i]),
                             args.at(b + 0).toDouble(), args.at(b + 1).toDouble());
        }
        hm.recompute();
        const cv::Mat Hinit = hm.homographyAt(frame);

        const LineCalibrator::Result res = LineCalibrator::refine(Hinit, lines);
        std::fprintf(stdout, "refine: ok=%d  inliers=%d  initErr=%.1fpx  reprojErr=%.1fpx\n",
                     res.ok, res.inliers, res.initErr, res.reprojErr);
        drawModel(Hinit, {40, 40, 255});    // init model = red
        if (res.ok) drawModel(res.H, {0, 255, 255}); // refined = yellow
    }

    // This OpenCV build has no PNG codec (imgcodecs), so save via QImage.
    auto savePng = [](const cv::Mat &m, const QString &path) {
        cv::Mat rgb;
        cv::cvtColor(m, rgb, cv::COLOR_BGR2RGB);
        const QImage img(rgb.data, rgb.cols, rgb.rows,
                         static_cast<int>(rgb.step), QImage::Format_RGB888);
        img.copy().save(path);
    };
    // Grid on a copy of the raw frame to help pick pixel coordinates.
    cv::Mat grid = bgr.clone();
    for (int x = 0; x < grid.cols; x += 100) {
        cv::line(grid, {x, 0}, {x, grid.rows}, {0, 0, 0}, x % 200 == 0 ? 1 : 1);
        if (x % 200 == 0)
            cv::putText(grid, std::to_string(x), {x + 2, 22}, cv::FONT_HERSHEY_SIMPLEX,
                        0.5, {0, 255, 255}, 1, cv::LINE_AA);
    }
    for (int y = 0; y < grid.rows; y += 100) {
        cv::line(grid, {0, y}, {grid.cols, y}, {0, 0, 0}, 1);
        if (y % 200 == 0)
            cv::putText(grid, std::to_string(y), {4, y + 18}, cv::FONT_HERSHEY_SIMPLEX,
                        0.5, {0, 255, 255}, 1, cv::LINE_AA);
    }

    const QString framePng = QDir(outDir).filePath(QStringLiteral("frame.png"));
    const QString ovPng = QDir(outDir).filePath(QStringLiteral("overlay.png"));
    const QString gridPng = QDir(outDir).filePath(QStringLiteral("grid.png"));
    savePng(bgr, framePng);
    savePng(overlay, ovPng);
    savePng(grid, gridPng);
    std::fprintf(stdout, "wrote %s , %s\n", qPrintable(framePng), qPrintable(ovPng));
    std::fflush(stdout);
    return 0;
}

// Headless: synthetic recovery test for the line-refinement math. Builds a
// ground-truth H, renders the pitch model into image-space "detected" lines
// (with noise + outliers), perturbs the H, and checks refine() recovers it.
static int runCalibrateSelftest()
{
    auto buildH = [](const double img[8]) {
        HomographyManager hm;
        hm.setImageSize(1920, 1080);
        const char *ids[4] = {"A", "B", "C", "D"};
        const double pitch[8] = {0, 0, 105, 0, 105, 68, 0, 68};
        for (int i = 0; i < 4; ++i) {
            hm.setPitchPoint(QString::fromLatin1(ids[i]), pitch[i * 2], pitch[i * 2 + 1]);
            hm.setImagePoint(QString::fromLatin1(ids[i]), img[i * 2], img[i * 2 + 1]);
        }
        hm.recompute();
        return hm.homographyAt(0);
    };

    // Ground-truth wide view (full pitch visible).
    const double trueImg[8] = {300, 300, 1620, 300, 1750, 1000, 170, 1000};
    const cv::Mat Htrue = buildH(trueImg);
    const cv::Mat HtrueInv = Htrue.inv();

    cv::RNG rng(1234);
    std::vector<cv::Vec4f> lines;
    for (const cv::Vec4f &s : LineCalibrator::pitchModelSegments()) {
        std::vector<cv::Point2f> pit{{s[0], s[1]}, {s[2], s[3]}}, im;
        cv::perspectiveTransform(pit, im, HtrueInv);
        lines.push_back({im[0].x + (float)rng.gaussian(0.8), im[0].y + (float)rng.gaussian(0.8),
                         im[1].x + (float)rng.gaussian(0.8), im[1].y + (float)rng.gaussian(0.8)});
    }
    for (int k = 0; k < 15; ++k)   // outlier clutter
        lines.push_back({(float)rng.uniform(0, 1920), (float)rng.uniform(0, 1080),
                         (float)rng.uniform(0, 1920), (float)rng.uniform(0, 1080)});

    // Perturbed initial homography.
    double initImg[8];
    for (int i = 0; i < 8; ++i) initImg[i] = trueImg[i] + rng.uniform(-28.0, 28.0);
    const cv::Mat Hinit = buildH(initImg);

    const LineCalibrator::Result res = LineCalibrator::refine(Hinit, lines);

    // Corner-recovery error: pitch corners projected to image via each H.
    auto cornerErr = [&](const cv::Mat &H) {
        std::vector<cv::Point2f> pit{{0, 0}, {105, 0}, {105, 68}, {0, 68}}, a, b;
        cv::perspectiveTransform(pit, a, H.inv());
        cv::perspectiveTransform(pit, b, HtrueInv);
        double e = 0.0;
        for (int i = 0; i < 4; ++i) e += cv::norm(a[i] - b[i]);
        return e / 4.0;
    };
    std::fprintf(stdout, "selftest: %d lines (+15 outliers)\n", (int)lines.size() - 15);
    std::fprintf(stdout, "  init  corner err = %.1f px\n", cornerErr(Hinit));
    if (res.ok)
        std::fprintf(stdout, "  refined corner err = %.1f px  (inliers %d, reproj %.2f px)  OK\n",
                     cornerErr(res.H), res.inliers, res.reprojErr);
    else
        std::fprintf(stdout, "  refine FAILED (inliers %d, reproj %.2f px)\n",
                     res.inliers, res.reprojErr);
    std::fflush(stdout);
    return res.ok && cornerErr(res.H) < 10.0 ? 0 : 1;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PepeTrack"));
    app.setOrganizationName(QStringLiteral("pepemxl"));

    {
        const QStringList args = app.arguments();
        if (args.size() >= 6 && args.at(1) == QLatin1String("--add-video"))
            return runAddVideo(args.at(2), args.at(3).toInt(), args.at(4), args.at(5));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--live-track"))
            return runLiveTracking(args.at(2));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--infer-ids"))
            return runInferIds(args.at(2));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--extract-lineups"))
            return runExtractLineups(args.at(2));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--dump-exclusions"))
            return runDumpExclusions(args.at(2),
                                     args.size() > 3 ? args.at(3).toInt() : 0,
                                     args.size() > 4 ? args.at(4).toInt() : 0);
        if (args.size() >= 3 && args.at(1) == QLatin1String("--track-chunks"))
            return runMatchOp(args.at(2), QStringLiteral("track"));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--create-chunks"))
            return runMatchOp(args.at(2), QStringLiteral("chunk"));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--preprocess"))
            return runMatchOp(args.at(2), QStringLiteral("preprocess"));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--propagate-homography"))
            return runPropagateHomography(args.at(2));
        if (args.size() >= 5 && args.at(1) == QLatin1String("--calibrate-lines"))
            return runCalibrateLines(args);
        if (args.size() >= 2 && args.at(1) == QLatin1String("--calibrate-selftest"))
            return runCalibrateSelftest();
    }

    // Basic style so QML can fully restyle the controls.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AppController controller;

    QQmlApplicationEngine engine;
    // The engine takes ownership of the provider.
    engine.addImageProvider(QStringLiteral("videoframe"), controller.frameProvider());
    engine.addImageProvider(QStringLiteral("videoframe2"), controller.frameProvider2());
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
