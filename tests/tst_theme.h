#pragma once

#include <QObject>
#include <QTemporaryDir>

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void semanticTheme();
    void legacyAnsiTheme();
    void quattroPathWinsOver3x();
    void derivesMissingShades();
    void modeFromThemeType();
    void modeFromLightModeFile();
    void modeFromBackgroundLuminance();
    void modeDefaultsToDark();
    void parsesQuotingAndComments();
    void valueFallbackIsKeyThenLiteral();
    void fallsBackWhenNoThemeInstalled();
    void reloadsAcrossAtomicThemeSwap();

private:
    // Writes colors.toml into one of the two supported layouts.
    void writeTheme(const QString &contents, bool quattro = true, const QString &extraFile = {});
    QString themeRoot(bool quattro) const;

    QTemporaryDir m_home;
    QByteArray m_realHome;
};

