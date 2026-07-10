#ifndef ROSTERMODEL_H
#define ROSTERMODEL_H

#include <QAbstractListModel>
#include <QJsonArray>

struct Player
{
    int     number{0};
    QString name;
    QString position;
};

// Editable roster for one team, exposed to QML (Metadata tab table and
// the tagging dropdown).
class RosterModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NumberRole = Qt::UserRole + 1,
        NameRole,
        PositionRole,
    };

    explicit RosterModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addPlayer();
    Q_INVOKABLE void removePlayer(int row);
    Q_INVOKABLE void set(int row, const QString &field, const QVariant &value);
    Q_INVOKABLE QVariantMap get(int row) const;

    void setPlayers(const QVector<Player> &players);
    const QVector<Player> &players() const { return m_players; }

    QJsonArray toJson() const;
    void fromJson(const QJsonArray &array);

signals:
    void countChanged();
    void edited();

private:
    QVector<Player> m_players;
};

#endif
