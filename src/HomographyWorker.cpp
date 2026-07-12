#include "HomographyWorker.h"

#include <QFile>
#include <QDataStream>

#include "HomographySolver.h"

#include <opencv2/opencv.hpp>

#include <array>
#include <vector>

// ---------------------------------------------------------------------------
// The homography estimation is delegated to the homog:: module (configurable
// OpenCV cv::findHomography or our custom RANSAC). Only the small geometry
// glue lives here.
// ---------------------------------------------------------------------------
namespace {

std::array<cv::Point2f, 4> applyH(const cv::Mat &H, const std::array<cv::Point2f, 4> &in)
{
    std::vector<cv::Point2f> v(in.begin(), in.end()), o;
    cv::perspectiveTransform(v, o, H);
    std::array<cv::Point2f, 4> r;
    for (int k = 0; k < 4; ++k) r[k] = o[k];
    return r;
}

// HS histogram (normalized) of a small BGR frame, for shot-cut detection
// (phase F4/F5: propagation must not trust motion across a camera cut).
cv::Mat hsHistogram(const cv::Mat &bgrSmall)
{
    cv::Mat hsv;
    cv::cvtColor(bgrSmall, hsv, cv::COLOR_BGR2HSV);
    const int histSize[] = {30, 32};
    const float hr[] = {0, 180}, sr[] = {0, 256};
    const float *ranges[] = {hr, sr};
    const int channels[] = {0, 1};
    cv::Mat hist;
    cv::calcHist(&hsv, 1, channels, cv::Mat(), hist, 2, histSize, ranges, true, false);
    cv::normalize(hist, hist, 1.0, 0.0, cv::NORM_L1);
    return hist;
}

// Estimate the frame-to-frame homography from prevGray -> curGray. Grays are
// downscaled; invScale maps their coordinates back to full-res pixels.
// inlierRatio (out) reports the fraction of tracked features consistent with
// the estimated motion — a per-frame confidence proxy. featureMask (optional,
// same size as prevGray) is 0 over players so no features are tracked on them.
cv::Mat estimateInterFrame(const cv::Mat &prevGray, const cv::Mat &curGray, double invScale,
                           double *inlierRatio, const cv::Mat &featureMask = cv::Mat())
{
    const cv::Mat eye = cv::Mat::eye(3, 3, CV_64F);
    if (inlierRatio) *inlierRatio = 0.0;

    std::vector<cv::Point2f> corners;
    cv::goodFeaturesToTrack(prevGray, corners, 700, 0.01, 7, featureMask);
    if (corners.size() < 12) return eye;

    std::vector<cv::Point2f> next;
    std::vector<uchar> status;
    std::vector<float> err;
    cv::calcOpticalFlowPyrLK(prevGray, curGray, corners, next, status, err,
                             cv::Size(21, 21), 3);

    std::vector<cv::Point2f> src, dst;
    src.reserve(corners.size());
    dst.reserve(corners.size());
    for (size_t i = 0; i < corners.size(); ++i) {
        if (!status[i] || err[i] > 24.0f) continue;
        src.emplace_back(static_cast<float>(corners[i].x * invScale),
                         static_cast<float>(corners[i].y * invScale));
        dst.emplace_back(static_cast<float>(next[i].x * invScale),
                         static_cast<float>(next[i].y * invScale));
    }
    if (src.size() < 12) return eye;

    int inliers = 0;
    cv::Mat H = homog::findHomography(src, dst, 3.0 * invScale, 200, &inliers);
    if (H.empty() || std::abs(H.at<double>(2, 2)) < 1e-12) return eye;
    if (inlierRatio) *inlierRatio = double(inliers) / double(src.size());
    return H;
}

// In-place, cut-aware, keyframe-anchored temporal smoothing of the dense track
// (phase F5). Each of the 8 coordinate channels is Gaussian-smoothed along the
// frame axis, never crossing a shot cut (conf == 0) and re-pinning the exact
// keyframe frames afterwards, with a short feather to avoid a kink.
void smoothDense(std::vector<std::array<cv::Point2f, 4>> &dense,
                 const std::vector<double> &conf,
                 const std::vector<char> &isKeyframe,
                 const std::vector<std::array<cv::Point2f, 4>> &keyframeVal)
{
    const int n = static_cast<int>(dense.size());
    if (n < 5) return;
    const int radius = 6;
    const double sigma = 2.5;
    std::vector<double> kern(radius + 1);
    for (int d = 0; d <= radius; ++d) kern[d] = std::exp(-0.5 * (d * d) / (sigma * sigma));

    auto sameRun = [&](int i, int j) {   // no cut between i and j
        const int lo = std::min(i, j), hi = std::max(i, j);
        for (int f = lo + 1; f <= hi; ++f) if (conf[f] <= 0.0) return false;
        return true;
    };

    std::vector<std::array<cv::Point2f, 4>> out = dense;
    for (int i = 0; i < n; ++i) {
        for (int p = 0; p < 4; ++p) {
            double sx = dense[i][p].x * kern[0], sy = dense[i][p].y * kern[0], w = kern[0];
            for (int d = 1; d <= radius; ++d) {
                for (int s : {i - d, i + d}) {
                    if (s < 0 || s >= n || !sameRun(i, s)) continue;
                    sx += dense[s][p].x * kern[d];
                    sy += dense[s][p].y * kern[d];
                    w += kern[d];
                }
            }
            out[i][p] = cv::Point2f(static_cast<float>(sx / w), static_cast<float>(sy / w));
        }
    }

    // Re-pin keyframes with a +/-3 frame linear feather back to the exact value.
    const int feather = 3;
    for (int i = 0; i < n; ++i) {
        if (!isKeyframe[i]) continue;
        for (int f = std::max(0, i - feather); f <= std::min(n - 1, i + feather); ++f) {
            if (!sameRun(i, f)) continue;
            const double a = double(std::abs(f - i)) / (feather + 1);  // 0 at kf -> ~1 at edge
            for (int p = 0; p < 4; ++p)
                out[f][p] = keyframeVal[i][p] * (1.0 - a) + out[f][p] * a;
        }
    }
    dense.swap(out);
}

} // namespace

// ---------------------------------------------------------------------------

HomographyWorker::HomographyWorker(QObject *parent) : QThread(parent) {}

HomographyWorker::~HomographyWorker() { stopAndWait(); }

void HomographyWorker::configure(const QString &videoPath,
                                 const QVector<Keyframe> &keyframes,
                                 const QString &outPath,
                                 const QHash<int, QVector<QRect>> &playerBoxes,
                                 const QVector<QRectF> &graphicsRegions,
                                 const QImage &staticMask)
{
    m_videoPath = videoPath;
    m_keyframes = keyframes;
    m_outPath = outPath;
    m_playerBoxes = playerBoxes;
    m_graphics = graphicsRegions;
    m_staticMask = staticMask;
    m_stop.store(false);
    std::sort(m_keyframes.begin(), m_keyframes.end(),
              [](const Keyframe &a, const Keyframe &b) { return a.frame < b.frame; });
}

void HomographyWorker::stopAndWait()
{
    requestStop();
    if (isRunning()) { wait(5000); }
}

void HomographyWorker::run()
{
    if (m_keyframes.size() < 2) {
        emit finished(false, "Se necesitan al menos 2 keyframes", 0, 0);
        return;
    }

    cv::VideoCapture cap;
    if (!cap.open(m_videoPath.toStdString())) {
        emit finished(false, "No se pudo abrir el video", 0, 0);
        return;
    }

    const double origW = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    const double maxW = 960.0;
    const double scale = (origW > maxW && origW > 0 ? maxW / origW : 1.0);
    const double invScale = 1.0 / scale;

    const int start = m_keyframes.first().frame;
    const int end = m_keyframes.last().frame;
    const int total = end - start;                 // number of steps
    if (total <= 0) { emit finished(false, "Rango de keyframes vacío", 0, 0); return; }

    auto toCv = [](const QPointF p[4]) {
        std::array<cv::Point2f, 4> a;
        for (int k = 0; k < 4; ++k)
            a[k] = cv::Point2f(static_cast<float>(p[k].x()), static_cast<float>(p[k].y()));
        return a;
    };
    auto smallColor = [&](const cv::Mat &f) {
        cv::Mat s;
        if (scale < 1.0) cv::resize(f, s, cv::Size(), scale, scale, cv::INTER_AREA);
        else s = f.clone();
        return s;
    };
    auto grayOf = [](const cv::Mat &s) {
        cv::Mat g;
        cv::cvtColor(s, g, cv::COLOR_BGR2GRAY);
        return g;
    };

    cap.set(cv::CAP_PROP_POS_FRAMES, start);
    cv::Mat frame;
    if (!cap.read(frame) || frame.empty()) {
        emit finished(false, "No se pudo leer el frame inicial", 0, 0);
        return;
    }
    cv::Mat prevSmall = smallColor(frame);
    cv::Mat prevGray = grayOf(prevSmall);
    cv::Mat prevHist = hsHistogram(prevSmall);

    // Static on-screen graphics (scoreboard, logos): fixed in screen space, so
    // they don't move when the camera pans and would bias the motion toward
    // "no motion". Masked on every frame (phase F5). Two sources are OR-ed:
    // the manually drawn rectangles (m_graphics, normalized) and the
    // auto-detected static-graphic mask (m_staticMask, a grayscale image).
    cv::Mat gfxMask;   // empty if no regions
    auto ensureGfx = [&]() {
        if (gfxMask.empty())
            gfxMask = cv::Mat(prevGray.size(), CV_8U, cv::Scalar(255));
    };
    if (!m_graphics.isEmpty()) {
        ensureGfx();
        for (const QRectF &g : m_graphics) {
            cv::Rect s(cvRound(g.x() * gfxMask.cols), cvRound(g.y() * gfxMask.rows),
                       cvRound(g.width() * gfxMask.cols), cvRound(g.height() * gfxMask.rows));
            s &= cv::Rect(0, 0, gfxMask.cols, gfxMask.rows);
            if (s.area() > 0) gfxMask(s).setTo(0);
        }
    }
    if (!m_staticMask.isNull()) {
        const QImage g8 = m_staticMask.convertToFormat(QImage::Format_Grayscale8);
        const cv::Mat view(g8.height(), g8.width(), CV_8UC1,
                           const_cast<uchar *>(g8.bits()),
                           static_cast<size_t>(g8.bytesPerLine()));
        cv::Mat sm;
        cv::resize(view, sm, prevGray.size(), 0, 0, cv::INTER_NEAREST);
        ensureGfx();
        gfxMask.setTo(0, sm > 127);   // blank the detected graphic pixels
    }

    // Dense output: 4 image points (full-res pixels) per frame in [start,end].
    QVector<std::array<cv::Point2f, 4>> dense(total + 1);
    // Per-frame confidence (0..1): flow inlier ratio, 0 at camera cuts.
    std::vector<double> conf(total + 1, 1.0);

    int segIdx = 0;                                 // current segment = [segIdx, segIdx+1]
    int a = start;                                  // start frame of current segment
    std::array<cv::Point2f, 4> fwd = toCv(m_keyframes[0].img);
    std::vector<cv::Mat> segM;                      // inter-frame H's within the segment
    std::vector<std::array<cv::Point2f, 4>> segFwd; // forward points per frame in segment
    segFwd.push_back(fwd);

    for (int t = start; t < end; ++t) {
        if (m_stop.load()) { emit finished(false, "Cancelado", 0, 0); return; }

        if (!cap.read(frame) || frame.empty()) break;
        cv::Mat curSmall = smallColor(frame);
        cv::Mat curGray = grayOf(curSmall);
        cv::Mat curHist = hsHistogram(curSmall);

        // Camera-cut detection: a sharp histogram change means the motion
        // estimate across this step is meaningless (phase F5 / F4).
        const double corr = cv::compareHist(prevHist, curHist, cv::HISTCMP_CORREL);
        const bool cut = corr < 0.55;

        // Feature mask (prevGray is frame t): static graphics + this frame's
        // players are blanked so features come only from the moving background.
        const auto bit = m_playerBoxes.constFind(t);
        const bool hasBoxes = (bit != m_playerBoxes.constEnd() && !bit.value().isEmpty());
        cv::Mat featMask;   // empty = track features everywhere
        if (!gfxMask.empty() || hasBoxes) {
            featMask = gfxMask.empty()
                ? cv::Mat(prevGray.size(), CV_8U, cv::Scalar(255)) : gfxMask.clone();
            if (hasBoxes) {
                for (const QRect &r : bit.value()) {
                    cv::Rect s(cvRound((r.x() - r.width() * 0.15) * scale),
                               cvRound((r.y() - r.height() * 0.10) * scale),
                               cvRound(r.width() * 1.30 * scale),
                               cvRound(r.height() * 1.30 * scale));
                    s &= cv::Rect(0, 0, featMask.cols, featMask.rows);
                    if (s.area() > 0) featMask(s).setTo(0);
                }
            }
        }

        double ratio = 0.0;
        cv::Mat M = estimateInterFrame(prevGray, curGray, invScale, &ratio, featMask);
        if (cut) M = cv::Mat::eye(3, 3, CV_64F);
        segM.push_back(M);
        fwd = applyH(M, fwd);
        segFwd.push_back(fwd);
        conf[(t + 1) - start] = cut ? 0.0 : ratio;
        prevGray = curGray;
        prevHist = curHist;

        if ((t - start) % 12 == 0) {
            emit progressChanged(double(t - start) / double(total),
                                 QStringLiteral("Flujo óptico  frame %1 / %2").arg(t + 1).arg(end + 1));
        }

        // Reached the next keyframe -> close and blend this segment.
        if (t + 1 == m_keyframes[segIdx + 1].frame) {
            const int len = (t + 1) - a;            // steps in segment
            std::vector<std::array<cv::Point2f, 4>> segBwd(len + 1);
            segBwd[len] = toCv(m_keyframes[segIdx + 1].img);
            for (int s = len - 1; s >= 0; --s)
                segBwd[s] = applyH(segM[s].inv(), segBwd[s + 1]);

            for (int s = 0; s <= len; ++s) {
                const double w = (len > 0 ? double(s) / double(len) : 0.0);
                std::array<cv::Point2f, 4> pts;
                for (int k = 0; k < 4; ++k)
                    pts[k] = segFwd[s][k] * (1.0 - w) + segBwd[s][k] * w;
                dense[(a - start) + s] = pts;
            }

            ++segIdx;
            if (segIdx + 1 >= m_keyframes.size()) break;
            a = t + 1;
            fwd = toCv(m_keyframes[segIdx].img);    // re-anchor at exact keyframe
            segM.clear();
            segFwd.clear();
            segFwd.push_back(fwd);
        }
    }

    emit progressChanged(0.97, QStringLiteral("Suavizado temporal…"));

    // Keyframe frames are calibrated ground truth: confidence 1, and the
    // smoothing must re-pin them exactly.
    const int n = dense.size();
    std::vector<char> isKeyframe(n, 0);
    std::vector<std::array<cv::Point2f, 4>> keyframeVal(n);
    for (const Keyframe &kf : m_keyframes) {
        const int idx = kf.frame - start;
        if (idx >= 0 && idx < n) {
            isKeyframe[idx] = 1;
            conf[idx] = 1.0;
            keyframeVal[idx] = toCv(kf.img);
        }
    }
    std::vector<std::array<cv::Point2f, 4>> denseVec(dense.begin(), dense.end());
    if (!qEnvironmentVariableIsSet("PEPE_NO_SMOOTH"))   // A/B toggle for tests
        smoothDense(denseVec, conf, isKeyframe, keyframeVal);

    // Write the dense track (v2): [magic=-2][start][count] then per frame
    // 4*(x,y) doubles + 1 confidence double.
    QFile out(m_outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        emit finished(false, "No se pudo escribir el archivo de salida", 0, 0);
        return;
    }
    QDataStream ds(&out);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    ds << qint32(-2) << qint32(start) << qint32(n);
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 4; ++k)
            ds << double(denseVec[i][k].x) << double(denseVec[i][k].y);
        ds << double(conf[i]);
    }
    out.close();

    emit progressChanged(1.0, QStringLiteral("Listo"));
    emit finished(true, QString(), start, n);
}
