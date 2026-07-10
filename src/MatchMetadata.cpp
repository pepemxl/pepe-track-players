#include "MatchMetadata.h"

MatchMetadata::MatchMetadata(QObject *parent)
    : QObject(parent)
{
    m_homeTeam    = QStringLiteral("Halcones FC");
    m_awayTeam    = QStringLiteral("Cóndores");
    m_league      = QStringLiteral("Liga Regional");
    m_season      = QStringLiteral("2025–2026");
    m_competition = QStringLiteral("Liga Regional Apertura — Fecha 8");
    m_date        = QStringLiteral("2026-07-05");
    m_venue       = QStringLiteral("Estadio Municipal Norte");
    m_referee     = QStringLiteral("S. Villalba");
}

QJsonObject MatchMetadata::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("homeTeam")]    = m_homeTeam;
    o[QStringLiteral("awayTeam")]    = m_awayTeam;
    o[QStringLiteral("league")]      = m_league;
    o[QStringLiteral("season")]      = m_season;
    o[QStringLiteral("competition")] = m_competition;
    o[QStringLiteral("date")]        = m_date;
    o[QStringLiteral("venue")]       = m_venue;
    o[QStringLiteral("referee")]     = m_referee;
    return o;
}

void MatchMetadata::fromJson(const QJsonObject &o)
{
    auto s = [&o](const char *key, const QString &fallback) {
        const QJsonValue v = o[QLatin1String(key)];
        return v.isString() ? v.toString() : fallback;
    };
    m_homeTeam    = s("homeTeam", m_homeTeam);
    m_awayTeam    = s("awayTeam", m_awayTeam);
    m_league      = s("league", m_league);
    m_season      = s("season", m_season);
    m_competition = s("competition", m_competition);
    m_date        = s("date", m_date);
    m_venue       = s("venue", m_venue);
    m_referee     = s("referee", m_referee);
    emit changed();
}
