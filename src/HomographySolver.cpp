#include "HomographySolver.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace homog {

namespace {

std::atomic<Backend> g_backend{Backend::OpenCV};

// Resolve the environment override once at process start.
Backend envBackend()
{
    if (const char *e = std::getenv("PEPE_HOMOG_BACKEND")) {
        if (std::strcmp(e, "custom") == 0) return Backend::Custom;
        if (std::strcmp(e, "opencv") == 0) return Backend::OpenCV;
    }
    return Backend::OpenCV;
}
[[maybe_unused]] const bool g_envInit = [] { g_backend.store(envBackend()); return true; }();

Backend resolve(Backend b) { return b == Backend::Default ? g_backend.load() : b; }

// ---- custom estimator: normalized DLT + home-grown RANSAC ------------------

cv::Mat normalizePoints(const std::vector<cv::Point2f> &pts, std::vector<cv::Point2f> &out)
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
    return (cv::Mat_<double>(3, 3) << s, 0, -s * c.x, 0, s, -s * c.y, 0, 0, 1);
}

cv::Mat customDLT(const std::vector<cv::Point2f> &src, const std::vector<cv::Point2f> &dst)
{
    const int n = static_cast<int>(src.size());
    if (n < 4 || dst.size() != src.size()) return cv::Mat();
    std::vector<cv::Point2f> sn, dn;
    cv::Mat T1 = normalizePoints(src, sn);
    cv::Mat T2 = normalizePoints(dst, dn);
    cv::Mat A = cv::Mat::zeros(2 * n, 9, CV_64F);
    for (int i = 0; i < n; ++i) {
        const double x = sn[i].x, y = sn[i].y, u = dn[i].x, v = dn[i].y;
        double *r0 = A.ptr<double>(2 * i);
        double *r1 = A.ptr<double>(2 * i + 1);
        r0[0] = -x; r0[1] = -y; r0[2] = -1; r0[6] = u * x; r0[7] = u * y; r0[8] = u;
        r1[3] = -x; r1[4] = -y; r1[5] = -1; r1[6] = v * x; r1[7] = v * y; r1[8] = v;
    }
    cv::Mat AtA = A.t() * A, evals, evecs;
    cv::eigen(AtA, evals, evecs);
    cv::Mat hv = evecs.row(8).t();
    cv::Mat Hn = hv.reshape(1, 3);
    cv::Mat H = T2.inv() * Hn * T1;
    if (std::abs(H.at<double>(2, 2)) > 1e-12) H /= H.at<double>(2, 2);
    return H;
}

int countInliers(const cv::Mat &H, const std::vector<cv::Point2f> &src,
                 const std::vector<cv::Point2f> &dst, double t2, std::vector<char> *mask)
{
    std::vector<cv::Point2f> proj;
    cv::perspectiveTransform(src, proj, H);
    int cnt = 0;
    if (mask) mask->assign(src.size(), 0);
    for (size_t i = 0; i < src.size(); ++i) {
        const double dx = proj[i].x - dst[i].x, dy = proj[i].y - dst[i].y;
        if (dx * dx + dy * dy < t2) { if (mask) (*mask)[i] = 1; ++cnt; }
    }
    return cnt;
}

cv::Mat customRansac(const std::vector<cv::Point2f> &src, const std::vector<cv::Point2f> &dst,
                     double thresh, int iters, int *inlierCount, std::vector<char> *inlierMask)
{
    const int n = static_cast<int>(src.size());
    if (inlierCount) *inlierCount = 0;
    if (inlierMask) inlierMask->clear();
    if (n < 4) return cv::Mat();
    cv::RNG rng(0x51ACED);
    int bestCount = -1;
    std::vector<char> best;
    const double t2 = thresh * thresh;
    for (int it = 0; it < iters; ++it) {
        int idx[4];
        for (int k = 0; k < 4; ++k) {
            bool dup = true;
            while (dup) { idx[k] = rng.uniform(0, n); dup = false;
                for (int j = 0; j < k; ++j) if (idx[j] == idx[k]) { dup = true; break; } }
        }
        std::vector<cv::Point2f> s4{src[idx[0]], src[idx[1]], src[idx[2]], src[idx[3]]};
        std::vector<cv::Point2f> d4{dst[idx[0]], dst[idx[1]], dst[idx[2]], dst[idx[3]]};
        cv::Mat H = customDLT(s4, d4);
        if (H.empty()) continue;
        std::vector<char> inl;
        const int cnt = countInliers(H, src, dst, t2, &inl);
        if (cnt > bestCount) { bestCount = cnt; best = inl; }
    }
    if (bestCount < 4) return cv::Mat();
    std::vector<cv::Point2f> si, di;
    for (int i = 0; i < n; ++i) if (best[i]) { si.push_back(src[i]); di.push_back(dst[i]); }
    cv::Mat H = customDLT(si, di);   // refit on inliers
    if (!H.empty()) {
        // Recompute inliers/mask against the refined model.
        std::vector<char> finalMask;
        const int cnt = countInliers(H, src, dst, t2, &finalMask);
        if (inlierCount) *inlierCount = cnt;
        if (inlierMask) *inlierMask = finalMask;
    }
    return H;
}

} // namespace

void setDefaultBackend(Backend b) { if (b != Backend::Default) g_backend.store(b); }
Backend defaultBackend() { return g_backend.load(); }
const char *backendName(Backend b)
{
    switch (resolve(b)) {
    case Backend::OpenCV: return "opencv";
    case Backend::Custom: return "custom";
    default: return "opencv";
    }
}

cv::Mat solveDLT(const std::vector<cv::Point2f> &src, const std::vector<cv::Point2f> &dst,
                 Backend backend)
{
    if (src.size() < 4 || dst.size() != src.size()) return cv::Mat();
    if (resolve(backend) == Backend::Custom)
        return customDLT(src, dst);
    // method 0 = least-squares (exact for 4 points).
    return cv::findHomography(src, dst, 0);
}

cv::Mat findHomography(const std::vector<cv::Point2f> &src, const std::vector<cv::Point2f> &dst,
                       double thresh, int iters, int *inlierCount,
                       std::vector<char> *inlierMask, Backend backend)
{
    if (inlierCount) *inlierCount = 0;
    if (inlierMask) inlierMask->clear();
    if (src.size() < 4 || dst.size() != src.size()) return cv::Mat();

    if (resolve(backend) == Backend::Custom)
        return customRansac(src, dst, thresh, iters, inlierCount, inlierMask);

    cv::Mat mask;
    cv::Mat H = cv::findHomography(src, dst, cv::RANSAC, thresh, mask,
                                  iters, 0.995);
    if (H.empty()) return H;
    H.convertTo(H, CV_64F);
    int cnt = 0;
    if (inlierMask) inlierMask->assign(src.size(), 0);
    for (int i = 0; i < mask.rows; ++i) {
        if (mask.at<uchar>(i)) { ++cnt; if (inlierMask) (*inlierMask)[i] = 1; }
    }
    if (inlierCount) *inlierCount = cnt;
    return H;
}

} // namespace homog
