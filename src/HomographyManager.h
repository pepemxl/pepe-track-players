#ifndef HOMOGRAPHYMANAGER_H
#define HOMOGRAPHYMANAGER_H

#include <opencv2/opencv.hpp>
#include <QObject>
#include <QVariantList>
#include <QPointF>
#include <QJsonObject>

// Image<->pitch calibration from the 4 corner correspondences A/B/C/D.
// Image points are in video pixel coordinates; pitch points in meters
// (105 x 68 field). Recompute runs cv::findHomography and reports the
// mean reprojection error in image pixels.
class HomographyManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList points READ points NOTIFY pointsChanged)
    Q_PROPERTY(bool verified READ verified NOTIFY stateChanged)
    Q_PROPERTY(double reprojError READ reprojError NOTIFY stateChanged)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayChanged)

public:
    struct Correspondence
    {
        QString id;         // "A".."D"
        QPointF image;      // video pixels
        QPointF pitch;      // meters
    };

    explicit HomographyManager(QObject *parent = nullptr);

    QVariantList points() const;
    bool verified() const { return m_verified; }
    double reprojError() const { return m_reprojError; }
    bool overlayEnabled() const { return m_overlayEnabled; }
    void setOverlayEnabled(bool on);

    // Called when a video is loaded so default points land inside the frame.
    void setImageSize(int width, int height);

    Q_INVOKABLE void setImagePoint(const QString &id, double x, double y);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void recompute();
    Q_INVOKABLE QPointF imageToPitch(double x, double y) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

signals:
    void pointsChanged();
    void stateChanged();
    void overlayChanged();
    void edited();

private:
    void applyDefaults();

    QVector<Correspondence> m_points;
    cv::Mat m_H;              // image -> pitch
    bool    m_verified{false};
    double  m_reprojError{0.0};
    bool    m_overlayEnabled{true};
    int     m_imageWidth{1920};
    int     m_imageHeight{1080};
    bool    m_touched{false}; // user moved a point since defaults were applied
};

#endif
