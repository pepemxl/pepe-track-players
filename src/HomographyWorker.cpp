#include "HomographyWorker.h"

#include <QFile>
#include <QDataStream>

#include <opencv2/opencv.hpp>

#include <array>
#include <vector>

// ---------------------------------------------------------------------------
// Geometry helpers (calib3d is not available in this OpenCV build, so the
// homography estimation is done by hand with a normalized DLT + SVD and a
// small RANSAC loop).
// ---------------------------------------------------------------------------
namespace {

// Hartley normalization: translate points to the centroid and scale so the
// mean distance to the origin is sqrt(2). Returns the 3x3 transform T such
// that out = T * in (in homogeneous coords), filling `out`.
cv::Mat normalizePoints(const std::vector<cv::Point2f> &pts,
                        std::vector<cv::Point2f> &out)
{
    const int n = static_cast<int>(pts.size());
    cv::Point2f c(0.f, 0.f);
    for (const auto &p : pts) c += p;
    c *= (n > 0 ? 1.0f / n : 0.0f);

    double meanDist = 0.0;
    for (const auto &p : pts) meanDist += cv::norm(cv::Point2f(p.x - c.x, p.y - c.y));
    meanDist = (n > 0 ? meanDist / n : 0.0);
    const double s = (meanDist > 1e-9 ? std::sqrt(2.0) / meanDist : 1.0);

    out.resize(n);
    for (int i = 0; i < n; ++i)
        out[i] = cv::Point2f(static_cast<float>((pts[i].x - c.x) * s),
                             static_cast<float>((pts[i].y - c.y) * s));

    cv::Mat T = (cv::Mat_<double>(3, 3) <<
                 s, 0, -s * c.x,
                 0, s, -s * c.y,
                 0, 0, 1);
    return T;
}

// Direct Linear Transform: solve for H (src -> dst) from >=4 correspondences.
cv::Mat homographyDLT(const std::vector<cv::Point2f> &src,
                      const std::vector<cv::Point2f> &dst)
{
    const int n = static_cast<int>(src.size());
    if (n < 4 || dst.size() != src.size()) return cv::Mat();

    std::vector<cv::Point2f> sn, dn;
    cv::Mat T1 = normalizePoints(src, sn);
    cv::Mat T2 = normalizePoints(dst, dn);

    cv::Mat A = cv::Mat::zeros(2 * n, 9, CV_64F);
    for (int i = 0; i < n; ++i) {
        const double x = sn[i].x, y = sn[i].y;
        const double u = dn[i].x, v = dn[i].y;
        double *r0 = A.ptr<double>(2 * i);
        double *r1 = A.ptr<double>(2 * i + 1);
        r0[0] = -x; r0[1] = -y; r0[2] = -1; r0[6] = u * x; r0[7] = u * y; r0[8] = u;
        r1[3] = -x; r1[4] = -y; r1[5] = -1; r1[6] = v * x; r1[7] = v * y; r1[8] = v;
    }

    // Null space of A via the smallest eigenvector of A^T A (9x9). This is
    // far cheaper than a full SVD of the tall A when there are many inliers.
    cv::Mat AtA = A.t() * A;                  // 9x9 symmetric PSD
    cv::Mat evals, evecs;
    cv::eigen(AtA, evals, evecs);             // rows sorted by descending eigenvalue
    cv::Mat h = evecs.row(8).t();            // smallest eigenvalue -> null space
    cv::Mat Hn = h.reshape(1, 3);            // 3x3 (normalized space)
    cv::Mat H = T2.inv() * Hn * T1;          // denormalize
    if (std::abs(H.at<double>(2, 2)) > 1e-12) H /= H.at<double>(2, 2);
    return H;
}

std::array<cv::Point2f, 4> applyH(const cv::Mat &H, const std::array<cv::Point2f, 4> &in)
{
    std::vector<cv::Point2f> v(in.begin(), in.end()), o;
    cv::perspectiveTransform(v, o, H);
    std::array<cv::Point2f, 4> r;
    for (int k = 0; k < 4; ++k) r[k] = o[k];
    return r;
}

// RANSAC homography over noisy correspondences (players are the outliers).
cv::Mat ransacHomography(const std::vector<cv::Point2f> &src,
                         const std::vector<cv::Point2f> &dst,
                         double thresh, int iters)
{
    const int n = static_cast<int>(src.size());
    if (n < 4) return cv::Mat();

    cv::RNG rng(0x51ACED);
    int bestCount = -1;
    std::vector<char> bestInliers;
    const double t2 = thresh * thresh;

    for (int it = 0; it < iters; ++it) {
        int idx[4];
        // 4 distinct random indices
        for (int k = 0; k < 4; ++k) {
            bool dup = true;
            while (dup) {
                idx[k] = rng.uniform(0, n);
                dup = false;
                for (int j = 0; j < k; ++j) if (idx[j] == idx[k]) { dup = true; break; }
            }
        }
        std::vector<cv::Point2f> s4{src[idx[0]], src[idx[1]], src[idx[2]], src[idx[3]]};
        std::vector<cv::Point2f> d4{dst[idx[0]], dst[idx[1]], dst[idx[2]], dst[idx[3]]};
        cv::Mat H = homographyDLT(s4, d4);
        if (H.empty()) continue;

        std::vector<cv::Point2f> proj;
        cv::perspectiveTransform(src, proj, H);
        int cnt = 0;
        std::vector<char> inl(n, 0);
        for (int i = 0; i < n; ++i) {
            const double dx = proj[i].x - dst[i].x;
            const double dy = proj[i].y - dst[i].y;
            if (dx * dx + dy * dy < t2) { inl[i] = 1; ++cnt; }
        }
        if (cnt > bestCount) { bestCount = cnt; bestInliers = inl; }
    }

    if (bestCount < 4) return cv::Mat();

    std::vector<cv::Point2f> si, di;
    for (int i = 0; i < n; ++i) if (bestInliers[i]) { si.push_back(src[i]); di.push_back(dst[i]); }
    return homographyDLT(si, di);   // refit on all inliers
}

// Estimate the frame-to-frame homography from prevGray -> curGray. Grays are
// downscaled; invScale maps their coordinates back to full-res pixels.
cv::Mat estimateInterFrame(const cv::Mat &prevGray, const cv::Mat &curGray, double invScale)
{
    const cv::Mat eye = cv::Mat::eye(3, 3, CV_64F);

    std::vector<cv::Point2f> corners;
    cv::goodFeaturesToTrack(prevGray, corners, 700, 0.01, 7);
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

    cv::Mat H = ransacHomography(src, dst, 3.0 * invScale, 200);
    if (H.empty() || std::abs(H.at<double>(2, 2)) < 1e-12) return eye;
    return H;
}

} // namespace

// ---------------------------------------------------------------------------

HomographyWorker::HomographyWorker(QObject *parent) : QThread(parent) {}

HomographyWorker::~HomographyWorker() { stopAndWait(); }

void HomographyWorker::configure(const QString &videoPath,
                                 const QVector<Keyframe> &keyframes,
                                 const QString &outPath)
{
    m_videoPath = videoPath;
    m_keyframes = keyframes;
    m_outPath = outPath;
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
    auto toGray = [&](const cv::Mat &f) {
        cv::Mat g;
        cv::cvtColor(f, g, cv::COLOR_BGR2GRAY);
        if (scale < 1.0) cv::resize(g, g, cv::Size(), scale, scale, cv::INTER_AREA);
        return g;
    };

    cap.set(cv::CAP_PROP_POS_FRAMES, start);
    cv::Mat frame;
    if (!cap.read(frame) || frame.empty()) {
        emit finished(false, "No se pudo leer el frame inicial", 0, 0);
        return;
    }
    cv::Mat prevGray = toGray(frame);

    // Dense output: 4 image points (full-res pixels) per frame in [start,end].
    QVector<std::array<cv::Point2f, 4>> dense(total + 1);

    int segIdx = 0;                                 // current segment = [segIdx, segIdx+1]
    int a = start;                                  // start frame of current segment
    std::array<cv::Point2f, 4> fwd = toCv(m_keyframes[0].img);
    std::vector<cv::Mat> segM;                      // inter-frame H's within the segment
    std::vector<std::array<cv::Point2f, 4>> segFwd; // forward points per frame in segment
    segFwd.push_back(fwd);

    for (int t = start; t < end; ++t) {
        if (m_stop.load()) { emit finished(false, "Cancelado", 0, 0); return; }

        if (!cap.read(frame) || frame.empty()) break;
        cv::Mat curGray = toGray(frame);

        cv::Mat M = estimateInterFrame(prevGray, curGray, invScale);
        segM.push_back(M);
        fwd = applyH(M, fwd);
        segFwd.push_back(fwd);
        prevGray = curGray;

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

    // Write the dense track: [qint32 start][qint32 count] then count*4*(x,y) doubles.
    QFile out(m_outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        emit finished(false, "No se pudo escribir el archivo de salida", 0, 0);
        return;
    }
    QDataStream ds(&out);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    ds << qint32(start) << qint32(dense.size());
    for (const auto &pts : dense)
        for (int k = 0; k < 4; ++k)
            ds << double(pts[k].x) << double(pts[k].y);
    out.close();

    emit progressChanged(1.0, QStringLiteral("Listo"));
    emit finished(true, QString(), start, dense.size());
}
