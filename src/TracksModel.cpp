#include "TracksModel.h"

#include <QVariantMap>

TracksModel::TracksModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TracksModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant TracksModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const QVariantMap row = m_rows.at(index.row()).toMap();
    switch (role) {
    case TrackIdRole:       return row.value(QStringLiteral("trackId"));
    case NameRole:          return row.value(QStringLiteral("name"));
    case FramesTrackedRole: return row.value(QStringLiteral("framesTracked"));
    case AvgConfRole:       return row.value(QStringLiteral("avgConf"));
    case StatusRole:        return row.value(QStringLiteral("status"));
    case StatusKindRole:    return row.value(QStringLiteral("statusKind"));
    }
    return {};
}

QHash<int, QByteArray> TracksModel::roleNames() const
{
    return {
        { TrackIdRole,       "trackId" },
        { NameRole,          "name" },
        { FramesTrackedRole, "framesTracked" },
        { AvgConfRole,       "avgConf" },
        { StatusRole,        "status" },
        { StatusKindRole,    "statusKind" },
    };
}

void TracksModel::setRows(const QVariantList &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}
