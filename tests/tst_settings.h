#pragma once

#include <QByteArray>
#include <QObject>
#include <QTemporaryDir>

// The precedence rules for remembered UI state. Getting these wrong is quietly annoying:
// either a flag stops working, or the window refuses to remember anything.
class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void remembersWhenNothingOverrides();
    void configBeatsRememberedState();
    void rememberKeywordDefersToState();
    void commandLineBeatsEverything();
    void acceptsTheUsualTruthySpellings();

    void roundTripsThroughTheStateFile();
    void writesToTheStateDirectory();

private:
    QTemporaryDir m_home;
    QByteArray m_realConfig;
    QByteArray m_realState;
};
