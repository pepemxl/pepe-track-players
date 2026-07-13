#ifndef MATCHMETADATA_H
#define MATCHMETADATA_H

#include <QObject>
#include <QString>
#include <QJsonObject>

#define META_PROP(name, Name)                                             \
    Q_PROPERTY(QString name READ name WRITE set##Name NOTIFY changed)     \
public:                                                                   \
    QString name() const { return m_##name; }                             \
    void set##Name(const QString &v)                                      \
    {                                                                     \
        if (m_##name == v) return;                                        \
        m_##name = v;                                                     \
        emit changed();                                                   \
    }                                                                     \
private:                                                                  \
    QString m_##name;

// Match-level form fields for the Metadata tab.
class MatchMetadata : public QObject
{
    Q_OBJECT
    META_PROP(homeTeam, HomeTeam)
    META_PROP(awayTeam, AwayTeam)
    META_PROP(league, League)
    META_PROP(season, Season)
    META_PROP(competition, Competition)
    META_PROP(date, Date)
    META_PROP(venue, Venue)
    META_PROP(referee, Referee)
    // Starting tactical formation, "-"-joined integers (e.g. "4-1-3-2").
    META_PROP(homeFormation, HomeFormation)
    META_PROP(awayFormation, AwayFormation)

public:
    explicit MatchMetadata(QObject *parent = nullptr);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

signals:
    void changed();
};

#undef META_PROP

#endif
