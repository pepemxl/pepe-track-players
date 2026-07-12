#include "LineCalibrator.h"
#include "HomographySolver.h"

#include <array>
#include <cmath>

namespace {

// ---- pitch model (105 x 68 m) straight segments ----------------------------

const std::vector<cv::Vec4f> &modelSegs()
{
    static const std::vector<cv::Vec4f> segs = {
        // touchlines + goal lines
        {0, 0, 105, 0}, {0, 68, 105, 68}, {0, 0, 0, 68}, {105, 0, 105, 68},
        // halfway
        {52.5f, 0, 52.5f, 68},
        // left penalty area
        {16.5f, 13.84f, 16.5f, 54.16f}, {0, 13.84f, 16.5f, 13.84f}, {0, 54.16f, 16.5f, 54.16f},
        // right penalty area
        {88.5f, 13.84f, 88.5f, 54.16f}, {105, 13.84f, 88.5f, 13.84f}, {105, 54.16f, 88.5f, 54.16f},
        // left goal area
        {5.5f, 24.84f, 5.5f, 43.16f}, {0, 24.84f, 5.5f, 24.84f}, {0, 43.16f, 5.5f, 43.16f},
        // right goal area
        {99.5f, 24.84f, 99.5f, 43.16f}, {105, 24.84f, 99.5f, 24.84f}, {105, 43.16f, 99.5f, 43.16f},
    };
    return segs;
}

// Pitch circles (center x, center y, radius) in meters: the two penalty arcs
// and the centre circle. HoughLinesP fragments these into short chords, whose
// points would otherwise be force-snapped to the nearest straight line.
struct Circle { float cx, cy, r; };
const std::vector<Circle> &modelCircles()
{
    static const std::vector<Circle> c = {
        {11.0f, 34.0f, 9.15f}, {94.0f, 34.0f, 9.15f}, {52.5f, 34.0f, 9.15f},
    };
    return c;
}

// Foot of perpendicular from q onto segment [a,b], clamped to the segment.
cv::Point2f footOnSeg(const cv::Point2f &q, const cv::Point2f &a, const cv::Point2f &b)
{
    const cv::Point2f d = b - a;
    const double dd = d.dot(d);
    double t = dd > 1e-9 ? (q - a).dot(d) / dd : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    return a + cv::Point2f(static_cast<float>(d.x * t), static_cast<float>(d.y * t));
}

// Nearest model line/circle to a pitch point; returns foot + distance (m).
bool snapToModel(const cv::Point2f &q, double gate, cv::Point2f &foot)
{
    double bestD = gate;
    bool found = false;
    for (const cv::Vec4f &s : modelSegs()) {
        const cv::Point2f f = footOnSeg(q, {s[0], s[1]}, {s[2], s[3]});
        const double d = cv::norm(q - f);
        if (d < bestD) { bestD = d; foot = f; found = true; }
    }
    for (const Circle &c : modelCircles()) {
        const cv::Point2f d(q.x - c.cx, q.y - c.cy);
        const double dist = cv::norm(d);
        if (dist < 1e-3) continue;
        const cv::Point2f onCirc(c.cx + c.r * d.x / dist, c.cy + c.r * d.y / dist);
        const double dd = cv::norm(q - onCirc);
        if (dd < bestD) { bestD = dd; foot = onCirc; found = true; }
    }
    return found;
}

double meanReproj(const cv::Mat &H, const std::vector<cv::Point2f> &img,
                  const std::vector<cv::Point2f> &pit, const std::vector<char> &inl)
{
    if (H.empty()) return 1e9;
    cv::Mat Hinv = H.inv();
    std::vector<cv::Point2f> back;
    cv::perspectiveTransform(pit, back, Hinv);
    double err = 0.0; int m = 0;
    for (size_t i = 0; i < img.size(); ++i) {
        if (!inl.empty() && !inl[i]) continue;
        err += cv::norm(back[i] - img[i]); ++m;
    }
    return m > 0 ? err / m : 1e9;
}

} // namespace

const std::vector<cv::Vec4f> &LineCalibrator::pitchModelSegments() { return modelSegs(); }

std::vector<cv::Vec4f> LineCalibrator::detectLines(const cv::Mat &bgr,
                                                   const std::vector<cv::Rect> &playerBoxes)
{
    std::vector<cv::Vec4f> out;
    if (bgr.empty()) return out;

    const double maxW = 1280.0;
    const double scale = (bgr.cols > maxW ? maxW / bgr.cols : 1.0);
    const double invScale = 1.0 / scale;

    cv::Mat small;
    if (scale < 1.0) cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else small = bgr;
    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);

    // Grass region: the pitch is green. A closed green mask (with the white
    // lines/players inside it filled in) confines line detection to the field,
    // discarding crowd, roof, LED boards and other off-pitch clutter — by far
    // the biggest source of spurious Hough lines.
    cv::Mat hsv;
    cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
    cv::Mat grass;
    cv::inRange(hsv, cv::Scalar(28, 25, 25), cv::Scalar(95, 255, 255), grass);
    cv::morphologyEx(grass, grass, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(31, 31)));
    cv::morphologyEx(grass, grass, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));
    // Keep only the largest green blob (the field), dropping stray crowd green.
    {
        cv::Mat labels, stats, cents;
        const int n = cv::connectedComponentsWithStats(grass, labels, stats, cents, 8);
        int best = 0; int bestArea = 0;
        for (int i = 1; i < n; ++i) {
            const int area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area > bestArea) { bestArea = area; best = i; }
        }
        if (best > 0) grass = (labels == best);
    }

    // White-line enhancement: morphological top-hat isolates thin bright
    // structures (lines) from the darker/greener background.
    cv::Mat tophat;
    cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(13, 13));
    cv::morphologyEx(gray, tophat, cv::MORPH_TOPHAT, k);

    cv::Mat bw;
    cv::threshold(tophat, bw, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    // Lines are bright: also require reasonable absolute brightness.
    cv::Mat bright;
    cv::threshold(gray, bright, 120, 255, cv::THRESH_BINARY);
    cv::bitwise_and(bw, bright, bw);
    // Confine to the field.
    cv::bitwise_and(bw, grass, bw);

    // Erase moving objects (players) so their edges don't feed Hough.
    for (const cv::Rect &r : playerBoxes) {
        cv::Rect s(cvRound((r.x - r.width * 0.15) * scale),
                   cvRound((r.y - r.height * 0.1) * scale),
                   cvRound(r.width * 1.3 * scale),
                   cvRound(r.height * 1.3 * scale));
        s &= cv::Rect(0, 0, bw.cols, bw.rows);
        if (s.area() > 0) bw(s).setTo(0);
    }

    std::vector<cv::Vec4i> lp;
    cv::HoughLinesP(bw, lp, 1.0, CV_PI / 180.0, 55, 45.0, 18.0);
    out.reserve(lp.size());
    for (const cv::Vec4i &l : lp)
        out.push_back(cv::Vec4f(l[0] * invScale, l[1] * invScale,
                                l[2] * invScale, l[3] * invScale));
    return out;
}

LineCalibrator::Result LineCalibrator::refine(const cv::Mat &Hinit,
                                              const std::vector<cv::Vec4f> &lines)
{
    Result r;
    r.lineCount = static_cast<int>(lines.size());
    if (Hinit.empty() || lines.size() < 3) return r;

    cv::Mat H = Hinit.clone();
    // Coarse-to-fine snap gates (meters).
    const double gates[5] = {5.0, 3.0, 1.8, 1.1, 0.7};
    std::vector<cv::Point2f> img, pit;
    std::vector<char> inliers;

    for (int iter = 0; iter < 5; ++iter) {
        img.clear();
        pit.clear();
        // Sample points along each detected segment and snap to the model.
        for (const cv::Vec4f &l : lines) {
            const cv::Point2f a(l[0], l[1]), b(l[2], l[3]);
            const double len = cv::norm(b - a);
            const int nSamp = std::max(2, static_cast<int>(len / 12.0));
            std::vector<cv::Point2f> pts;
            for (int s = 0; s <= nSamp; ++s) {
                const float t = static_cast<float>(s) / nSamp;
                pts.push_back(a + cv::Point2f((b.x - a.x) * t, (b.y - a.y) * t));
            }
            std::vector<cv::Point2f> mapped;
            cv::perspectiveTransform(pts, mapped, H);
            for (size_t i = 0; i < pts.size(); ++i) {
                cv::Point2f foot;
                if (snapToModel(mapped[i], gates[iter], foot)) {
                    img.push_back(pts[i]);
                    pit.push_back(foot);
                }
            }
        }
        if (static_cast<int>(img.size()) < 8) break;

        std::vector<char> inl;
        // Tighten the RANSAC inlier band together with the snap gate.
        const double ransacThresh = std::max(0.6, gates[iter] * 0.45);
        cv::Mat Hn = homog::findHomography(img, pit, ransacThresh, 600, nullptr, &inl);
        if (Hn.empty()) break;
        H = Hn;
        inliers = inl;
    }

    if (img.size() < 8 || inliers.empty()) return r;
    r.H = H;
    r.inliers = static_cast<int>(std::count(inliers.begin(), inliers.end(), 1));
    r.reprojErr = meanReproj(H, img, pit, inliers);
    r.initErr = meanReproj(Hinit, img, pit, inliers);
    r.ok = r.inliers >= 12 && r.reprojErr < 40.0;
    return r;
}

LineCalibrator::Result LineCalibrator::calibrate(const cv::Mat &bgr,
                                                 const std::vector<cv::Rect> &playerBoxes,
                                                 const cv::Mat &Hinit)
{
    return refine(Hinit, detectLines(bgr, playerBoxes));
}
