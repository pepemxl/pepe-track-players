#include "AppController.h"
#include "FrameProvider.h"
#include "HomographyManager.h"
#include "HomographyWorker.h"
#include "LineCalibrator.h"
#include "ShotDetector.h"
#include "LineupPositionExtractor.h"
#include "MaskGenerator.h"
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

        // Read the raw dense .bin for smoothness (jitter) + confidence stats.
        QFile bin(outPath);
        if (bin.open(QIODevice::ReadOnly)) {
            QDataStream d(&bin);
            d.setByteOrder(QDataStream::LittleEndian);
            d.setFloatingPointPrecision(QDataStream::DoublePrecision);
            qint32 magic = 0, st = 0, cnt = 0;
            d >> magic;
            const bool v2 = (magic == -2);
            if (v2) d >> st; else st = magic;
            d >> cnt;
            QVector<QPointF> A;    // point A per frame
            QVector<double> cf;
            for (int i = 0; i < cnt; ++i) {
                double x = 0, y = 0;
                for (int k = 0; k < 4; ++k) { double px, py; d >> px >> py; if (k == 0) { x = px; y = py; } }
                double c = 1.0; if (v2) d >> c;
                A.append(QPointF(x, y)); cf.append(c);
            }
            // Mean |second difference| of A (jitter); lower = smoother.
            double jit = 0.0; int jn = 0;
            for (int i = 1; i + 1 < A.size(); ++i) {
                if (cf[i] <= 0 || cf[i - 1] <= 0 || cf[i + 1] <= 0) continue;
                const QPointF s = A[i + 1] - 2.0 * A[i] + A[i - 1];
                jit += std::hypot(s.x(), s.y()); ++jn;
            }
            double cmin = 1.0, cmean = 0.0; int lowc = 0;
            for (double c : cf) { cmin = std::min(cmin, c); cmean += c; if (c < 0.35) ++lowc; }
            cmean = cf.isEmpty() ? 0.0 : cmean / cf.size();
            std::fprintf(stdout, "smoothing: jitter(A) = %.3f px  |  confidence: min=%.2f mean=%.2f  low(<0.35)=%d frames\n",
                         jn ? jit / jn : 0.0, cmin, cmean, lowc);
        }

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

    worker.configure(videoPath, wkfs, outPath, {}, hm.graphicsRects());
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
    // Both solver backends must converge and reject the outlier lines; the
    // exact corner error differs by backend (the custom estimator refits
    // algebraically on all inliers each ICP step and tends to be tighter here).
    return res.ok && cornerErr(res.H) < 20.0 ? 0 : 1;
}

// Headless: regression for the bug where assigning a pitch landmark reset the
// image points placed in the session ("pepe --homography-pitchpoint-test").
static int runHomographyPitchPointTest()
{
    HomographyManager hm;
    hm.setImageSize(1920, 1080);
    // Calibrate + commit a keyframe at frame 0 (so m_keyframes is not empty).
    hm.setImagePoint(QStringLiteral("A"), 100, 100);
    hm.setImagePoint(QStringLiteral("B"), 1800, 100);
    hm.setImagePoint(QStringLiteral("C"), 1800, 1000);
    hm.setImagePoint(QStringLiteral("D"), 100, 1000);
    hm.recompute();

    // The user now re-places the image points in this session.
    hm.setImagePoint(QStringLiteral("A"), 200, 200);
    hm.setImagePoint(QStringLiteral("B"), 1700, 150);
    hm.setImagePoint(QStringLiteral("C"), 1750, 950);
    hm.setImagePoint(QStringLiteral("D"), 150, 900);

    // Assign a pitch landmark to B — the action that used to reset the points.
    hm.setPitchLandmark(QStringLiteral("B"), QStringLiteral("half_top"));

    auto imgOf = [&](const QString &id, double &ix, double &iy) {
        for (const QVariant &v : hm.points()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("id")).toString() == id) {
                ix = m.value(QStringLiteral("ix")).toDouble();
                iy = m.value(QStringLiteral("iy")).toDouble();
                return;
            }
        }
    };
    double bx = 0, by = 0, dx = 0, dy = 0;
    imgOf(QStringLiteral("B"), bx, by);
    imgOf(QStringLiteral("D"), dx, dy);
    std::fprintf(stdout, "after setPitchLandmark(B): B=(%.0f,%.0f) D=(%.0f,%.0f)\n", bx, by, dx, dy);
    const bool ok = std::abs(bx - 1700) < 1 && std::abs(by - 150) < 1
                 && std::abs(dx - 150) < 1 && std::abs(dy - 900) < 1;
    std::fprintf(stdout, "%s\n", ok ? "PASS: session image points preserved"
                                     : "FAIL: image points were reset by the pitch-point change");
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// Headless: verify the reference-pitch landmark catalog contains the added
// points — the four centre-circle cardinal points and the penalty-area front
// line extended to both touchlines ("pepe --landmark-catalog-test"). Also
// checks setPitchLandmark resolves one of the new keys to the right pitch
// coordinate.
static int runLandmarkCatalogTest()
{
    HomographyManager hm;
    const QVariantList lms = hm.pitchLandmarks();

    // Build key -> (px,py) lookup and confirm keys are unique.
    QMap<QString, QPointF> byKey;
    bool dup = false;
    for (const QVariant &v : lms) {
        const QVariantMap m = v.toMap();
        const QString k = m.value(QStringLiteral("key")).toString();
        if (byKey.contains(k)) dup = true;
        byKey.insert(k, QPointF(m.value(QStringLiteral("px")).toDouble(),
                                m.value(QStringLiteral("py")).toDouble()));
    }

    struct Expect { const char *key; double px, py; };
    const Expect want[] = {
        // Centre circle: left/right complete the four cardinal points (radius
        // 9.15 m about the 52.5,34 centre); top/bottom already existed.
        { "circ_top",   52.5,  24.85 },
        { "circ_bot",   52.5,  43.15 },
        { "circ_left",  43.35, 34.0 },
        { "circ_right", 61.65, 34.0 },
        // Penalty-area front line (x = 16.5 / 88.5) extended to the touchlines.
        { "lpa_front_top", 16.5, 0.0 },
        { "lpa_front_bot", 16.5, 68.0 },
        { "rpa_front_top", 88.5, 0.0 },
        { "rpa_front_bot", 88.5, 68.0 },
    };

    bool ok = !dup;
    if (dup) std::fprintf(stdout, "FAIL: duplicate landmark key in catalog\n");
    for (const Expect &e : want) {
        const QString k = QString::fromLatin1(e.key);
        if (!byKey.contains(k)) {
            std::fprintf(stdout, "FAIL: missing landmark '%s'\n", e.key);
            ok = false;
            continue;
        }
        const QPointF p = byKey.value(k);
        const bool match = std::abs(p.x() - e.px) < 0.01 && std::abs(p.y() - e.py) < 0.01;
        std::fprintf(stdout, "  %-14s (%.2f, %.2f) %s\n", e.key, p.x(), p.y(),
                     match ? "ok" : "WRONG");
        if (!match) ok = false;
    }

    // setPitchLandmark must resolve a new key to its catalogue coordinate.
    hm.setPitchLandmark(QStringLiteral("A"), QStringLiteral("rpa_front_bot"));
    double apx = -1, apy = -1;
    for (const QVariant &v : hm.points()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == QLatin1String("A")) {
            apx = m.value(QStringLiteral("px")).toDouble();
            apy = m.value(QStringLiteral("py")).toDouble();
        }
    }
    const bool assigned = std::abs(apx - 88.5) < 0.01 && std::abs(apy - 68.0) < 0.01;
    std::fprintf(stdout, "  setPitchLandmark(A, rpa_front_bot) -> (%.2f, %.2f) %s\n",
                 apx, apy, assigned ? "ok" : "WRONG");
    ok = ok && assigned;

    std::fprintf(stdout, "%s\n", ok ? "PASS: landmark catalog has the new points"
                                     : "FAIL: landmark catalog check failed");
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// Headless: two keyframes reference DIFFERENT pitch landmarks; verify each
// keyframe keeps its own A/B/C/D assignment, that it drives the right H, and
// that it survives a toJson/fromJson round-trip ("pepe --keyframe-pitch-test").
static int runKeyframePitchTest()
{
    HomographyManager hm;
    hm.setImageSize(1920, 1080);

    // Keyframe 1 @ frame 100: A/B/C/D on the field corners (defaults).
    hm.setCurrentFrame(100);
    hm.setImagePoint(QStringLiteral("A"), 300, 180);
    hm.setImagePoint(QStringLiteral("B"), 1650, 170);
    hm.setImagePoint(QStringLiteral("C"), 1850, 980);
    hm.setImagePoint(QStringLiteral("D"), 120, 990);
    hm.recompute();

    // Keyframe 2 @ frame 200: reassign the four points to different landmarks
    // (left box + centre circle) — a panned camera showing another region.
    hm.setCurrentFrame(200);
    hm.setImagePoint(QStringLiteral("A"), 400, 200);
    hm.setImagePoint(QStringLiteral("B"), 1400, 220);
    hm.setImagePoint(QStringLiteral("C"), 1500, 900);
    hm.setImagePoint(QStringLiteral("D"), 260, 880);
    hm.setPitchLandmark(QStringLiteral("A"), QStringLiteral("lpa_tl"));   // 0,13.84
    hm.setPitchLandmark(QStringLiteral("B"), QStringLiteral("circ_top")); // 52.5,24.85
    hm.setPitchLandmark(QStringLiteral("C"), QStringLiteral("circ_bot")); // 52.5,43.15
    hm.setPitchLandmark(QStringLiteral("D"), QStringLiteral("lpa_bl"));   // 0,54.16
    hm.recompute();

    auto pitchOf = [](const QVariantList &pts, const QString &id, QPointF *p) {
        for (const QVariant &v : pts) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("id")).toString() == id) {
                *p = QPointF(m.value(QStringLiteral("px")).toDouble(),
                             m.value(QStringLiteral("py")).toDouble());
                return;
            }
        }
    };
    auto check = [&](const QString &tag) {
        // Land on keyframe 1: should show the corner assignment.
        hm.setCurrentFrame(100);
        QPointF a1; pitchOf(hm.points(), QStringLiteral("A"), &a1);
        // Land on keyframe 2: should show the reassigned landmark.
        hm.setCurrentFrame(200);
        QPointF b2; pitchOf(hm.points(), QStringLiteral("B"), &b2);
        const bool kf1ok = std::abs(a1.x() - 0.0) < 0.01 && std::abs(a1.y() - 0.0) < 0.01;
        const bool kf2ok = std::abs(b2.x() - 52.5) < 0.01 && std::abs(b2.y() - 24.85) < 0.01;
        std::fprintf(stdout, "[%s] KF100 A=(%.2f,%.2f) %s | KF200 B=(%.2f,%.2f) %s\n",
                     qPrintable(tag), a1.x(), a1.y(), kf1ok ? "ok" : "WRONG",
                     b2.x(), b2.y(), kf2ok ? "ok" : "WRONG");
        return kf1ok && kf2ok;
    };

    const bool live = check(QStringLiteral("live"));

    // Round-trip through JSON.
    const QJsonObject json = hm.toJson();
    HomographyManager hm2;
    hm2.setImageSize(1920, 1080);
    hm2.fromJson(json);
    QPointF a1, b2;
    hm2.setCurrentFrame(100); pitchOf(hm2.points(), QStringLiteral("A"), &a1);
    hm2.setCurrentFrame(200); pitchOf(hm2.points(), QStringLiteral("B"), &b2);
    const bool persisted = std::abs(a1.x()) < 0.01 && std::abs(a1.y()) < 0.01
                        && std::abs(b2.x() - 52.5) < 0.01 && std::abs(b2.y() - 24.85) < 0.01;
    std::fprintf(stdout, "[reload] KF100 A=(%.2f,%.2f) | KF200 B=(%.2f,%.2f) %s\n",
                 a1.x(), a1.y(), b2.x(), b2.y(), persisted ? "ok" : "WRONG");

    const bool ok = live && persisted;
    std::fprintf(stdout, "%s\n", ok ? "PASS: per-keyframe pitch preserved and persisted"
                                     : "FAIL: per-keyframe pitch lost");
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// Headless: reproduce the user report — reassigning a reference-pitch landmark
// at a keyframe WITHOUT pressing "Set keyframe", then navigating between
// keyframes / reloading, and checking whether the choice sticks
// ("pepe --keyframe-pitch-nav-test").
static int runKeyframePitchNavTest()
{
    auto pxOf = [](HomographyManager &h, const QString &id) {
        for (const QVariant &v : h.points()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("id")).toString() == id)
                return QPointF(m.value(QStringLiteral("px")).toDouble(),
                               m.value(QStringLiteral("py")).toDouble());
        }
        return QPointF(-1, -1);
    };
    auto placeCorners = [](HomographyManager &h, int f) {
        h.setCurrentFrame(f);
        h.setImagePoint(QStringLiteral("A"), 300, 180);
        h.setImagePoint(QStringLiteral("B"), 1650, 170);
        h.setImagePoint(QStringLiteral("C"), 1850, 980);
        h.setImagePoint(QStringLiteral("D"), 120, 990);
        h.recompute();
    };

    bool allOk = true;

    // --- Scenario A: reassign exactly on a keyframe, no recompute ---
    {
        HomographyManager hm;
        hm.setImageSize(1920, 1080);
        placeCorners(hm, 100);
        placeCorners(hm, 500);
        hm.setCurrentFrame(100);
        hm.setPitchLandmark(QStringLiteral("A"), QStringLiteral("circ_top")); // (52.5,24.85)
        hm.setCurrentFrame(500);
        hm.setCurrentFrame(100);
        const QPointF a = pxOf(hm, QStringLiteral("A"));
        const bool navOk = std::abs(a.x() - 52.5) < 0.01 && std::abs(a.y() - 24.85) < 0.01;
        // Round-trip.
        HomographyManager hm2; hm2.setImageSize(1920, 1080); hm2.fromJson(hm.toJson());
        hm2.setCurrentFrame(100);
        const QPointF a2 = pxOf(hm2, QStringLiteral("A"));
        const bool rtOk = std::abs(a2.x() - 52.5) < 0.01 && std::abs(a2.y() - 24.85) < 0.01;
        std::fprintf(stdout, "[A on-keyframe no-recompute] nav A=(%.2f,%.2f) %s | reload A=(%.2f,%.2f) %s\n",
                     a.x(), a.y(), navOk ? "ok" : "REVERTED",
                     a2.x(), a2.y(), rtOk ? "ok" : "REVERTED");
        allOk = allOk && navOk && rtOk;
    }

    // --- Scenario B: reassign one frame OFF the keyframe (imprecise seek) ---
    {
        HomographyManager hm;
        hm.setImageSize(1920, 1080);
        placeCorners(hm, 100);
        placeCorners(hm, 500);
        hm.setCurrentFrame(101);   // user thinks they are "on" keyframe 100
        hm.setPitchLandmark(QStringLiteral("A"), QStringLiteral("circ_top"));
        hm.setCurrentFrame(500);
        hm.setCurrentFrame(100);
        const QPointF a = pxOf(hm, QStringLiteral("A"));
        const bool stuck = std::abs(a.x() - 52.5) < 0.01 && std::abs(a.y() - 24.85) < 0.01;
        // Round-trip too.
        HomographyManager hm2; hm2.setImageSize(1920, 1080); hm2.fromJson(hm.toJson());
        hm2.setCurrentFrame(100);
        const QPointF a2 = pxOf(hm2, QStringLiteral("A"));
        const bool rtOk = std::abs(a2.x() - 52.5) < 0.01 && std::abs(a2.y() - 24.85) < 0.01;
        std::fprintf(stdout, "[B off-by-one] back-at-100 A=(%.2f,%.2f) %s | reload %s\n",
                     a.x(), a.y(), stuck ? "stuck" : "REVERTED",
                     rtOk ? "ok" : "REVERTED");
        allOk = allOk && stuck && rtOk;
    }

    std::fprintf(stdout, "%s\n", allOk ? "PASS: on-keyframe reassignment persists"
                                       : "FAIL: on-keyframe reassignment lost");
    std::fflush(stdout);
    return allOk ? 0 : 1;
}

// Headless: verify the pitch-model reprojection (pitch->image via H^-1)
// produces non-empty geometry for a plausible camera ("pepe
// --reproject-model-test").
static int runReprojectModelTest()
{
    HomographyManager hm;
    hm.setImageSize(1920, 1080);
    // A trapezoid mapping the field corners to a broadcast-like view.
    hm.setImagePoint(QStringLiteral("A"), 300, 180);
    hm.setImagePoint(QStringLiteral("B"), 1650, 170);
    hm.setImagePoint(QStringLiteral("C"), 1850, 980);
    hm.setImagePoint(QStringLiteral("D"), 120, 990);
    hm.recompute();

    const QVariantMap m = hm.projectedPitchModel(0);
    const bool valid = m.value(QStringLiteral("valid")).toBool();
    const QVariantList lines = m.value(QStringLiteral("lines")).toList();
    const QVariantList points = m.value(QStringLiteral("points")).toList();
    int verts = 0;
    for (const QVariant &l : lines) verts += l.toList().size();
    std::fprintf(stdout, "valid=%d  lines=%d (%d verts)  points=%d\n",
                 valid ? 1 : 0, static_cast<int>(lines.size()), verts,
                 static_cast<int>(points.size()));

    // Round-trip a couple of landmarks: pitch -> image -> pitch.
    const QPointF centre = hm.pitchToImage(52.5, 34.0);
    const QPointF back = hm.imageToPitch(centre.x(), centre.y());
    std::fprintf(stdout, "centre spot -> img(%.1f,%.1f) -> pitch(%.2f,%.2f)\n",
                 centre.x(), centre.y(), back.x(), back.y());
    const bool rt = std::abs(back.x() - 52.5) < 0.5 && std::abs(back.y() - 34.0) < 0.5;

    const bool ok = valid && !lines.isEmpty() && verts > 20
                 && points.size() > 10 && centre.x() > 0 && rt;
    std::fprintf(stdout, "%s\n", ok ? "PASS: reprojection produced geometry"
                                     : "FAIL: reprojection geometry missing/wrong");
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// Headless: shot segmentation over a frame range ("pepe --detect-shots <video>
// <startFrame> <count>"), printing the shots and writing shots.json.
static int runDetectShots(const QString &videoPath, int startFrame, int count)
{
    const int endFrame = count > 0 ? startFrame + count : 0;
    const QVector<ShotDetector::Shot> shots = ShotDetector::detectSync(
        videoPath, startFrame, endFrame,
        [](double f, const QString &l) {
            static int last = -1;
            const int pct = static_cast<int>(f * 100);
            if (pct / 20 != last) { last = pct / 20;
                std::fprintf(stdout, "%3d%%  %s\n", pct, qPrintable(l)); std::fflush(stdout); }
        },
        nullptr);
    if (shots.isEmpty()) { std::fprintf(stderr, "error: no shots\n"); return 1; }

    const QFileInfo info(videoPath);
    const QString dir = info.dir().filePath(info.completeBaseName() + QStringLiteral("_project"));
    QDir().mkpath(dir);
    ShotDetector::save(QDir(dir).filePath(QStringLiteral("shots.json")), shots);

    int pitchCount = 0;
    for (const auto &s : shots) if (s.pitch) ++pitchCount;
    std::fprintf(stdout, "%d shots (%d pitch, %d non-pitch):\n",
                 static_cast<int>(shots.size()), pitchCount,
                 static_cast<int>(shots.size()) - pitchCount);
    for (const auto &s : shots)
        std::fprintf(stdout, "  [%6d..%6d] %5d f  %-9s  grass=%.2f  cut=%.2f\n",
                     s.startFrame, s.endFrame, s.endFrame - s.startFrame + 1,
                     s.pitch ? "PITCH" : "non-pitch", s.grassMean, s.cutStrength);
    std::fflush(stdout);
    return 0;
}

// Headless: verify the player-mask mechanism used by HomographyWorker's flow
// estimation ("pepe --flow-mask-test <video> <frame> <x> <y> <w> <h>"). It
// runs goodFeaturesToTrack with and without a mask over the box and reports how
// many features land inside it — masked should be ~0.
static int runFlowMaskTest(const QString &videoPath, int frame, const cv::Rect &box)
{
    cv::VideoCapture cap(videoPath.toStdString());
    if (!cap.isOpened()) { std::fprintf(stderr, "error: cannot open video\n"); return 1; }
    cap.set(cv::CAP_PROP_POS_FRAMES, frame);
    cv::Mat bgr;
    if (!cap.read(bgr) || bgr.empty()) { std::fprintf(stderr, "error: cannot read frame\n"); return 1; }

    const double scale = (bgr.cols > 960.0 ? 960.0 / bgr.cols : 1.0);
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    if (scale < 1.0) cv::resize(gray, gray, cv::Size(), scale, scale, cv::INTER_AREA);

    // Same mask geometry as HomographyWorker::run.
    cv::Rect s(cvRound((box.x - box.width * 0.15) * scale),
               cvRound((box.y - box.height * 0.10) * scale),
               cvRound(box.width * 1.30 * scale),
               cvRound(box.height * 1.30 * scale));
    s &= cv::Rect(0, 0, gray.cols, gray.rows);
    cv::Mat mask(gray.size(), CV_8U, cv::Scalar(255));
    if (s.area() > 0) mask(s).setTo(0);

    auto inBox = [&](const std::vector<cv::Point2f> &pts) {
        int c = 0;
        for (const auto &p : pts) if (s.contains(cv::Point(cvRound(p.x), cvRound(p.y)))) ++c;
        return c;
    };
    std::vector<cv::Point2f> c0, cm;
    cv::goodFeaturesToTrack(gray, c0, 700, 0.01, 7);
    cv::goodFeaturesToTrack(gray, cm, 700, 0.01, 7, mask);
    std::fprintf(stdout, "frame %d  box(scaled)=%dx%d@(%d,%d)\n", frame, s.width, s.height, s.x, s.y);
    std::fprintf(stdout, "  no mask : %zu features, %d inside box\n", c0.size(), inBox(c0));
    std::fprintf(stdout, "  masked  : %zu features, %d inside box\n", cm.size(), inBox(cm));
    std::fflush(stdout);
    return inBox(cm) == 0 ? 0 : 1;
}

// Headless: generate feature masks over chunk videos
// ("pepe --gen-masks <green|static> <chunksDir> <outDir> [maxChunks]").
static int runGenMasks(const QString &kind, const QString &chunksDir,
                       const QString &outDir, int maxChunks)
{
    MaskGenerator gen;
    QVector<int> chunks;
    for (int i = 1; i <= maxChunks; ++i) chunks.append(i);   // empty = all
    gen.configure(kind == QLatin1String("static") ? MaskGenerator::Kind::Static
                                                   : MaskGenerator::Kind::Green,
                  chunksDir, chunks, outDir);
    bool ok = false; int written = -1; QString err;
    QObject::connect(&gen, &MaskGenerator::finished, &gen,
        [&](bool o, const QString &e, int w) { ok = o; err = e; written = w; },
        Qt::DirectConnection);
    QObject::connect(&gen, &MaskGenerator::progressChanged, &gen,
        [](double f, const QString &l) {
            static int last = -1; const int pct = static_cast<int>(f * 100);
            if (pct / 10 != last) { last = pct / 10;
                std::fprintf(stdout, "%3d%%  %s\n", pct, qPrintable(l)); std::fflush(stdout); }
        }, Qt::DirectConnection);
    gen.start();
    gen.wait();
    std::fprintf(stdout, "%s masks: ok=%d written=%d %s\n",
                 qPrintable(kind), ok ? 1 : 0, written, qPrintable(err));
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// Headless: majority-vote union of a match's per-chunk static masks, written
// as a PNG ("pepe --static-union <matchDir> <outPng>"). Verifies the mask that
// gets folded into the propagation flow's RANSAC exclusion.
static int runStaticUnion(const QString &matchDir, const QString &outPng)
{
    const cv::Mat u = MaskGenerator::unionStaticMasks(
        matchDir + QStringLiteral("/static_mask"));
    if (u.empty()) { std::fprintf(stderr, "error: no static masks / empty union\n"); return 1; }
    if (!cv::imwrite(outPng.toStdString(), u)) {
        std::fprintf(stderr, "error: could not write %s\n", qPrintable(outPng));
        return 1;
    }
    const double frac = double(cv::countNonZero(u)) / (u.rows * u.cols);
    std::fprintf(stdout, "static union: %dx%d  %.2f%% masked -> %s\n",
                 u.cols, u.rows, frac * 100.0, qPrintable(outPng));
    std::fflush(stdout);
    return 0;
}

// Headless: verify the static-mask RANSAC exclusion exactly as HomographyWorker
// applies it ("pepe --flow-static-test <video> <frame> <matchDir>"). Builds the
// static union, folds it into the 960-wide working frame like the worker, and
// reports how many good features land on the masked graphics — should be ~0.
static int runFlowStaticTest(const QString &videoPath, int frame, const QString &matchDir)
{
    const cv::Mat u = MaskGenerator::unionStaticMasks(matchDir + QStringLiteral("/static_mask"));
    if (u.empty()) { std::fprintf(stderr, "error: empty static union\n"); return 1; }

    cv::VideoCapture cap(videoPath.toStdString());
    if (!cap.isOpened()) { std::fprintf(stderr, "error: cannot open video\n"); return 1; }
    cap.set(cv::CAP_PROP_POS_FRAMES, frame);
    cv::Mat bgr;
    if (!cap.read(bgr) || bgr.empty()) { std::fprintf(stderr, "error: cannot read frame\n"); return 1; }

    const double scale = (bgr.cols > 960.0 ? 960.0 / bgr.cols : 1.0);
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    if (scale < 1.0) cv::resize(gray, gray, cv::Size(), scale, scale, cv::INTER_AREA);

    // Same fold as HomographyWorker::run: resize union to the working frame,
    // build an all-255 mask, zero the detected graphic pixels.
    cv::Mat sm;
    cv::resize(u, sm, gray.size(), 0, 0, cv::INTER_NEAREST);
    cv::Mat mask(gray.size(), CV_8U, cv::Scalar(255));
    mask.setTo(0, sm > 127);
    const cv::Mat graphic = (sm > 127);

    auto onGraphic = [&](const std::vector<cv::Point2f> &pts) {
        int c = 0;
        for (const auto &p : pts)
            if (graphic.at<uchar>(cvRound(p.y), cvRound(p.x))) ++c;
        return c;
    };
    std::vector<cv::Point2f> c0, cm;
    cv::goodFeaturesToTrack(gray, c0, 700, 0.01, 7);
    cv::goodFeaturesToTrack(gray, cm, 700, 0.01, 7, mask);
    std::fprintf(stdout, "frame %d  masked %.2f%% of working frame\n", frame,
                 double(cv::countNonZero(graphic)) / (graphic.rows * graphic.cols) * 100.0);
    std::fprintf(stdout, "  no mask : %zu features, %d on graphics\n", c0.size(), onGraphic(c0));
    std::fprintf(stdout, "  masked  : %zu features, %d on graphics\n", cm.size(), onGraphic(cm));
    std::fflush(stdout);
    return onGraphic(cm) == 0 ? 0 : 1;
}

// Headless: OCR a captured line-up image and print each shirt number with its
// normalised position on the graphic ("pepe --lineup-positions <image>").
static int runLineupPositions(const QString &imagePath)
{
    QString err;
    const QVariantList nums = LineupPositionExtractor::numbersFromImage(imagePath, &err);
    if (nums.isEmpty() && !err.isEmpty()) {
        std::fprintf(stderr, "error: %s\n", qPrintable(err));
        return 1;
    }
    std::fprintf(stdout, "%s: %lld number(s)\n", qPrintable(imagePath),
                 static_cast<long long>(nums.size()));
    for (const QVariant &v : nums) {
        const QVariantMap m = v.toMap();
        std::fprintf(stdout, "  #%-2d  x=%.3f  y=%.3f\n",
                     m.value(QStringLiteral("number")).toInt(),
                     m.value(QStringLiteral("x")).toDouble(),
                     m.value(QStringLiteral("y")).toDouble());
    }
    std::fflush(stdout);
    return nums.isEmpty() ? 1 : 0;
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
        if (args.size() >= 2 && args.at(1) == QLatin1String("--homography-pitchpoint-test"))
            return runHomographyPitchPointTest();
        if (args.size() >= 2 && args.at(1) == QLatin1String("--landmark-catalog-test"))
            return runLandmarkCatalogTest();
        if (args.size() >= 2 && args.at(1) == QLatin1String("--reproject-model-test"))
            return runReprojectModelTest();
        if (args.size() >= 2 && args.at(1) == QLatin1String("--keyframe-pitch-test"))
            return runKeyframePitchTest();
        if (args.size() >= 2 && args.at(1) == QLatin1String("--keyframe-pitch-nav-test"))
            return runKeyframePitchNavTest();
        if (args.size() >= 5 && args.at(1) == QLatin1String("--detect-shots"))
            return runDetectShots(args.at(2), args.at(3).toInt(), args.at(4).toInt());
        if (args.size() >= 8 && args.at(1) == QLatin1String("--flow-mask-test"))
            return runFlowMaskTest(args.at(2), args.at(3).toInt(),
                                   cv::Rect(args.at(4).toInt(), args.at(5).toInt(),
                                            args.at(6).toInt(), args.at(7).toInt()));
        if (args.size() >= 5 && args.at(1) == QLatin1String("--gen-masks"))
            return runGenMasks(args.at(2), args.at(3), args.at(4),
                               args.size() > 5 ? args.at(5).toInt() : 0);
        if (args.size() >= 4 && args.at(1) == QLatin1String("--static-union"))
            return runStaticUnion(args.at(2), args.at(3));
        if (args.size() >= 5 && args.at(1) == QLatin1String("--flow-static-test"))
            return runFlowStaticTest(args.at(2), args.at(3).toInt(), args.at(4));
        if (args.size() >= 3 && args.at(1) == QLatin1String("--lineup-positions"))
            return runLineupPositions(args.at(2));
    }

    // Basic style so QML can fully restyle the controls.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AppController controller;

    QQmlApplicationEngine engine;
    // The engine takes ownership of the provider.
    engine.addImageProvider(QStringLiteral("videoframe"), controller.frameProvider());
    engine.addImageProvider(QStringLiteral("videoframe2"), controller.frameProvider2());
    engine.addImageProvider(QStringLiteral("featuremask"), controller.maskProvider());
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
