#ifndef LINECALIBRATOR_H
#define LINECALIBRATOR_H

#include <opencv2/opencv.hpp>
#include <vector>

// Phase F3 of docs/homography.md — automatic calibration by pitch lines.
//
// The fully-blind line-to-model assignment is fragile, so this uses the plan's
// robust variant: starting from an approximate homography (the manual 4-point
// calibration or the interpolated/propagated H at the frame), it detects the
// white pitch lines, then refines H with an ICP-style loop that snaps points
// sampled along the detected lines onto the known pitch-model line segments and
// re-solves H (normalized DLT + home-grown RANSAC, since calib3d is absent).
// The initial H bootstraps the correspondence; each iteration tightens it.
class LineCalibrator
{
public:
    struct Result
    {
        cv::Mat H;              // refined image -> pitch homography (3x3 CV_64F)
        int     lineCount{0};   // detected line segments
        int     inliers{0};     // point correspondences used in the final fit
        double  reprojErr{0.0}; // mean px error of inliers (pitch->image via H^-1)
        double  initErr{0.0};   // same, measured for the initial H (for comparison)
        bool    ok{false};
    };

    // White-line segments in full-resolution image pixels. Player boxes (video
    // pixels) are erased from the line mask before Hough, plus a margin.
    static std::vector<cv::Vec4f> detectLines(const cv::Mat &bgr,
                                              const std::vector<cv::Rect> &playerBoxes);

    // Refine Hinit (image->pitch) against the detected lines.
    static Result refine(const cv::Mat &Hinit, const std::vector<cv::Vec4f> &lines);

    // Convenience: detect + refine in one call.
    static Result calibrate(const cv::Mat &bgr,
                            const std::vector<cv::Rect> &playerBoxes,
                            const cv::Mat &Hinit);

    // Straight pitch-model line segments in meters (105 x 68), for drawing /
    // debugging overlays.
    static const std::vector<cv::Vec4f> &pitchModelSegments();
};

#endif
