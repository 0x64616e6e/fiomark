#include "fiorunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStorageInfo>

namespace {
struct RowSpec { const char *name; const char *detail; const char *rwRead; const char *rwWrite; const char *bs; int iodepth; int numjobs; };
constexpr RowSpec kRows[] = {
    {"SEQ1M",  "Q8 T1",  "read",     "write",     "1M", 8,  1},
    {"RND4K",  "Q32 T4", "randread", "randwrite", "4k", 32, 4},
    {"RND4K",  "Q1 T1",  "randread", "randwrite", "4k", 1,  1},
};

QString runTool(const QString &program, const QStringList &args)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(3000))
        return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
} // namespace

FioRunner::FioRunner(QObject *parent) : QObject(parent)
{
    m_targetDir = QDir::homePath();
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, &FioRunner::updateProgress);
    connect(&m_proc, &QProcess::finished, this, &FioRunner::jobDone);
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(QStringLiteral("could not start fio (is it installed?)"));
    });
    resetResults();
}

FioRunner::~FioRunner()
{
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.kill();
        m_proc.waitForFinished(2000);
    }
    QFile::remove(testFile());
}

void FioRunner::setTargetDir(const QString &dir)
{
    QString d = dir;
    if (d.startsWith(QStringLiteral("file://")))
        d = QUrl(d).toLocalFile();
    if (d == m_targetDir || m_running)
        return;
    m_targetDir = d;
    emit targetDirChanged();
}

void FioRunner::setSizeGiB(int v)
{
    if (v == m_sizeGiB || v < 1 || m_running) return;
    m_sizeGiB = v;
    emit configChanged();
}

void FioRunner::setRuntimeSec(int v)
{
    if (v == m_runtimeSec || v < 1 || m_running) return;
    m_runtimeSec = v;
    emit configChanged();
}

QString FioRunner::testFile() const { return QDir(m_targetDir).filePath(QStringLiteral("fiomark.bin")); }

QString FioRunner::deviceInfo() const
{
    const QStorageInfo st(m_targetDir);
    if (!st.isValid())
        return QStringLiteral("not a valid directory");
    const QString dev = QString::fromUtf8(st.device());
    QString model;
    // walk from the filesystem's device up through LVM/partitions to the disk and take its model
    const QStringList lines = runTool(QStringLiteral("lsblk"), {QStringLiteral("-nso"), QStringLiteral("MODEL,TRAN"), dev}).split('\n');
    for (const QString &l : lines)
        if (!l.trimmed().isEmpty()) { model = l.simplified(); break; }
    const double freeGiB = st.bytesAvailable() / 1073741824.0;
    return QStringLiteral("%1  ·  %2 on %3  ·  %4 GiB free")
        .arg(model.isEmpty() ? QStringLiteral("unknown device") : model,
             QString::fromUtf8(st.fileSystemType()), dev, QString::number(freeGiB, 'f', 1));
}

QString FioRunner::warning() const
{
    const QStorageInfo st(m_targetDir);
    const QString fs = QString::fromUtf8(st.fileSystemType());
    if (fs == QLatin1String("zfs"))
        return QStringLiteral("ZFS caches and compresses: these numbers will measure ZFS, not the drive.");
    if (fs == QLatin1String("tmpfs"))
        return QStringLiteral("tmpfs lives in RAM: this would benchmark memory, not a disk.");
    return {};
}

void FioRunner::resetResults()
{
    m_results.clear();
    for (const RowSpec &r : kRows) {
        QVariantMap row;
        row[QStringLiteral("name")] = QString::fromLatin1(r.name);
        row[QStringLiteral("detail")] = QString::fromLatin1(r.detail);
        for (const char *k : {"readMB", "writeMB", "readIops", "writeIops", "readLatUs", "writeLatUs"})
            row[QString::fromLatin1(k)] = -1.0;
        m_results.append(row);
    }
    emit resultsChanged();
}

void FioRunner::start()
{
    if (m_running)
        return;
    m_error.clear();
    emit errorChanged();

    const QFileInfo dir(m_targetDir);
    if (!dir.isDir() || !dir.isWritable()) { fail(QStringLiteral("target directory is not writable")); return; }
    const QStorageInfo st(m_targetDir);
    const qint64 need = (qint64(m_sizeGiB) + 1) * 1073741824LL;
    if (st.bytesAvailable() < need) { fail(QStringLiteral("not enough free space for a %1 GiB test file").arg(m_sizeGiB)); return; }

    resetResults();
    m_jobs.clear();
    m_jobs.append({-1, true, QStringLiteral("Preparing test file"), QStringLiteral("write"), QStringLiteral("1M"), 8, 1});
    int rowIndex = 0;
    for (const RowSpec &r : kRows) {
        const QString base = QStringLiteral("%1 %2").arg(QString::fromLatin1(r.name), QString::fromLatin1(r.detail));
        m_jobs.append({rowIndex, false, base + QStringLiteral(" read"),  QString::fromLatin1(r.rwRead),  QString::fromLatin1(r.bs), r.iodepth, r.numjobs});
        m_jobs.append({rowIndex, true,  base + QStringLiteral(" write"), QString::fromLatin1(r.rwWrite), QString::fromLatin1(r.bs), r.iodepth, r.numjobs});
        ++rowIndex;
    }
    m_jobIndex = -1;
    m_stopping = false;
    m_running = true;
    emit runningChanged();
    m_tick.start();
    nextJob();
}

void FioRunner::stop()
{
    if (!m_running)
        return;
    m_stopping = true;
    m_proc.terminate();
    QTimer::singleShot(2000, this, [this] { if (m_proc.state() != QProcess::NotRunning) m_proc.kill(); });
}

void FioRunner::nextJob()
{
    ++m_jobIndex;
    if (m_jobIndex >= m_jobs.size()) { finish(true); return; }
    const Job &j = m_jobs.at(m_jobIndex);
    m_currentTest = j.label;
    m_jobClock.restart();
    updateProgress();

    QStringList a{
        QStringLiteral("--name=fiomark"), QStringLiteral("--filename=") + testFile(),
        QStringLiteral("--size=%1G").arg(m_sizeGiB), QStringLiteral("--rw=") + j.rw, QStringLiteral("--bs=") + j.bs,
        QStringLiteral("--iodepth=%1").arg(j.iodepth), QStringLiteral("--numjobs=%1").arg(j.numjobs),
        QStringLiteral("--ioengine=io_uring"), QStringLiteral("--direct=1"), QStringLiteral("--group_reporting"),
        QStringLiteral("--output-format=json"),
    };
    if (j.row < 0)
        a << QStringLiteral("--end_fsync=1");                      // lay the file out once, fully
    else
        a << QStringLiteral("--runtime=%1").arg(m_runtimeSec) << QStringLiteral("--time_based");
    m_proc.start(QStringLiteral("fio"), a);
}

void FioRunner::jobDone(int exitCode, QProcess::ExitStatus status)
{
    if (!m_running)
        return;
    if (m_stopping) { finish(false); return; }
    const QByteArray out = m_proc.readAllStandardOutput();
    if (status != QProcess::NormalExit || exitCode != 0) {
        const QString err = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
        fail(QStringLiteral("fio failed: ") + (err.isEmpty() ? QStringLiteral("exit code %1").arg(exitCode) : err.section('\n', 0, 0)));
        return;
    }
    const Job &j = m_jobs.at(m_jobIndex);
    if (j.row >= 0) {
        const int brace = out.indexOf('{');                        // fio may print notices before the JSON
        const QJsonObject job = QJsonDocument::fromJson(out.mid(qMax(0, brace))).object()
                                    .value(QStringLiteral("jobs")).toArray().at(0).toObject();
        const QJsonObject d = job.value(j.write ? QStringLiteral("write") : QStringLiteral("read")).toObject();
        if (d.isEmpty()) { fail(QStringLiteral("could not parse fio output")); return; }
        double latNs = d.value(QStringLiteral("clat_ns")).toObject().value(QStringLiteral("mean")).toDouble();
        if (latNs <= 0)
            latNs = d.value(QStringLiteral("lat_ns")).toObject().value(QStringLiteral("mean")).toDouble();
        QVariantMap row = m_results.at(j.row).toMap();
        const QString p = j.write ? QStringLiteral("write") : QStringLiteral("read");
        row[p + QStringLiteral("MB")] = d.value(QStringLiteral("bw_bytes")).toDouble() / 1e6;
        row[p + QStringLiteral("Iops")] = d.value(QStringLiteral("iops")).toDouble();
        row[p + QStringLiteral("LatUs")] = latNs / 1000.0;
        m_results[j.row] = row;
        emit resultsChanged();
    }
    nextJob();
}

void FioRunner::updateProgress()
{
    if (!m_running || m_jobs.isEmpty())
        return;
    // the preparation step has no fixed duration: count it as one slot and let it fill slowly
    const double slot = 1.0 / m_jobs.size();
    const double expected = (m_jobIndex == 0 ? qMax(4.0, m_sizeGiB * 1.0) : double(m_runtimeSec)) * 1000.0;
    const double within = qMin(0.97, m_jobClock.elapsed() / expected);
    m_progress = qBound(0.0, (m_jobIndex + within) * slot, 1.0);
    emit progressChanged();
}

void FioRunner::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    if (m_running)
        finish(false);
}

void FioRunner::finish(bool ok)
{
    m_tick.stop();
    QFile::remove(testFile());
    m_running = false;
    m_stopping = false;
    m_progress = ok ? 1.0 : 0.0;
    m_currentTest = ok ? QStringLiteral("Done") : (m_error.isEmpty() ? QStringLiteral("Stopped") : QStringLiteral("Failed"));
    emit progressChanged();
    emit runningChanged();
    emit finished(ok);
}
