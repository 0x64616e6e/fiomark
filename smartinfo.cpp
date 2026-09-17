#include "smartinfo.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUrl>

namespace {
const QString kService = QStringLiteral("org.freedesktop.UDisks2");
const QString kProps = QStringLiteral("org.freedesktop.DBus.Properties");

QVariant prop(const QString &path, const QString &iface, const QString &name)
{
    QDBusInterface p(kService, path, kProps, QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> r = p.call(QStringLiteral("Get"), iface, name);
    return r.isValid() ? r.value().variant() : QVariant();
}

bool hasInterface(const QString &path, const QString &iface)
{
    QDBusInterface p(kService, path, kProps, QDBusConnection::systemBus());
    QDBusReply<QVariantMap> r = p.call(QStringLiteral("GetAll"), iface);
    return r.isValid();
}

QVariantMap callMap(const QString &path, const QString &iface, const QString &method)
{
    QDBusInterface i(kService, path, iface, QDBusConnection::systemBus());
    QDBusReply<QVariantMap> r = i.call(method, QVariantMap());
    return r.isValid() ? r.value() : QVariantMap();
}

QString bytesHuman(double b)
{
    const char *u[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int i = 0;
    while (b >= 1000.0 && i < 5) { b /= 1000.0; ++i; }
    return QStringLiteral("%1 %2").arg(b, 0, 'f', i >= 3 ? 2 : 0).arg(QString::fromLatin1(u[i]));
}

QString kelvinC(double k) { return k > 0 ? QStringLiteral("%1 °C").arg(k - 273.15, 0, 'f', 0) : QStringLiteral("n/a"); }
QString hours(qulonglong h) { return QStringLiteral("%1 h  (%2 days)").arg(h).arg(h / 24.0, 0, 'f', 1); }
} // namespace

SmartInfo::SmartInfo(QObject *parent) : QObject(parent) { m_targetDir = QDir::homePath(); }

void SmartInfo::setTargetDir(const QString &dir)
{
    QString d = dir;
    if (d.startsWith(QStringLiteral("file://")))
        d = QUrl(d).toLocalFile();
    if (d == m_targetDir)
        return;
    m_targetDir = d;
    emit targetDirChanged();
    read(false);
}

QString SmartInfo::diskFor(const QString &dir) const
{
    const QStorageInfo st(dir);
    if (!st.isValid())
        return {};
    QProcess p;
    p.start(QStringLiteral("lsblk"), {QStringLiteral("-nlso"), QStringLiteral("NAME,TYPE"), QString::fromUtf8(st.device())});   // -l: no tree glyphs
    if (!p.waitForFinished(3000))
        return {};
    QString disk;
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split('\n')) {
        const QStringList f = line.simplified().split(' ');
        if (f.size() == 2 && f[1] == QLatin1String("disk"))
            disk = f[0];                  // the last "disk" in the chain is the physical device
    }
    return disk;
}

void SmartInfo::add(const QString &name, const QString &value, const QString &level)
{
    QVariantMap row;
    row[QStringLiteral("name")] = name;
    row[QStringLiteral("value")] = value;
    row[QStringLiteral("level")] = level;
    m_attributes.append(row);
}

void SmartInfo::refresh()
{
    if (m_busy) return;
    m_busy = true; emit busyChanged();
    read(true);
    m_busy = false; emit busyChanged();
}

void SmartInfo::read(bool update)
{
    m_drive.clear(); m_attributes.clear(); m_error.clear(); m_summary.clear();
    m_health = QStringLiteral("Unknown");
    int warn = 0, crit = 0;

    const QString disk = diskFor(m_targetDir);
    if (disk.isEmpty()) { m_error = QStringLiteral("could not resolve the directory to a disk"); emit changed(); return; }
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_]+$"));
    if (!safe.match(disk).hasMatch()) { m_error = QStringLiteral("unexpected disk name '%1'").arg(disk); emit changed(); return; }
    const QString blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + disk;
    const QString drivePath = prop(blockPath, QStringLiteral("org.freedesktop.UDisks2.Block"), QStringLiteral("Drive")).value<QDBusObjectPath>().path();
    if (drivePath.isEmpty() || drivePath == QLatin1String("/")) {
        m_error = QStringLiteral("udisks2 does not know /dev/%1 (is udisks2 running?)").arg(disk); emit changed(); return;
    }
    const QString D = QStringLiteral("org.freedesktop.UDisks2.Drive");
    m_drive[QStringLiteral("device")] = QStringLiteral("/dev/") + disk;
    m_drive[QStringLiteral("model")] = prop(drivePath, D, QStringLiteral("Model")).toString().simplified();
    m_drive[QStringLiteral("serial")] = prop(drivePath, D, QStringLiteral("Serial")).toString().simplified();
    m_drive[QStringLiteral("firmware")] = prop(drivePath, D, QStringLiteral("Revision")).toString().simplified();
    m_drive[QStringLiteral("size")] = bytesHuman(prop(drivePath, D, QStringLiteral("Size")).toDouble());
    m_drive[QStringLiteral("bus")] = prop(drivePath, D, QStringLiteral("ConnectionBus")).toString();
    const int rpm = prop(drivePath, D, QStringLiteral("RotationRate")).toInt();

    const QString N = QStringLiteral("org.freedesktop.UDisks2.NVMe.Controller");
    const QString A = QStringLiteral("org.freedesktop.UDisks2.Drive.Ata");

    if (hasInterface(drivePath, N)) {
        m_drive[QStringLiteral("kind")] = QStringLiteral("NVMe %1").arg(prop(drivePath, N, QStringLiteral("NVMeRevision")).toString());
        if (update) callMap(drivePath, N, QStringLiteral("SmartUpdate"));
        const QVariantMap a = callMap(drivePath, N, QStringLiteral("SmartGetAttributes"));
        if (a.isEmpty()) { m_error = QStringLiteral("udisks2 returned no SMART data for this drive"); emit changed(); return; }
        const QStringList flags = prop(drivePath, N, QStringLiteral("SmartCriticalWarning")).toStringList();
        const double temp = prop(drivePath, N, QStringLiteral("SmartTemperature")).toDouble();
        const double wc = a.value(QStringLiteral("wctemp")).toDouble(), cc = a.value(QStringLiteral("cctemp")).toDouble();
        const int spare = a.value(QStringLiteral("avail_spare")).toInt(), spareT = a.value(QStringLiteral("spare_thresh")).toInt();
        const int used = a.value(QStringLiteral("percent_used")).toInt();
        const qulonglong mediaErr = a.value(QStringLiteral("media_errors")).toULongLong();

        add(QStringLiteral("Critical warning"), flags.isEmpty() ? QStringLiteral("none") : flags.join(QStringLiteral(", ")), flags.isEmpty() ? QStringLiteral("ok") : QStringLiteral("crit"));
        crit += !flags.isEmpty();
        add(QStringLiteral("Temperature"), kelvinC(temp) + (wc > 0 ? QStringLiteral("  (warn %1, crit %2)").arg(kelvinC(wc), kelvinC(cc)) : QString()),
            (cc > 0 && temp >= cc) ? QStringLiteral("crit") : (wc > 0 && temp >= wc) ? QStringLiteral("warn") : QStringLiteral("ok"));
        warn += (wc > 0 && temp >= wc);
        add(QStringLiteral("Percentage used"), QStringLiteral("%1 %  of rated endurance").arg(used), used >= 100 ? QStringLiteral("crit") : used >= 80 ? QStringLiteral("warn") : QStringLiteral("ok"));
        warn += used >= 80;
        add(QStringLiteral("Available spare"), QStringLiteral("%1 %  (threshold %2 %)").arg(spare).arg(spareT), spare < spareT ? QStringLiteral("crit") : QStringLiteral("ok"));
        crit += spare < spareT;
        add(QStringLiteral("Data written"), bytesHuman(a.value(QStringLiteral("total_data_written")).toDouble()), QStringLiteral("info"));
        add(QStringLiteral("Data read"), bytesHuman(a.value(QStringLiteral("total_data_read")).toDouble()), QStringLiteral("info"));
        add(QStringLiteral("Power-on time"), hours(prop(drivePath, N, QStringLiteral("SmartPowerOnHours")).toULongLong()), QStringLiteral("info"));
        add(QStringLiteral("Power cycles"), QString::number(a.value(QStringLiteral("power_cycles")).toULongLong()), QStringLiteral("info"));
        const qulonglong unsafe = a.value(QStringLiteral("unsafe_shutdowns")).toULongLong();
        add(QStringLiteral("Unsafe shutdowns"), QString::number(unsafe), QStringLiteral("info"));
        add(QStringLiteral("Media errors"), QString::number(mediaErr), mediaErr ? QStringLiteral("warn") : QStringLiteral("ok"));
        warn += mediaErr > 0;
        add(QStringLiteral("Error log entries"), QString::number(a.value(QStringLiteral("num_err_log_entries")).toULongLong()), QStringLiteral("info"));
        add(QStringLiteral("Controller busy time"), QStringLiteral("%1 min").arg(a.value(QStringLiteral("ctrl_busy_time")).toULongLong()), QStringLiteral("info"));
        const uint wt = a.value(QStringLiteral("warning_temp_time")).toUInt(), ct = a.value(QStringLiteral("critical_temp_time")).toUInt();
        add(QStringLiteral("Time over warning / critical temp"), QStringLiteral("%1 min / %2 min").arg(wt).arg(ct), (wt || ct) ? QStringLiteral("warn") : QStringLiteral("ok"));
        QStringList sensors;
        const QVariant ts = a.value(QStringLiteral("temp_sensors"));
        const QDBusArgument arg = ts.value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::ArrayType) {
            arg.beginArray();
            while (!arg.atEnd()) { ushort v; arg >> v; if (v) sensors << kelvinC(v); }
            arg.endArray();
        }
        if (!sensors.isEmpty()) add(QStringLiteral("Temperature sensors"), sensors.join(QStringLiteral("  ")), QStringLiteral("info"));
        add(QStringLiteral("Self-test"), prop(drivePath, N, QStringLiteral("SmartSelftestStatus")).toString(), QStringLiteral("info"));
        add(QStringLiteral("Controller state"), prop(drivePath, N, QStringLiteral("State")).toString(), QStringLiteral("info"));
        m_updated = QDateTime::fromSecsSinceEpoch(prop(drivePath, N, QStringLiteral("SmartUpdated")).toLongLong()).toString(QStringLiteral("HH:mm:ss"));
    } else if (hasInterface(drivePath, A)) {
        m_drive[QStringLiteral("kind")] = rpm > 0 ? QStringLiteral("HDD %1 rpm").arg(rpm) : QStringLiteral("SATA SSD");
        if (update) callMap(drivePath, A, QStringLiteral("SmartUpdate"));
        if (!prop(drivePath, A, QStringLiteral("SmartSupported")).toBool()) { m_error = QStringLiteral("drive does not support SMART"); emit changed(); return; }
        const bool failing = prop(drivePath, A, QStringLiteral("SmartFailing")).toBool();
        add(QStringLiteral("SMART self-assessment"), failing ? QStringLiteral("FAILING") : QStringLiteral("passed"), failing ? QStringLiteral("crit") : QStringLiteral("ok"));
        crit += failing;
        add(QStringLiteral("Temperature"), kelvinC(prop(drivePath, A, QStringLiteral("SmartTemperature")).toDouble()), QStringLiteral("ok"));
        add(QStringLiteral("Power-on time"), hours(prop(drivePath, A, QStringLiteral("SmartPowerOnSeconds")).toULongLong() / 3600), QStringLiteral("info"));
        const qulonglong bad = prop(drivePath, A, QStringLiteral("SmartNumBadSectors")).toULongLong();
        add(QStringLiteral("Bad sectors"), QString::number(bad), bad ? QStringLiteral("warn") : QStringLiteral("ok"));
        warn += bad > 0;
        add(QStringLiteral("Self-test"), prop(drivePath, A, QStringLiteral("SmartSelftestStatus")).toString(), QStringLiteral("info"));
        // raw attribute table: a(ysqiiixia{sv})
        QDBusInterface i(kService, drivePath, A, QDBusConnection::systemBus());
        QDBusMessage r = i.call(QStringLiteral("SmartGetAttributes"), QVariantMap());
        if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty()) {
            const QDBusArgument arg = r.arguments().at(0).value<QDBusArgument>();
            arg.beginArray();
            while (!arg.atEnd()) {
                uchar id; QString name; ushort flagsv; int value, worst, threshold; qlonglong pretty; int unit; QVariantMap ext;
                arg.beginStructure(); arg >> id >> name >> flagsv >> value >> worst >> threshold >> pretty >> unit >> ext; arg.endStructure();
                QString shown;
                switch (unit) {                      // udisks pretty_unit: 1 ms, 2 sectors, 3 mK, 4 other
                case 1: shown = QStringLiteral("%1 h").arg(pretty / 3.6e6, 0, 'f', 1); break;
                case 2: shown = QStringLiteral("%1 sectors").arg(pretty); break;
                case 3: shown = kelvinC(pretty / 1000.0); break;
                default: shown = QString::number(pretty);
                }
                const bool bad = threshold > 0 && value <= threshold;
                add(QStringLiteral("%1 %2").arg(id, 3).arg(name), QStringLiteral("%1   (value %2 / worst %3 / thr %4)").arg(shown).arg(value).arg(worst).arg(threshold), bad ? QStringLiteral("crit") : QStringLiteral("info"));
                crit += bad;
            }
            arg.endArray();
        }
        m_updated = QDateTime::fromSecsSinceEpoch(prop(drivePath, A, QStringLiteral("SmartUpdated")).toLongLong()).toString(QStringLiteral("HH:mm:ss"));
    } else {
        m_drive[QStringLiteral("kind")] = QStringLiteral("unknown");
        m_error = QStringLiteral("no SMART interface for this drive (USB bridges often hide it)");
        emit changed();
        return;
    }
    m_health = crit ? QStringLiteral("Bad") : warn ? QStringLiteral("Caution") : QStringLiteral("Good");
    m_summary = crit ? QStringLiteral("%1 critical indicator(s): back up this drive.").arg(crit)
              : warn ? QStringLiteral("%1 indicator(s) worth watching.").arg(warn)
                     : QStringLiteral("All indicators within normal limits.");
    emit changed();
}

QString SmartInfo::textReport() const
{
    QString out;
    if (!m_error.isEmpty()) return QStringLiteral("error: ") + m_error + '\n';
    out += QStringLiteral("%1  %2  fw %3  %4  %5  [%6]\n").arg(m_drive[QStringLiteral("model")].toString(), m_drive[QStringLiteral("serial")].toString(),
              m_drive[QStringLiteral("firmware")].toString(), m_drive[QStringLiteral("size")].toString(), m_drive[QStringLiteral("kind")].toString(), m_drive[QStringLiteral("device")].toString());
    out += QStringLiteral("Health: %1  —  %2  (SMART data as of %3)\n\n").arg(m_health, m_summary, m_updated);
    for (const QVariant &v : m_attributes) {
        const QVariantMap m = v.toMap();
        const QString lv = m[QStringLiteral("level")].toString();
        out += QStringLiteral("%1 %2  %3\n").arg(lv == QLatin1String("crit") ? QStringLiteral("!!") : lv == QLatin1String("warn") ? QStringLiteral(" !") : QStringLiteral("  "))
                   .arg(m[QStringLiteral("name")].toString(), -34).arg(m[QStringLiteral("value")].toString());
    }
    return out;
}
