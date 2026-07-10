#ifndef YOLODETECTOR_H
#define YOLODETECTOR_H

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <QString>
#include <vector>

// YOLOv8 (Ultralytics COCO export) person detector on cv::dnn, shared by
// the interactive TrackingManager and the offline chunk-tracking op.
// Letterbox + decode pattern follows videopp's PoseDetector.
class YoloDetector
{
public:
    struct Detection
    {
        cv::Rect box;
        float    conf{0.f};
    };

    // Locates models/yolov8n.onnx near the executable. Empty if not found.
    static QString resolveModelPath();

    // Returns false and fills errorOut if the model cannot be loaded.
    bool load(QString *errorOut = nullptr);
    bool isLoaded() const { return !m_net.empty(); }

    std::vector<Detection> detect(const cv::Mat &frame);

private:
    cv::dnn::Net m_net;
};

#endif
