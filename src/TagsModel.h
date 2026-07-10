#ifndef TAGSMODEL_H
#define TAGSMODEL_H

#include <QAbstractListModel>
#include <QJsonArray>

struct TagEvent
{
    int     frame{0};
    QString timecode;
    int     playerNumber{0};
    QString playerName;
    int     team{0};       // 0 = home, 1 = away
    double  x{0.0};        // video pixel coordinates
    double  y{0.0};
    double  pitchX{0.0};   // meters, valid when hasPitch
    double  pitchY{0.0};
    bool    hasPitch{false};
};

// Tag events created by clicking the video with a player selected.
class TagsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        FrameRole = Qt::UserRole + 1,
        TimecodeRole,
        PlayerNumberRole,
        PlayerNameRole,
        TeamRole,
        XRole,
        YRole,
        PitchXRole,
        PitchYRole,
        HasPitchRole,
    };

    explicit TagsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addTag(const TagEvent &tag);
    Q_INVOKABLE void removeTag(int row);

    const QVector<TagEvent> &tags() const { return m_tags; }

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &array);

signals:
    void countChanged();
    void edited();

private:
    QVector<TagEvent> m_tags;
};

#endif
