#include "YoloDetector.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace {
constexpr int   kInputSize  = 640;
constexpr float kConfThresh = 0.35f;
constexpr float kNmsThresh  = 0.45f;
}

QString YoloDetector::resolveModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/models/yolov8n.onnx"),
        appDir + QStringLiteral("/../../models/yolov8n.onnx"),
        QStringLiteral("models/yolov8n.onnx"),
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c))
            return QFileInfo(c).absoluteFilePath();
    }
    return {};
}

bool YoloDetector::load(QString *errorOut)
{
    const QString modelPath = resolveModelPath();
    if (modelPath.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("yolov8n.onnx not found (expected in models/)");
        return false;
    }
    try {
        m_net = cv::dnn::readNetFromONNX(modelPath.toStdString());
        m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    } catch (const cv::Exception &e) {
        if (errorOut)
            *errorOut = QStringLiteral("failed to load model: %1")
                            .arg(QString::fromStdString(e.msg));
        return false;
    }
    return true;
}

std::vector<YoloDetector::Detection> YoloDetector::detect(const cv::Mat &frame)
{
    const int origW = frame.cols;
    const int origH = frame.rows;
    const float r = std::min(static_cast<float>(kInputSize) / origW,
                             static_cast<float>(kInputSize) / origH);
    const int newW = static_cast<int>(std::round(origW * r));
    const int newH = static_cast<int>(std::round(origH * r));
    const int padX = (kInputSize - newW) / 2;
    const int padY = (kInputSize - newH) / 2;

    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(newW, newH));
    cv::Mat canvas(kInputSize, kInputSize, frame.type(), cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(padX, padY, newW, newH)));

    cv::Mat blob = cv::dnn::blobFromImage(canvas, 1.0 / 255.0,
                                          cv::Size(kInputSize, kInputSize),
                                          cv::Scalar(), /*swapRB=*/true, /*crop=*/false);
    m_net.setInput(blob);

    std::vector<cv::Mat> outputs;
    m_net.forward(outputs, m_net.getUnconnectedOutLayersNames());
    if (outputs.empty())
        return {};

    // Output [1, 4+80, anchors] -> [anchors, attrs]; person score at col 4.
    cv::Mat output = outputs[0];
    if (output.dims == 3) {
        output = cv::Mat(output.size[1], output.size[2], CV_32F,
                         const_cast<void *>(static_cast<const void *>(output.ptr<float>())))
                     .clone();
        cv::transpose(output, output);
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    for (int i = 0; i < output.rows; ++i) {
        const float *row = output.ptr<float>(i);
        const float personScore = row[4];
        if (personScore < kConfThresh)
            continue;
        const float cx = row[0], cy = row[1], w = row[2], h = row[3];
        boxes.emplace_back(static_cast<int>((cx - w * 0.5f - padX) / r),
                           static_cast<int>((cy - h * 0.5f - padY) / r),
                           static_cast<int>(w / r),
                           static_cast<int>(h / r));
        scores.push_back(personScore);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, scores, kConfThresh, kNmsThresh, keep);

    std::vector<Detection> result;
    const cv::Rect frameRect(0, 0, origW, origH);
    for (int k : keep) {
        const cv::Rect clipped = boxes[k] & frameRect;
        if (clipped.area() > 0)
            result.push_back({clipped, scores[k]});
    }
    return result;
}
