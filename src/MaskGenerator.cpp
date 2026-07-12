#include "MaskGenerator.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {

// A green blob smaller than this fraction of the frame is stray crowd/seat
// colour, not the field, and gets dropped by the largest-component step.
constexpr double kMinGrassFrac = 0.02;

// Downscale target width for the temporal-stability pass: full-res per-pixel
// variance over hundreds of frames is needlessly heavy, and the static
// overlays we want are large.
constexpr double kStaticWork = 480.0;

} // namespace

MaskGenerator::MaskGenerator(QObject *parent) : QThread(parent) {}
MaskGenerator::~MaskGenerator() { stopAndWait(); }

void MaskGenerator::configure(Kind kind, const QString &chunksDir,
                              const QVector<int> &chunkNumbers, const QString &outDir)
{
    m_kind = kind;
    m_chunksDir = chunksDir;
    m_outDir = outDir;
    m_chunks = chunkNumbers.isEmpty() ? discoverChunks(chunksDir) : chunkNumbers;
    std::sort(m_chunks.begin(), m_chunks.end());
    m_stop.store(false);
}

void MaskGenerator::stopAndWait()
{
    requestStop();
    if (isRunning()) wait(10000);
}

QVector<int> MaskGenerator::discoverChunks(const QString &chunksDir)
{
    QVector<int> out;
    const QStringList files = QDir(chunksDir).entryList(
        {QStringLiteral("video_part_*.mp4")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        bool ok = false;
        const int n = f.mid(11, 3).toInt(&ok);   // "video_part_NNN.mp4"
        if (ok) out.append(n);
    }
    return out;
}

cv::Mat MaskGenerator::greenMask(const cv::Mat &bgr)
{
    if (bgr.empty()) return {};

    // Work at a reduced width for speed; the mask is upscaled back at the end.
    const double maxW = 960.0;
    const double scale = (bgr.cols > maxW ? maxW / bgr.cols : 1.0);
    cv::Mat small;
    if (scale < 1.0) cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else small = bgr;

    cv::Mat hsv;
    cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
    cv::Mat grass;
    cv::inRange(hsv, cv::Scalar(28, 25, 25), cv::Scalar(95, 255, 255), grass);
    cv::morphologyEx(grass, grass, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(25, 25)));
    cv::morphologyEx(grass, grass, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));

    // Keep only the largest green blob (the field) if it is big enough.
    cv::Mat labels, stats, cents;
    const int n = cv::connectedComponentsWithStats(grass, labels, stats, cents, 8);
    int best = 0, bestArea = 0;
    for (int i = 1; i < n; ++i) {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area > bestArea) { bestArea = area; best = i; }
    }
    const double frac = static_cast<double>(bestArea) / (grass.rows * grass.cols);
    if (best > 0 && frac >= kMinGrassFrac) grass = (labels == best);
    else grass.setTo(0);

    if (scale < 1.0)
        cv::resize(grass, grass, bgr.size(), 0, 0, cv::INTER_NEAREST);
    return grass;
}

cv::Mat MaskGenerator::staticMask(const QString &chunkPath, const std::atomic<bool> *stop)
{
    cv::VideoCapture cap;
    if (!cap.open(chunkPath.toStdString())) return {};

    const int total = std::max(1, static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)));
    const double W = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    const double H = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    if (W < 1 || H < 1) return {};
    const double scale = (W > kStaticWork ? kStaticWork / W : 1.0);
    const cv::Size wsz(std::max(1, cvRound(W * scale)), std::max(1, cvRound(H * scale)));

    // Sample up to ~150 frames spread over the chunk. We read sequentially and
    // keep every step-th frame: random seeking (cap.set POS_FRAMES) is
    // unreliable on inter-coded chunks, so a linear scan is both robust and
    // fast enough. Per-pixel running mean + mean-of-squares give the temporal
    // std cheaply in one pass.
    const int want = std::min(total, 150);
    const int step = std::max(1, total / want);

    cv::Mat sumMean = cv::Mat::zeros(wsz, CV_32FC3);
    cv::Mat sumSq   = cv::Mat::zeros(wsz, CV_32FC3);
    cv::Mat frame, small, f32;
    cv::Mat lastHsv;                 // for the non-grass gate on a middle frame
    int used = 0;
    for (int f = 0; ; ++f) {
        if (stop && stop->load()) return {};
        if (!cap.read(frame) || frame.empty()) break;
        if (f % step != 0) continue;
        if (scale < 1.0) cv::resize(frame, small, wsz, 0, 0, cv::INTER_AREA);
        else small = frame;
        small.convertTo(f32, CV_32FC3);
        sumMean += f32;
        cv::accumulateSquare(f32, sumSq);
        if (used == want / 2) cv::cvtColor(small, lastHsv, cv::COLOR_BGR2HSV);
        ++used;
    }
    if (used < 4) return {};

    cv::Mat mean = sumMean / used;
    cv::Mat meanSq = sumSq / used;
    cv::Mat var = meanSq - mean.mul(mean);
    cv::max(var, 0.0, var);
    cv::Mat stdev;
    cv::sqrt(var, stdev);

    // Per-pixel temporal std across channels (max of the three): a burnt-in
    // graphic barely moves while the panned scene behind it changes a lot.
    std::vector<cv::Mat> ch;
    cv::split(stdev, ch);
    cv::Mat stdMax = cv::max(ch[0], cv::max(ch[1], ch[2]));

    cv::Mat stable;                  // low temporal variation
    cv::threshold(stdMax, stable, 7.0, 255, cv::THRESH_BINARY_INV);
    stable.convertTo(stable, CV_8U);

    // Exclude the pitch: a green mid-frame region is grass, not a graphic.
    if (!lastHsv.empty()) {
        cv::Mat grass;
        cv::inRange(lastHsv, cv::Scalar(28, 25, 25), cv::Scalar(95, 255, 255), grass);
        cv::bitwise_not(grass, grass);
        cv::bitwise_and(stable, grass, stable);
    }

    // Clean speckle and keep only sizeable overlay blobs.
    cv::morphologyEx(stable, stable, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
    cv::morphologyEx(stable, stable, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9)));
    {
        cv::Mat labels, stats, cents;
        const int n = cv::connectedComponentsWithStats(stable, labels, stats, cents, 8);
        const int minArea = cvRound(0.0008 * stable.rows * stable.cols);
        cv::Mat keep = cv::Mat::zeros(stable.size(), CV_8U);
        for (int i = 1; i < n; ++i)
            if (stats.at<int>(i, cv::CC_STAT_AREA) >= minArea)
                keep.setTo(255, labels == i);
        stable = keep;
    }

    cv::Mat full;
    cv::resize(stable, full, cv::Size(cvRound(W), cvRound(H)), 0, 0, cv::INTER_NEAREST);
    return full;
}

cv::Mat MaskGenerator::unionStaticMasks(const QString &staticDir, double voteFrac)
{
    const QStringList parts = QDir(staticDir).entryList(
        {QStringLiteral("video_part_*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    cv::Mat counter;   // CV_16U, size of the first mask read
    int nMasks = 0;
    for (const QString &p : parts) {
        const QString path = staticDir + QLatin1Char('/') + p + QStringLiteral("/mask.png");
        cv::Mat mask = cv::imread(path.toStdString(), cv::IMREAD_GRAYSCALE);
        if (mask.empty()) continue;
        if (counter.empty())
            counter = cv::Mat::zeros(mask.size(), CV_16U);
        else if (mask.size() != counter.size())
            cv::resize(mask, mask, counter.size(), 0, 0, cv::INTER_NEAREST);
        cv::add(counter, 1, counter, mask > 127);
        ++nMasks;
    }
    if (nMasks == 0 || counter.empty()) return {};
    const int thresh = std::max(2, static_cast<int>(std::ceil(nMasks * voteFrac)));
    cv::Mat out = (counter >= thresh);   // 0/255, CV_8U
    if (cv::countNonZero(out) == 0) return {};
    return out;
}

int MaskGenerator::runGreen()
{
    int written = 0;
    const int nChunks = m_chunks.size();
    for (int ci = 0; ci < nChunks; ++ci) {
        if (m_stop.load()) return written;
        const int part = m_chunks[ci];
        const QString chunkPath = m_chunksDir
            + QStringLiteral("/video_part_%1.mp4").arg(part, 3, 10, QLatin1Char('0'));
        if (!QFileInfo::exists(chunkPath)) continue;

        const QString outDir = m_outDir
            + QStringLiteral("/green_mask/video_part_%1").arg(part, 3, 10, QLatin1Char('0'));
        QDir().mkpath(outDir);

        cv::VideoCapture cap;
        if (!cap.open(chunkPath.toStdString())) continue;
        const int total = std::max(1, static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)));

        cv::Mat frame;
        int f = 0;
        for (; ; ++f) {
            if (m_stop.load()) return written;
            if (!cap.read(frame) || frame.empty()) break;
            const cv::Mat mask = greenMask(frame);
            const QString out = outDir
                + QStringLiteral("/frame_%1.png").arg(f, 5, 10, QLatin1Char('0'));
            cv::imwrite(out.toStdString(), mask);
            ++written;
            if (f % 20 == 0)
                emit progressChanged(
                    (ci + double(f) / total) / std::max(1, nChunks),
                    QStringLiteral("Green · parte %1/%2 · frame %3/%4")
                        .arg(ci + 1).arg(nChunks).arg(f + 1).arg(total));
        }
    }
    return written;
}

int MaskGenerator::runStatic()
{
    int written = 0;
    const int nChunks = m_chunks.size();
    for (int ci = 0; ci < nChunks; ++ci) {
        if (m_stop.load()) return written;
        const int part = m_chunks[ci];
        const QString chunkPath = m_chunksDir
            + QStringLiteral("/video_part_%1.mp4").arg(part, 3, 10, QLatin1Char('0'));
        if (!QFileInfo::exists(chunkPath)) continue;

        emit progressChanged(double(ci) / std::max(1, nChunks),
                             QStringLiteral("Estáticos · parte %1/%2")
                                 .arg(ci + 1).arg(nChunks));

        const cv::Mat mask = staticMask(chunkPath, &m_stop);
        if (mask.empty()) continue;

        const QString outDir = m_outDir
            + QStringLiteral("/static_mask/video_part_%1").arg(part, 3, 10, QLatin1Char('0'));
        QDir().mkpath(outDir);
        cv::imwrite((outDir + QStringLiteral("/mask.png")).toStdString(), mask);
        ++written;
    }
    return written;
}

void MaskGenerator::run()
{
    if (m_chunks.isEmpty()) {
        emit finished(false, QStringLiteral("No hay chunks (video_part_*.mp4) para procesar"), 0);
        return;
    }
    const int written = (m_kind == Kind::Green) ? runGreen() : runStatic();
    if (m_stop.load()) { emit finished(false, QStringLiteral("Cancelado"), written); return; }
    if (written == 0) { emit finished(false, QStringLiteral("Sin resultados"), 0); return; }
    emit progressChanged(1.0, QStringLiteral("Listo"));
    emit finished(true, QString(), written);
}
