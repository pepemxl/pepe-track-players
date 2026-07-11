#ifndef HOMOGRAPHYMANAGER_H
#define HOMOGRAPHYMANAGER_H

#include <opencv2/opencv.hpp>
#include <QObject>
#include <QVariantList>
#include <QPointF>
#include <QJsonObject>
#include <QVector>
#include <array>

// Image<->pitch calibration from the 4 corner correspondences A/B/C/D.
// Image points are in video pixel coordinates; pitch points in meters
// (105 x 68 field). The homography is solved with a direct 4-point DLT
// (cv::solve) because this OpenCV build ships without calib3d.
//
// Cameras are not static, so a single homography is only valid at the
// instant it was calibrated. This manager keeps a *track* of manual
// keyframes — each one is the 4 image points at a specific frame — and
// interpolates the image points linearly between the surrounding
// keyframes to produce H(frame) for any frame (phase F1 of docs/
// homography.md). With a single keyframe it degrades to the old constant
// homography.
class HomographyManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList points READ points NOTIFY pointsChanged)
    Q_PROPERTY(bool verified READ verified NOTIFY stateChanged)
    Q_PROPERTY(double reprojError READ reprojError NOTIFY stateChanged)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayChanged)
    // Manual keyframe track.
    Q_PROPERTY(QVariantList keyframes READ keyframes NOTIFY keyframesChanged)
    Q_PROPERTY(int keyframeCount READ keyframeCount NOTIFY keyframesChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY stateChanged)
    Q_PROPERTY(bool atKeyframe READ atKeyframe NOTIFY stateChanged)
    // Inter-frame propagation (phase F2): a dense per-frame track computed by
    // HomographyWorker (optical flow). When present, homographyAt uses it
    // instead of the linear keyframe interpolation.
    Q_PROPERTY(bool propagated READ propagated NOTIFY propagationChanged)
    Q_PROPERTY(bool propagating READ propagating NOTIFY propagationChanged)
    Q_PROPERTY(double propProgress READ propProgress NOTIFY propagationChanged)
    Q_PROPERTY(QString propLabel READ propLabel NOTIFY propagationChanged)
    Q_PROPERTY(bool atPropagated READ atPropagated NOTIFY stateChanged)

public:
    struct Correspondence
    {
        QString id;         // "A".."D"
        QPointF image;      // video pixels (working buffer for currentFrame)
        QPointF pitch;      // meters
    };

    // 4 image points (A,B,C,D order) captured at a given frame.
    struct Keyframe
    {
        int     frame{0};
        QPointF image[4];
        bool    verified{false};
        double  reprojError{0.0};
    };

    explicit HomographyManager(QObject *parent = nullptr);

    QVariantList points() const;
    bool verified() const { return m_verified; }
    double reprojError() const { return m_reprojError; }
    bool overlayEnabled() const { return m_overlayEnabled; }
    void setOverlayEnabled(bool on);

    QVariantList keyframes() const;
    int keyframeCount() const { return m_keyframes.size(); }
    int currentFrame() const { return m_currentFrame; }
    bool atKeyframe() const;

    bool propagated() const { return !m_dense.isEmpty(); }
    bool propagating() const { return m_propagating; }
    double propProgress() const { return m_propProgress; }
    QString propLabel() const { return m_propLabel; }
    bool atPropagated() const;

    // Read-only access to the keyframes for the propagation worker.
    const QVector<Keyframe> &keyframeData() const { return m_keyframes; }

    // Progress hooks driven by AppController while the worker runs.
    void setPropagating(bool on, const QString &label = QString());
    void setPropProgress(double frac, const QString &label);
    // Load / attach a dense track (4 image points per frame from start).
    void applyDenseTrack(int start, const QVector<std::array<QPointF, 4>> &dense);
    Q_INVOKABLE bool loadDenseTrack(const QString &path);
    Q_INVOKABLE void clearPropagation();

    // Called when a video is loaded so default points land inside the frame.
    void setImageSize(int width, int height);
    // The frame currently displayed: refreshes the interpolated working
    // points and H so overlays and tagging follow the moving camera.
    void setCurrentFrame(int frame);

    Q_INVOKABLE void setImagePoint(const QString &id, double x, double y);
    // Reassign which pitch landmark a reference point maps to. The 4 points
    // default to the field corners, but any 4 non-collinear landmarks work
    // (useful when the corners are off-screen). Applies to every keyframe.
    Q_INVOKABLE void setPitchPoint(const QString &id, double px, double py);
    Q_INVOKABLE void setPitchLandmark(const QString &id, const QString &key);
    // Catalog of standard pitch landmarks: [{key,label,px,py}] (105x68 m).
    Q_INVOKABLE QVariantList pitchLandmarks() const;
    Q_INVOKABLE void reset();
    // Solves H from the current points and stores it as a verified keyframe
    // at the current frame (creating or replacing it).
    Q_INVOKABLE void recompute();
    Q_INVOKABLE void removeKeyframe(int frame);

    // H for a given frame (interpolated / clamped across keyframes).
    cv::Mat homographyAt(int frame) const;
    // Current-frame mapping (uses the working H kept in sync with currentFrame).
    Q_INVOKABLE QPointF imageToPitch(double x, double y) const;
    // Explicit per-frame mapping.
    Q_INVOKABLE QPointF imageToPitchAt(int frame, double x, double y) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

signals:
    void pointsChanged();
    void stateChanged();
    void overlayChanged();
    void keyframesChanged();
    void propagationChanged();
    void edited();

private:
    void applyDefaults();
    // 4-point DLT; optionally reports the mean reprojection error (px).
    cv::Mat solveH(const QPointF img[4], double *reprojErrPx = nullptr) const;
    void interpolatedImagePoints(int frame, QPointF out[4]) const;
    // Image points for a frame: dense track if propagated, else interpolation.
    void imagePointsAt(int frame, QPointF out[4]) const;
    void refreshForCurrentFrame();
    void upsertKeyframe(int frame, const QPointF img[4], bool verified, double err);

    QVector<Correspondence> m_points;   // pitch = fixed corners; image = working buffer
    QVector<Keyframe>       m_keyframes; // sorted by frame
    int     m_currentFrame{0};
    cv::Mat m_H;              // image -> pitch for the current frame
    bool    m_verified{false};
    double  m_reprojError{0.0};
    bool    m_overlayEnabled{true};
    int     m_imageWidth{1920};
    int     m_imageHeight{1080};
    bool    m_touched{false}; // user moved a point since defaults were applied

    // Dense per-frame track (phase F2). Empty when not propagated.
    int     m_denseStart{0};
    QVector<std::array<QPointF, 4>> m_dense;
    bool    m_propagating{false};
    double  m_propProgress{0.0};
    QString m_propLabel;
};

#endif
