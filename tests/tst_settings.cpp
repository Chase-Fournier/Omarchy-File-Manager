#include "tst_settings.h"

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QTest>

namespace {

void writeToml(const QString &path, const QString &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(path));
    file.write(contents.toUtf8());
}

} // namespace

void TestSettings::initTestCase()
{
    QVERIFY(m_home.isValid());
    m_realConfig = qgetenv("XDG_CONFIG_HOME");
    m_realState = qgetenv("XDG_STATE_HOME");
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toLocal8Bit());
    qputenv("XDG_STATE_HOME", (m_home.path() + "/state").toLocal8Bit());
}

void TestSettings::cleanupTestCase()
{
    if (m_realConfig.isEmpty()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", m_realConfig);
    if (m_realState.isEmpty()) qunsetenv("XDG_STATE_HOME"); else qputenv("XDG_STATE_HOME", m_realState);
}

void TestSettings::init()
{
    QDir(m_home.path() + "/config").removeRecursively();
    QDir(m_home.path() + "/state").removeRecursively();
}

// No flag and no config: the window opens the way it was left.
void TestSettings::remembersWhenNothingOverrides()
{
    QCOMPARE(Settings::resolve(QString(), QString(), true), true);
    QCOMPARE(Settings::resolve(QString(), QString(), false), false);
}

void TestSettings::configBeatsRememberedState()
{
    // "I always want the sidebar" wins over "it was closed last time".
    QCOMPARE(Settings::resolve(QString(), QStringLiteral("on"), false), true);
    QCOMPARE(Settings::resolve(QString(), QStringLiteral("off"), true), false);
}

// "remember" is how you ask for the default behaviour back explicitly.
void TestSettings::rememberKeywordDefersToState()
{
    QCOMPARE(Settings::resolve(QString(), QStringLiteral("remember"), true), true);
    QCOMPARE(Settings::resolve(QString(), QStringLiteral("remember"), false), false);
}

void TestSettings::commandLineBeatsEverything()
{
    QCOMPARE(Settings::resolve(QStringLiteral("on"), QStringLiteral("off"), false), true);
    QCOMPARE(Settings::resolve(QStringLiteral("off"), QStringLiteral("on"), true), false);
}

void TestSettings::acceptsTheUsualTruthySpellings()
{
    for (const QString &yes : { "true", "on", "yes", "1", "TRUE", "On" })
        QCOMPARE(Settings::resolve(QString(), yes, false), true);
    for (const QString &no : { "false", "off", "no", "0" })
        QCOMPARE(Settings::resolve(QString(), no, true), false);
}

void TestSettings::roundTripsThroughTheStateFile()
{
    {
        Settings settings;
        settings.setSidebar(true);
        settings.setPreview(false);
        // The destructor flushes a pending write, so closing right after a toggle still
        // remembers it.
    }

    Settings reopened;
    QCOMPARE(reopened.sidebar(), true);
    QCOMPARE(reopened.preview(), false);

    {
        Settings settings;
        settings.setPreview(true);
    }
    Settings again;
    QCOMPARE(again.preview(), true);
}

// State is ours to write; config is the user's. We must never write to theirs.
void TestSettings::writesToTheStateDirectory()
{
    {
        Settings settings;
        settings.setSidebar(true);
    }

    QVERIFY(Settings::statePath().contains(QStringLiteral("/state/omafile/")));
    QVERIFY(QFile::exists(Settings::statePath()));
    QVERIFY(!QFile::exists(Settings::configPath()));
}
