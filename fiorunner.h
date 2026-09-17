#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Runs a CrystalDiskMark-style suite with fio (JSON output) and exposes the results to QML.
class FioRunner : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString targetDir READ targetDir WRITE setTargetDir NOTIFY targetDirChanged)
    Q_PROPERTY(int sizeGiB READ sizeGiB WRITE setSizeGiB NOTIFY configChanged)
    Q_PROPERTY(int runtimeSec READ runtimeSec WRITE setRuntimeSec NOTIFY configChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString currentTest READ currentTest NOTIFY progressChanged)
    Q_PROPERTY(QString deviceInfo READ deviceInfo NOTIFY targetDirChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY targetDirChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)

public:
    explicit FioRunner(QObject *parent = nullptr);
    ~FioRunner() override;

    QString targetDir() const { return m_targetDir; }
    void setTargetDir(const QString &dir);
    int sizeGiB() const { return m_sizeGiB; }
    void setSizeGiB(int v);
    int runtimeSec() const { return m_runtimeSec; }
    void setRuntimeSec(int v);
    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString currentTest() const { return m_currentTest; }
    QString deviceInfo() const;
    QString warning() const;
    QString error() const { return m_error; }
    QVariantList results() const { return m_results; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void targetDirChanged();
    void configChanged();
    void runningChanged();
    void progressChanged();
    void errorChanged();
    void resultsChanged();
    void finished(bool ok);

private:
    struct Job {
        int row;          // results row, -1 for the file preparation step
        bool write;
        QString label, rw, bs;
        int iodepth, numjobs;
    };

    void resetResults();
    void nextJob();
    void jobDone(int exitCode, QProcess::ExitStatus status);
    void fail(const QString &message);
    void finish(bool ok);
    void updateProgress();
    QString testFile() const;

    QString m_targetDir;
    int m_sizeGiB = 4;
    int m_runtimeSec = 15;
    bool m_running = false;
    bool m_stopping = false;
    double m_progress = 0.0;
    QString m_currentTest;
    QString m_error;
    QVariantList m_results;

    QList<Job> m_jobs;
    int m_jobIndex = -1;
    QProcess m_proc;
    QTimer m_tick;
    QElapsedTimer m_jobClock;
};
