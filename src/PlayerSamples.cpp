#include "PlayerSamples.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {

struct RoleInfo { const char *key; const char *name; };
const RoleInfo kRoles[] = {
    {"team_a_player", "Team A player"},
    {"team_b_player", "Team B player"},
    {"central_ref",   "Central referee"},
    {"assistant_ref", "Assistant referee"},
    {"gk_a",          "Goalkeeper A"},
    {"gk_b",          "Goalkeeper B"},
    {"team_a_lineup", "Team A line-up"},
    {"team_b_lineup", "Team B line-up"},
};

} // namespace

PlayerSamples::PlayerSamples(QObject *parent) : QObject(parent) {}

QString PlayerSamples::roleName(int role) const
{
    return (role >= 0 && role < RoleCount) ? QString::fromLatin1(kRoles[role].name)
                                           : QString();
}

QString PlayerSamples::roleKey(int role) const
{
    return (role >= 0 && role < RoleCount) ? QString::fromLatin1(kRoles[role].key)
                                           : QString();
}

QVariantMap PlayerSamples::toMap(const Sample &s) const
{
    QVariantMap m;
    m[QStringLiteral("id")]       = s.id;
    m[QStringLiteral("role")]     = s.role;
    m[QStringLiteral("roleName")] = roleName(s.role);
    m[QStringLiteral("roleKey")]  = roleKey(s.role);
    m[QStringLiteral("frame")]    = s.frame;
    m[QStringLiteral("x")]        = s.rect.x();
    m[QStringLiteral("y")]        = s.rect.y();
    m[QStringLiteral("w")]        = s.rect.width();
    m[QStringLiteral("h")]        = s.rect.height();
    m[QStringLiteral("thumb")]    = s.thumb;
    // Absolute file URL for the QML Image.
    m[QStringLiteral("thumbUrl")] = s.thumb.isEmpty() || m_baseDir.isEmpty()
        ? QString()
        : QUrl::fromLocalFile(m_baseDir + QLatin1Char('/') + s.thumb).toString();
    return m;
}

QVariantList PlayerSamples::samples() const
{
    QVariantList list;
    for (const Sample &s : m_samples)
        list.append(toMap(s));
    return list;
}

QVariantList PlayerSamples::forRole(int role) const
{
    QVariantList list;
    for (const Sample &s : m_samples)
        if (s.role == role)
            list.append(toMap(s));
    return list;
}

int PlayerSamples::countForRole(int role) const
{
    int n = 0;
    for (const Sample &s : m_samples)
        if (s.role == role) ++n;
    return n;
}

void PlayerSamples::setBaseDir(const QString &dir)
{
    if (dir == m_baseDir)
        return;
    m_baseDir = dir;
    m_jsonPath = dir.isEmpty() ? QString()
                               : dir + QStringLiteral("/player_samples.json");
    m_thumbDir = dir.isEmpty() ? QString()
                               : dir + QStringLiteral("/player_samples");
    load();
}

int PlayerSamples::add(int role, int frame, const QRectF &rect, const QString &relThumb)
{
    if (role < 0 || role >= RoleCount)
        return -1;
    Sample s;
    s.id = m_nextId++;
    s.role = role;
    s.frame = frame;
    s.rect = rect;
    s.thumb = relThumb;
    m_samples.append(s);
    save();
    emit changed();
    return s.id;
}

void PlayerSamples::remove(int id)
{
    for (int i = 0; i < m_samples.size(); ++i) {
        if (m_samples[i].id == id) {
            // Delete the thumbnail file too.
            if (!m_baseDir.isEmpty() && !m_samples[i].thumb.isEmpty())
                QFile::remove(m_baseDir + QLatin1Char('/') + m_samples[i].thumb);
            m_samples.removeAt(i);
            save();
            emit changed();
            return;
        }
    }
}

void PlayerSamples::clearRole(int role)
{
    bool any = false;
    for (int i = m_samples.size() - 1; i >= 0; --i) {
        if (m_samples[i].role == role) {
            if (!m_baseDir.isEmpty() && !m_samples[i].thumb.isEmpty())
                QFile::remove(m_baseDir + QLatin1Char('/') + m_samples[i].thumb);
            m_samples.removeAt(i);
            any = true;
        }
    }
    if (any) {
        save();
        emit changed();
    }
}

void PlayerSamples::load()
{
    m_samples.clear();
    m_nextId = 1;
    if (!m_jsonPath.isEmpty()) {
        QFile f(m_jsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonArray arr = QJsonDocument::fromJson(f.readAll())
                                       .object()[QStringLiteral("samples")].toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                Sample s;
                s.id    = o[QStringLiteral("id")].toInt();
                s.role  = o[QStringLiteral("role")].toInt();
                s.frame = o[QStringLiteral("frame")].toInt();
                s.rect  = QRectF(o[QStringLiteral("x")].toDouble(),
                                 o[QStringLiteral("y")].toDouble(),
                                 o[QStringLiteral("w")].toDouble(),
                                 o[QStringLiteral("h")].toDouble());
                s.thumb = o[QStringLiteral("thumb")].toString();
                m_samples.append(s);
                m_nextId = qMax(m_nextId, s.id + 1);
            }
        }
    }
    emit changed();
}

void PlayerSamples::save() const
{
    if (m_jsonPath.isEmpty())
        return;
    QDir().mkpath(m_baseDir);
    QJsonArray arr;
    for (const Sample &s : m_samples) {
        QJsonObject o;
        o[QStringLiteral("id")]    = s.id;
        o[QStringLiteral("role")]  = s.role;
        o[QStringLiteral("roleKey")] = roleKey(s.role);
        o[QStringLiteral("frame")] = s.frame;
        o[QStringLiteral("x")]     = s.rect.x();
        o[QStringLiteral("y")]     = s.rect.y();
        o[QStringLiteral("w")]     = s.rect.width();
        o[QStringLiteral("h")]     = s.rect.height();
        o[QStringLiteral("thumb")] = s.thumb;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("samples")] = arr;
    QFile f(m_jsonPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
