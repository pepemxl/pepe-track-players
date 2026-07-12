#ifndef HOMOGRAPHYSOLVER_H
#define HOMOGRAPHYSOLVER_H

#include <opencv2/core.hpp>
#include <vector>

// Pluggable homography estimation.
//
// Two interchangeable backends solve the same problem:
//   - OpenCV : cv::findHomography (calib3d) — battle-tested RANSAC/RHO, fast.
//   - Custom : our own normalized-DLT + home-grown RANSAC (SVD/eigen based),
//              kept as a separate, fully controllable/instrumentable estimator
//              for cases where a customizable version is preferred (special
//              weighting, deterministic behaviour, environments without
//              calib3d, etc.).
//
// The active backend is configurable globally (setDefaultBackend, or the
// PEPE_HOMOG_BACKEND=opencv|custom environment variable) and can be overridden
// per call. Default is OpenCV.
namespace homog {

enum class Backend { Default, OpenCV, Custom };

// Global default (thread-safe). Initialized from PEPE_HOMOG_BACKEND if set.
void setDefaultBackend(Backend b);
Backend defaultBackend();
const char *backendName(Backend b);

// Exact (4 points) or least-squares (>4 clean points) homography src -> dst,
// no outlier rejection. Returns an empty Mat on failure.
cv::Mat solveDLT(const std::vector<cv::Point2f> &src,
                 const std::vector<cv::Point2f> &dst,
                 Backend backend = Backend::Default);

// Robust homography src -> dst with RANSAC. `thresh` is the inlier reprojection
// distance in dst units (pixels). `inlierCount` (optional) receives the number
// of inliers; `inlierMask` (optional) receives a 0/1 flag per correspondence.
cv::Mat findHomography(const std::vector<cv::Point2f> &src,
                       const std::vector<cv::Point2f> &dst,
                       double thresh, int iters = 2000,
                       int *inlierCount = nullptr,
                       std::vector<char> *inlierMask = nullptr,
                       Backend backend = Backend::Default);

} // namespace homog

#endif
