#pragma once
#include <QDateTime>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Drive identity and SMART health for the physical disk behind a directory, read through
// udisks2 over D-Bus (no root needed: udisks handles the privileged part for local sessions).
class SmartInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString targetDir READ targetDir WRITE setTargetDir NOTIFY targetDirChanged)
    Q_PROPERTY(QVariantMap drive READ drive NOTIFY changed)          // model, serial, firmware, size, bus, kind
    Q_PROPERTY(QVariantList attributes READ attributes NOTIFY changed) // [{name, value, level}] level: ok|warn|crit|info
    Q_PROPERTY(QString health READ health NOTIFY changed)            // Good | Caution | Bad | Unknown
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString updated READ updated NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit SmartInfo(QObject *parent = nullptr);

    QString targetDir() const { return m_targetDir; }
    void setTargetDir(const QString &dir);
    QVariantMap drive() const { return m_drive; }
    QVariantList attributes() const { return m_attributes; }
    QString health() const { return m_health; }
    QString summary() const { return m_summary; }
    QString updated() const { return m_updated; }
    QString error() const { return m_error; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void refresh();             // asks the drive for fresh SMART data, then re-reads
    QString textReport() const;             // for --smart

signals:
    void targetDirChanged();
    void changed();
    void busyChanged();

private:
    void read(bool update);
    void add(const QString &name, const QString &value, const QString &level = QStringLiteral("ok"));
    QString diskFor(const QString &dir) const;   // e.g. "nvme0n1"

    QString m_targetDir, m_health = QStringLiteral("Unknown"), m_summary, m_updated, m_error;
    QVariantMap m_drive;
    QVariantList m_attributes;
    bool m_busy = false;
};
