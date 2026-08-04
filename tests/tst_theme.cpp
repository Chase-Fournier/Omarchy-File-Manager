#include "tst_theme.h"

#include "theme.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Every test runs against a fake $HOME so the real Omarchy install is never read and
// the theme-directory precedence rules are themselves under test.

QString TestTheme::themeRoot(bool quattro) const
{
    return m_home.path()
        + (quattro ? QStringLiteral("/.local/state/omarchy/current")
                   : QStringLiteral("/.config/omarchy/current"));
}

void TestTheme::writeTheme(const QString &contents, bool quattro, const QString &extraFile)
{
    const QString dir = themeRoot(quattro) + QStringLiteral("/theme");
    QVERIFY(QDir().mkpath(dir));

    QFile file(dir + QStringLiteral("/colors.toml"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(contents.toUtf8());
    file.close();

    if (!extraFile.isEmpty()) {
        QFile marker(dir + QLatin1Char('/') + extraFile);
        QVERIFY(marker.open(QIODevice::WriteOnly));
    }
}

void TestTheme::initTestCase()
{
    QVERIFY(m_home.isValid());
    m_realHome = qgetenv("HOME");
    qputenv("HOME", m_home.path().toLocal8Bit());
    // Guard the whole premise of these tests: QDir must follow the fake HOME.
    QCOMPARE(QDir::homePath(), m_home.path());
}

void TestTheme::cleanupTestCase()
{
    qputenv("HOME", m_realHome);
}

void TestTheme::cleanup()
{
    QDir(m_home.path() + QStringLiteral("/.local")).removeRecursively();
    QDir(m_home.path() + QStringLiteral("/.config")).removeRecursively();
}

// A Quattro-era semantic theme is used verbatim, with no derivation needed.
void TestTheme::semanticTheme()
{
    writeTheme(R"(mode = "dark"

accent = "#7aa2f7"
selection = "#292e42"
muted = "#414868"
background = "#1a1b26"
lighter_background = "#24283b"
foreground = "#a9b1d6"
dark_foreground = "#565f89"
bright_foreground = "#c0caf5"
red = "#f7768e"
green = "#9ece6a"
)");

    Theme theme;
    QCOMPARE(theme.mode(), QStringLiteral("dark"));
    QVERIFY(theme.isDark());
    QCOMPARE(theme.accent(), QColor("#7aa2f7"));
    QCOMPARE(theme.bg(), QColor("#1a1b26"));
    QCOMPARE(theme.fg(), QColor("#a9b1d6"));
    QCOMPARE(theme.dim(), QColor("#565f89"));
    QCOMPARE(theme.selection(), QColor("#292e42"));
    QCOMPARE(theme.error(), QColor("#f7768e"));
}

// The Omarchy 3.x format defines ANSI colorN names. Note the deliberate asymmetry:
// an explicit `background` overwrites color0 rather than the other way round, so
// lighter_background (which derives from color0) ends up equal to background.
void TestTheme::legacyAnsiTheme()
{
    writeTheme(R"(accent = "#7aa2f7"
foreground = "#a9b1d6"
background = "#1a1b26"
selection_foreground = "#c0caf5"
selection_background = "#7aa2f7"

color0 = "#32344a"
color1 = "#f7768e"
color7 = "#787c99"
color8 = "#444b6a"
color15 = "#acb0d0"
)");

    Theme theme;
    QCOMPARE(theme.bg(), QColor("#1a1b26"));
    QCOMPARE(theme.value(QStringLiteral("color0")), QStringLiteral("#1a1b26"));
    QCOMPARE(theme.value(QStringLiteral("lighter_background")), QStringLiteral("#1a1b26"));
    // foreground was explicit, so color7's own value is replaced by it.
    QCOMPARE(theme.fg(), QColor("#a9b1d6"));
    QCOMPARE(theme.fgLight(), QColor("#a9b1d6"));
    QCOMPARE(theme.error(), QColor("#f7768e"));
    QCOMPARE(theme.muted(), QColor("#444b6a"));
    QCOMPARE(theme.dim(), QColor("#444b6a"));
    QCOMPARE(theme.fgBright(), QColor("#acb0d0"));
    // selection falls back to selection_background when unset.
    QCOMPARE(theme.selection(), QColor("#7aa2f7"));
    // cursor is assigned unconditionally from bright_foreground.
    QCOMPARE(theme.value(QStringLiteral("cursor")), QStringLiteral("#acb0d0"));
}

void TestTheme::quattroPathWinsOver3x()
{
    writeTheme(QStringLiteral("mode = \"dark\"\nbackground = \"#111111\"\n"), false);
    writeTheme(QStringLiteral("mode = \"dark\"\nbackground = \"#222222\"\n"), true);

    Theme theme;
    QCOMPARE(theme.bg(), QColor("#222222"));
}

// Shades absent from the file are mixed toward black/white, matching omarchy.
void TestTheme::derivesMissingShades()
{
    writeTheme(QStringLiteral("mode = \"dark\"\nbackground = \"#1a1b26\"\n"
                              "foreground = \"#a9b1d6\"\nred = \"#f7768e\"\n"
                              "yellow = \"#e0af68\"\n"));

    Theme theme;
    // #1a1b26 blended 25% toward black, rounding half up per component.
    QCOMPARE(theme.bgDark(), QColor("#14141d"));
    QCOMPARE(theme.bgDarker(), QColor("#0d0e13"));
    // bright_red is red blended 20% toward white: 247,118,142 -> 249,145,165.
    QCOMPARE(theme.value(QStringLiteral("bright_red")), QStringLiteral("#f991a5"));
    // orange falls back to yellow, and brown is orange at 50% black.
    QCOMPARE(theme.value(QStringLiteral("orange")), QStringLiteral("#e0af68"));
    QCOMPARE(theme.value(QStringLiteral("brown")), QStringLiteral("#705834"));
}

void TestTheme::modeFromThemeType()
{
    writeTheme(QStringLiteral("theme_type = \"light\"\nbackground = \"#000000\"\n"));

    Theme theme;
    QCOMPARE(theme.mode(), QStringLiteral("light"));
    QVERIFY(!theme.isDark());
}

void TestTheme::modeFromLightModeFile()
{
    writeTheme(QStringLiteral("background = \"#000000\"\nforeground = \"#ffffff\"\n"), true,
               QStringLiteral("light.mode"));

    Theme theme;
    QCOMPARE(theme.mode(), QStringLiteral("light"));
}

void TestTheme::modeFromBackgroundLuminance()
{
    // Channel sum 0xef+0xf1+0xf5 = 725 > 382, so this reads as a light theme.
    writeTheme(QStringLiteral("background = \"#eff1f5\"\nforeground = \"#4c4f69\"\n"));
    {
        Theme theme;
        QCOMPARE(theme.mode(), QStringLiteral("light"));
    }

    cleanup();
    writeTheme(QStringLiteral("background = \"#1a1b26\"\nforeground = \"#a9b1d6\"\n"));
    {
        Theme theme;
        QCOMPARE(theme.mode(), QStringLiteral("dark"));
    }
}

void TestTheme::modeDefaultsToDark()
{
    writeTheme(QStringLiteral("background = \"not-a-color\"\nforeground = \"#ffffff\"\n"));

    Theme theme;
    QCOMPARE(theme.mode(), QStringLiteral("dark"));
}

void TestTheme::parsesQuotingAndComments()
{
    writeTheme(QStringLiteral("# a comment line\n"
                              "mode = \"dark\"\n"
                              "  accent   =   \"#aabbcc\"   # trailing comment\n"
                              "background = '#112233'\n"
                              "foreground=#445566\n"
                              "\n"
                              "not a key value line\n"));

    Theme theme;
    QCOMPARE(theme.accent(), QColor("#aabbcc"));
    QCOMPARE(theme.bg(), QColor("#112233"));
    QCOMPARE(theme.fg(), QColor("#445566"));
}

void TestTheme::valueFallbackIsKeyThenLiteral()
{
    writeTheme(QStringLiteral("mode = \"dark\"\nblue = \"#7aa2f7\"\nbackground = \"#1a1b26\"\n"
                              "foreground = \"#a9b1d6\"\n"));

    Theme theme;
    // A fallback naming a real palette key resolves through it.
    QCOMPARE(theme.value(QStringLiteral("nonexistent"), QStringLiteral("blue")),
             QStringLiteral("#7aa2f7"));
    // A fallback that is not a key is used verbatim.
    QCOMPARE(theme.value(QStringLiteral("nonexistent"), QStringLiteral("#ff0000")),
             QStringLiteral("#ff0000"));
    QVERIFY(theme.value(QStringLiteral("nonexistent")).isEmpty());
}

void TestTheme::fallsBackWhenNoThemeInstalled()
{
    Theme theme;
    QVERIFY(Theme::currentThemeDir().isEmpty());
    // Tokyo Night, so the window is themed rather than black-on-black.
    QCOMPARE(theme.bg(), QColor("#1a1b26"));
    QCOMPARE(theme.accent(), QColor("#7aa2f7"));
    QCOMPARE(theme.mode(), QStringLiteral("dark"));
    QVERIFY(theme.fg().isValid());
}

// omarchy-theme-set does `rm -rf current/theme && mv current/next-theme current/theme`,
// which destroys any watch on the theme directory or on colors.toml. Watching the
// parent is what makes live re-theming survive that.
void TestTheme::reloadsAcrossAtomicThemeSwap()
{
    writeTheme(QStringLiteral("mode = \"dark\"\nbackground = \"#1a1b26\"\n"
                              "foreground = \"#a9b1d6\"\naccent = \"#7aa2f7\"\n"));

    Theme theme;
    QCOMPARE(theme.bg(), QColor("#1a1b26"));

    QSignalSpy spy(&theme, &Theme::changed);

    // Stage the next theme beside the current one, then swap it in, as omarchy does.
    const QString root = themeRoot(true);
    const QString next = root + QStringLiteral("/next-theme");
    QVERIFY(QDir().mkpath(next));
    QFile file(next + QStringLiteral("/colors.toml"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("mode = \"light\"\nbackground = \"#eff1f5\"\n"
               "foreground = \"#4c4f69\"\naccent = \"#1e66f5\"\n");
    file.close();

    QVERIFY(QDir(root + QStringLiteral("/theme")).removeRecursively());
    QVERIFY(QDir().rename(next, root + QStringLiteral("/theme")));

    QVERIFY(spy.wait(5000));
    QCOMPARE(theme.bg(), QColor("#eff1f5"));
    QCOMPARE(theme.accent(), QColor("#1e66f5"));
    QCOMPARE(theme.mode(), QStringLiteral("light"));
    QVERIFY(!theme.isDark());
}

