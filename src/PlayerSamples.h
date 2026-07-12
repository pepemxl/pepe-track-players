#ifndef PLAYERSAMPLES_H
#define PLAYERSAMPLES_H

#include <QObject>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVector>

// Per-video reference appearance samples used to help identify players,
// referees and goalkeepers (and to seed the player-id inference). The user
// crops a box on a chosen frame for one of six roles; each sample stores its
// role, source frame, bounding box (video pixels) and a thumbnail PNG.
//
// Persisted next to the other per-video project data as player_samples.json
// with the thumbnails under player_samples/. Saved immediately on edit.
class PlayerSamples : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList samples READ samples NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int roleCount READ roleCountProp CONSTANT)

public:
    enum Role {
        TeamAPlayer = 0,
        TeamBPlayer,
        CentralRef,
        AssistantRef,
        GoalkeeperA,
        GoalkeeperB,
        RoleCount
    };
    Q_ENUM(Role)

    explicit PlayerSamples(QObject *parent = nullptr);

    struct Sample {
        int     id{0};
        int     role{0};
        int     frame{0};
        QRectF  rect;        // video pixels
        QString thumb;       // path relative to the base dir
    };

    QVariantList samples() const;
    int count() const { return m_samples.size(); }
    int roleCountProp() const { return RoleCount; }

    Q_INVOKABLE QVariantList forRole(int role) const;
    Q_INVOKABLE int countForRole(int role) const;
    Q_INVOKABLE QString roleName(int role) const;
    Q_INVOKABLE QString roleKey(int role) const;

    // Base dir = the per-video project folder. Reloads if it changed.
    void setBaseDir(const QString &dir);
    QString baseDir() const { return m_baseDir; }
    QString thumbDir() const { return m_thumbDir; }

    // Records a sample (relThumb relative to the base dir) and persists.
    int add(int role, int frame, const QRectF &rect, const QString &relThumb);
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void clearRole(int role);

signals:
    void changed();

private:
    QVariantMap toMap(const Sample &s) const;
    void load();
    void save() const;

    QString m_baseDir;
    QString m_jsonPath;
    QString m_thumbDir;
    QVector<Sample> m_samples;
    int m_nextId{1};
};

#endif
