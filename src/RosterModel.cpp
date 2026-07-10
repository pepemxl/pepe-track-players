#include "RosterModel.h"

#include <QJsonObject>

RosterModel::RosterModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RosterModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_players.size();
}

QVariant RosterModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_players.size())
        return {};
    const Player &p = m_players.at(index.row());
    switch (role) {
    case NumberRole:   return p.number;
    case NameRole:     return p.name;
    case PositionRole: return p.position;
    }
    return {};
}

QHash<int, QByteArray> RosterModel::roleNames() const
{
    return {
        { NumberRole,   "number" },
        { NameRole,     "name" },
        { PositionRole, "position" },
    };
}

void RosterModel::addPlayer()
{
    beginInsertRows(QModelIndex(), m_players.size(), m_players.size());
    m_players.append(Player{0, QStringLiteral("New player"), QStringLiteral("—")});
    endInsertRows();
    emit countChanged();
    emit edited();
}

void RosterModel::removePlayer(int row)
{
    if (row < 0 || row >= m_players.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_players.removeAt(row);
    endRemoveRows();
    emit countChanged();
    emit edited();
}

void RosterModel::set(int row, const QString &field, const QVariant &value)
{
    if (row < 0 || row >= m_players.size())
        return;
    Player &p = m_players[row];
    int role = -1;
    if (field == QLatin1String("number")) {
        p.number = value.toInt();
        role = NumberRole;
    } else if (field == QLatin1String("name")) {
        p.name = value.toString();
        role = NameRole;
    } else if (field == QLatin1String("position")) {
        p.position = value.toString();
        role = PositionRole;
    }
    if (role >= 0) {
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {role});
        emit edited();
    }
}

QVariantMap RosterModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_players.size())
        return map;
    const Player &p = m_players.at(row);
    map[QStringLiteral("number")]   = p.number;
    map[QStringLiteral("name")]     = p.name;
    map[QStringLiteral("position")] = p.position;
    return map;
}

void RosterModel::setPlayers(const QVector<Player> &players)
{
    beginResetModel();
    m_players = players;
    endResetModel();
    emit countChanged();
}

QJsonArray RosterModel::toJson() const
{
    QJsonArray array;
    for (const Player &p : m_players) {
        QJsonObject o;
        o[QStringLiteral("number")]   = p.number;
        o[QStringLiteral("name")]     = p.name;
        o[QStringLiteral("position")] = p.position;
        array.append(o);
    }
    return array;
}

void RosterModel::fromJson(const QJsonArray &array)
{
    QVector<Player> players;
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        players.append(Player{
            o[QStringLiteral("number")].toInt(),
            o[QStringLiteral("name")].toString(),
            o[QStringLiteral("position")].toString(),
        });
    }
    setPlayers(players);
}
