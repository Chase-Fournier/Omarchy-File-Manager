#include "directorymodel.h"
#include "operations.h"
#include "location.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>

#include <cstdio>

namespace {

// Print the fully resolved palette as key<TAB>value, sorted. Mirrors the output of
// `omarchy-theme-color --all`, which is what tests/ diffs against to prove the
// resolution cascade still matches omarchy's.
int dumpTheme(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    Theme theme;
    QString dir = Theme::currentThemeDir();
    for (int i = 2; i < argc; ++i) {
        if (qstrcmp(argv[i], "--file") == 0 && i + 1 < argc)
            dir = QFileInfo(QString::fromLocal8Bit(argv[++i])).absolutePath();
    }
    if (!dir.isEmpty())
        theme.load(QDir(dir));

    const QStringList keys = theme.keys();
    for (const QString &key : keys)
        std::printf("%s\t%s\n", qPrintable(key), qPrintable(theme.value(key)));

    return 0;
}

void printUsage()
{
    std::printf("omafile [path]\n"
                "  path              directory or URI to open (default: $PWD)\n"
                "  --select <file>   open the file's parent and preselect it\n"
                "  --dump-theme      print the resolved theme palette and exit\n");
}

// Works out where to start, in the order §13 specifies: --select's parent, an explicit
// path, then the working directory.
Location startingLocation(const QStringList &arguments, QString *preselect)
{
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);

        if (argument == QLatin1String("--select") && i + 1 < arguments.size()) {
            const QFileInfo target(arguments.at(i + 1));
            *preselect = target.fileName();
            return Location::fromLocalPath(target.absolutePath());
        }
        if (argument.startsWith(QLatin1String("--")))
            continue;

        return Location::parse(argument);
    }

    return Location::fromLocalPath(QDir::currentPath());
}

} // namespace

int main(int argc, char *argv[])
{
    // Started before anything else so the startup trace covers the whole process.
    QElapsedTimer startup;
    startup.start();

    // Handled before any GUI object exists so they work headless, without a display.
    if (argc > 1 && qstrcmp(argv[1], "--dump-theme") == 0)
        return dumpTheme(argc, argv);
    if (argc > 1 && (qstrcmp(argv[1], "--help") == 0 || qstrcmp(argv[1], "-h") == 0)) {
        printUsage();
        return 0;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omafile"));
    app.setOrganizationName(QStringLiteral("omafile"));
    app.setDesktopFileName(QStringLiteral("omafile"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Theme theme;
    DirectoryModel model;
    Operations operations;

    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("Omafile", 1, 0, "Theme", &theme);
    qmlRegisterSingletonInstance("Omafile", 1, 0, "Dir", &model);
    qmlRegisterSingletonInstance("Omafile", 1, 0, "Ops", &operations);
    // Registered only so QML can name the sort enum; the instance above is the model.
    qmlRegisterUncreatableType<DirectoryModel>("Omafile", 1, 0, "DirectoryModel",
                                               QStringLiteral("Use the Dir singleton"));

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Listing starts only after the window exists, so first paint is never behind I/O.
    QString preselect;
    const Location start = startingLocation(app.arguments(), &preselect);
    model.setLocation(start);
    if (!preselect.isEmpty())
        model.selectByName(preselect);

    // OMAFILE_TRACE_STARTUP=1 prints ms to first painted frame; =quit also exits there,
    // which is how bin/test measures the cold-start budget.
    const QByteArray trace = qgetenv("OMAFILE_TRACE_STARTUP");
    if (!trace.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (window) {
            // frameSwapped fires on the render thread; the context object keeps the
            // handler on the GUI thread so quit() is delivered to the right loop.
            QObject::connect(
                window, &QQuickWindow::frameSwapped, &app,
                [&startup, &app, trace, fired = false]() mutable {
                    if (fired)
                        return;
                    fired = true;
                    std::printf("startup_ms %lld\n", static_cast<long long>(startup.elapsed()));
                    std::fflush(stdout);
                    if (trace == "quit")
                        app.quit();
                },
                Qt::QueuedConnection);
        }
    }

    return app.exec();
}
