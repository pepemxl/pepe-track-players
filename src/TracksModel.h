#ifndef TRACKSMODEL_H
#define TRACKSMODEL_H

#include <QAbstractListModel>
#include <QVariantList>

// Read-only table of tracks produced by the TrackingManager worker.
// Rows arrive as QVariantMaps: trackId, name, framesTracked, avgConf,
// status ("stable"/"flickering"/"lost Nx"), statusKind (0 ok, 1 warn, 2 bad).
class TracksModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        TrackIdRole = Qt::UserRole + 1,
        NameRole,
        FramesTrackedRole,
        AvgConfRole,
        StatusRole,
        StatusKindRole,
    };

    explicit TracksModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QVariantList &rows);
    const QVariantList &rows() const { return m_rows; }

signals:
    void countChanged();

private:
    QVariantList m_rows;
};

#endif
