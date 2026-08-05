#include "theme.h"

#include <QQmlEngine>
#include <QtQuickTest>

// The QML components import the `Omafile` module for Theme, exactly as the app does, so
// the suite registers the same singleton against a real Theme. Everything else a
// component needs is passed in as its `app` property, which the tests stub — that is what
// makes a component testable on its own rather than only inside the whole window.
class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        // Outlives every engine the suite creates, which is what
        // qmlRegisterSingletonInstance requires of it.
        static Theme theme;
        qmlRegisterSingletonInstance("Omafile", 1, 0, "Theme", &theme);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(omafile_qml, Setup)

#include "main.moc"
