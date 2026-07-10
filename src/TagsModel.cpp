#include "TagsModel.h"

#include <QJsonObject>
#include <algorithm>

TagsModel::TagsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TagsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tags.size();
}

QVariant TagsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tags.size())
        return {};
    const TagEvent &t = m_tags.at(index.row());
    switch (role) {
    case FrameRole:        return t.frame;
    case TimecodeRole:     return t.timecode;
    case PlayerNumberRole: return t.playerNumber;
    case PlayerNameRole:   return t.playerName;
    case TeamRole:         return t.team;
    case XRole:            return t.x;
    case YRole:            return t.y;
    case PitchXRole:       return t.pitchX;
    case PitchYRole:       return t.pitchY;
    case HasPitchRole:     return t.hasPitch;
    }
    return {};
}

QHash<int, QByteArray> TagsModel::roleNames() const
{
    return {
        { FrameRole,        "frame" },
        { TimecodeRole,     "timecode" },
        { PlayerNumberRole, "playerNumber" },
        { PlayerNameRole,   "playerName" },
        { TeamRole,         "team" },
        { XRole,            "x" },
        { YRole,            "y" },
        { PitchXRole,       "pitchX" },
        { PitchYRole,       "pitchY" },
        { HasPitchRole,     "hasPitch" },
    };
}

int TagsModel::addTag(const TagEvent &tag)
{
    TagEvent t = tag;
    if (t.id <= 0)
        t.id = m_nextId++;
    else
        m_nextId = std::max(m_nextId, t.id + 1);
    // Newest first, like an event log.
    beginInsertRows(QModelIndex(), 0, 0);
    m_tags.prepend(t);
    endInsertRows();
    emit countChanged();
    emit edited();
    return t.id;
}

void TagsModel::insertTag(int row, const TagEvent &tag)
{
    row = std::clamp(row, 0, static_cast<int>(m_tags.size()));
    TagEvent t = tag;
    if (t.id <= 0)
        t.id = m_nextId++;
    else
        m_nextId = std::max(m_nextId, t.id + 1);
    beginInsertRows(QModelIndex(), row, row);
    m_tags.insert(row, t);
    endInsertRows();
    emit countChanged();
    emit edited();
}

int TagsModel::rowById(int id) const
{
    for (int i = 0; i < m_tags.size(); ++i) {
        if (m_tags.at(i).id == id)
            return i;
    }
    return -1;
}

bool TagsModel::removeById(int id)
{
    const int row = rowById(id);
    if (row < 0)
        return false;
    removeTag(row);
    return true;
}

void TagsModel::removeTag(int row)
{
    if (row < 0 || row >= m_tags.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_tags.removeAt(row);
    endRemoveRows();
    emit countChanged();
    emit edited();
}

QJsonArray TagsModel::toJson() const
{
    QJsonArray array;
    for (const TagEvent &t : m_tags) {
        QJsonObject o;
        o[QStringLiteral("frame")]        = t.frame;
        o[QStringLiteral("timecode")]     = t.timecode;
        o[QStringLiteral("playerNumber")] = t.playerNumber;
        o[QStringLiteral("playerName")]   = t.playerName;
        o[QStringLiteral("team")]         = t.team;
        o[QStringLiteral("x")]            = t.x;
        o[QStringLiteral("y")]            = t.y;
        o[QStringLiteral("pitchX")]       = t.pitchX;
        o[QStringLiteral("pitchY")]       = t.pitchY;
        o[QStringLiteral("hasPitch")]     = t.hasPitch;
        array.append(o);
    }
    return array;
}

void TagsModel::fromJson(const QJsonArray &array)
{
    beginResetModel();
    m_tags.clear();
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        TagEvent t;
        t.id           = m_nextId++;
        t.frame        = o[QStringLiteral("frame")].toInt();
        t.timecode     = o[QStringLiteral("timecode")].toString();
        t.playerNumber = o[QStringLiteral("playerNumber")].toInt();
        t.playerName   = o[QStringLiteral("playerName")].toString();
        t.team         = o[QStringLiteral("team")].toInt();
        t.x            = o[QStringLiteral("x")].toDouble();
        t.y            = o[QStringLiteral("y")].toDouble();
        t.pitchX       = o[QStringLiteral("pitchX")].toDouble();
        t.pitchY       = o[QStringLiteral("pitchY")].toDouble();
        t.hasPitch     = o[QStringLiteral("hasPitch")].toBool();
        m_tags.append(t);
    }
    endResetModel();
    emit countChanged();
}
