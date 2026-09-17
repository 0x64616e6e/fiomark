#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>

#include "fiorunner.h"

static int runCli(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({QStringLiteral("cli"), QStringLiteral("Run the suite in the terminal and print a table.")});
    p.addOption({QStringLiteral("dir"), QStringLiteral("Directory to test (default: home)."), QStringLiteral("path")});
    p.addOption({QStringLiteral("size"), QStringLiteral("Test file size in GiB (default 4)."), QStringLiteral("GiB")});
    p.addOption({QStringLiteral("runtime"), QStringLiteral("Seconds per test (default 15)."), QStringLiteral("s")});
    p.process(app);

    FioRunner r;
    if (p.isSet(QStringLiteral("dir"))) r.setTargetDir(p.value(QStringLiteral("dir")));
    if (p.isSet(QStringLiteral("size"))) r.setSizeGiB(p.value(QStringLiteral("size")).toInt());
    if (p.isSet(QStringLiteral("runtime"))) r.setRuntimeSec(p.value(QStringLiteral("runtime")).toInt());

    QTextStream out(stdout);
    out << r.deviceInfo() << '\n';
    if (!r.warning().isEmpty()) out << "warning: " << r.warning() << '\n';
    out.flush();
    QString last;
    QObject::connect(&r, &FioRunner::progressChanged, [&] {
        if (r.currentTest() != last) { last = r.currentTest(); out << "  " << last << '\n'; out.flush(); }
    });
    QObject::connect(&r, &FioRunner::finished, [&](bool ok) {
        if (!ok) { out << "error: " << (r.error().isEmpty() ? QStringLiteral("stopped") : r.error()) << '\n'; out.flush(); app.exit(1); return; }
        out << QStringLiteral("\n%1 %2 %3 %4 %5\n").arg(QStringLiteral("test"), -14).arg(QStringLiteral("read MB/s"), 11)
                   .arg(QStringLiteral("write MB/s"), 11).arg(QStringLiteral("read IOPS"), 11).arg(QStringLiteral("write IOPS"), 11);
        for (const QVariant &v : r.results()) {
            const QVariantMap m = v.toMap();
            out << QStringLiteral("%1 %2 %3 %4 %5\n")
                       .arg(m[QStringLiteral("name")].toString() + ' ' + m[QStringLiteral("detail")].toString(), -14)
                       .arg(m[QStringLiteral("readMB")].toDouble(), 11, 'f', 1).arg(m[QStringLiteral("writeMB")].toDouble(), 11, 'f', 1)
                       .arg(m[QStringLiteral("readIops")].toDouble(), 11, 'f', 0).arg(m[QStringLiteral("writeIops")].toDouble(), 11, 'f', 0);
        }
        out.flush();
        app.exit(0);
    });
    r.start();
    if (!r.running()) return 1;
    return app.exec();
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--cli") == 0 || qstrcmp(argv[i], "--help") == 0 || qstrcmp(argv[i], "-h") == 0)
            return runCli(argc, argv);

    if (!qEnvironmentVariableIsSet("QT_LOGGING_RULES"))
        qputenv("QT_LOGGING_RULES", "default.debug=false");   // the qt6ct platform theme logs every palette lookup via qDebug
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("fiomark"));
    app.setDesktopFileName(QStringLiteral("fiomark"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));   // Fusion follows the system palette (qt6ct -> Day/Night Panel)
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("FioMark", "Main");
    return app.exec();
}
