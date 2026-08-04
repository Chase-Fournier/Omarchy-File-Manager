#include "theme.h"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QTextStream>

namespace {

// Candidate locations for the active theme, newest layout first.
//   Quattro (4.x): ~/.local/state/omarchy/current/theme
//   Omarchy 3.x:   ~/.config/omarchy/current/theme
QStringList themeDirCandidates()
{
    const QString home = QDir::homePath();
    return {
        home + QStringLiteral("/.local/state/omarchy/current/theme"),
        home + QStringLiteral("/.config/omarchy/current/theme"),
    };
}

QStringList themeNameCandidates()
{
    const QString home = QDir::homePath();
    return {
        home + QStringLiteral("/.local/state/omarchy/current/theme.name"),
        home + QStringLiteral("/.config/omarchy/current/theme.name"),
    };
}

// Tokyo Night — Omarchy's default. Used verbatim when no theme is installed, and
// as the floor under every derived key so the UI is never handed a null color.
const QHash<QString, QString> &fallbackPalette()
{
    static const QHash<QString, QString> palette = {
        { QStringLiteral("mode"), QStringLiteral("dark") },
        { QStringLiteral("accent"), QStringLiteral("#7aa2f7") },
        { QStringLiteral("selection"), QStringLiteral("#292e42") },
        { QStringLiteral("muted"), QStringLiteral("#414868") },
        { QStringLiteral("background"), QStringLiteral("#1a1b26") },
        { QStringLiteral("dark_background"), QStringLiteral("#13141c") },
        { QStringLiteral("darker_background"), QStringLiteral("#0e0e14") },
        { QStringLiteral("lighter_background"), QStringLiteral("#24283b") },
        { QStringLiteral("foreground"), QStringLiteral("#a9b1d6") },
        { QStringLiteral("dark_foreground"), QStringLiteral("#565f89") },
        { QStringLiteral("light_foreground"), QStringLiteral("#b4bee6") },
        { QStringLiteral("bright_foreground"), QStringLiteral("#c0caf5") },
        { QStringLiteral("red"), QStringLiteral("#f7768e") },
        { QStringLiteral("yellow"), QStringLiteral("#e0af68") },
        { QStringLiteral("orange"), QStringLiteral("#eb927b") },
        { QStringLiteral("green"), QStringLiteral("#9ece6a") },
        { QStringLiteral("cyan"), QStringLiteral("#449dab") },
        { QStringLiteral("blue"), QStringLiteral("#7aa2f7") },
        { QStringLiteral("magenta"), QStringLiteral("#ad8ee6") },
        { QStringLiteral("brown"), QStringLiteral("#75493d") },
    };
    return palette;
}

} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(50);
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    reload();
    rewatch();
}

QString Theme::currentThemeDir()
{
    const QStringList candidates = themeDirCandidates();
    for (const QString &dir : candidates) {
        if (QFileInfo::exists(dir + QStringLiteral("/colors.toml")))
            return dir;
    }
    return QString();
}

void Theme::load(const QDir &themeDir)
{
    m_colors.clear();
    m_themeDir = themeDir.absolutePath();

    parse(m_themeDir + QStringLiteral("/colors.toml"));
    resolve(themeDir);
}

void Theme::reload()
{
    const QHash<QString, QString> before = m_colors;
    const QString beforeName = m_name;

    const QString dir = currentThemeDir();
    if (dir.isEmpty()) {
        applyFallbackPalette();
    } else {
        load(QDir(dir));
    }

    // theme.name is written next to the theme dir; it is the display name only.
    m_name.clear();
    const QStringList nameFiles = themeNameCandidates();
    for (const QString &path : nameFiles) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_name = QString::fromUtf8(file.readAll()).trimmed();
            break;
        }
    }

    if (m_colors != before || m_name != beforeName)
        emit changed();
}

void Theme::applyFallbackPalette()
{
    m_colors = fallbackPalette();
    m_themeDir.clear();
    resolve(QDir());
}

void Theme::parse(const QString &colorsFile)
{
    QFile file(colorsFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;

        // Strip quotes and spaces from the key, exactly as omarchy-theme-color does.
        QString key = line.left(eq);
        key.remove(QLatin1Char('"'));
        key.remove(QLatin1Char('\''));
        key.remove(QLatin1Char(' '));
        if (key.isEmpty() || key.startsWith(QLatin1Char('#')))
            continue;

        QString value = line.mid(eq + 1);
        const int openQuote = value.indexOf(QRegularExpression(QStringLiteral("[\"']")));
        if (openQuote >= 0) {
            // Take what is between the quotes, which also drops any trailing comment.
            value = value.mid(openQuote + 1);
            const int closeQuote = value.indexOf(QRegularExpression(QStringLiteral("[\"']")));
            if (closeQuote >= 0)
                value = value.left(closeQuote);
        } else {
            value = value.trimmed();
        }

        m_colors.insert(key, value);
    }
}

// Port of resolve_theme_colors() from omarchy's bin/omarchy-theme-color. The order of
// these steps matters: later derivations read keys the earlier ones filled in.
void Theme::resolve(const QDir &themeDir)
{
    auto has = [this](const QString &key) {
        return !m_colors.value(key).isEmpty();
    };
    // `key ||= value` — assign only when key is currently unset or empty.
    auto fill = [this, &has](const QString &key, const QString &value) {
        if (!has(key) && !value.isEmpty())
            m_colors.insert(key, value);
    };
    auto get = [this](const QString &key) { return m_colors.value(key); };
    // `key ||= first non-empty of others`
    auto fillFrom = [&](const QString &key, const QStringList &sources) {
        for (const QString &source : sources) {
            if (has(key))
                return;
            fill(key, get(source));
        }
    };

    // Canonical keys are background/foreground; pre-semantic themes only have ANSI names.
    fill(QStringLiteral("background"), get(QStringLiteral("color0")));
    fill(QStringLiteral("foreground"), get(QStringLiteral("color7")));
    if (has(QStringLiteral("background")))
        m_colors.insert(QStringLiteral("color0"), get(QStringLiteral("background")));
    if (has(QStringLiteral("foreground")))
        m_colors.insert(QStringLiteral("color7"), get(QStringLiteral("foreground")));

    // Legacy compatibility: map ANSI color0..color15 onto the semantic names.
    static const QVector<QPair<QString, QString>> legacyAliases = {
        { QStringLiteral("red"), QStringLiteral("color1") },
        { QStringLiteral("green"), QStringLiteral("color2") },
        { QStringLiteral("yellow"), QStringLiteral("color3") },
        { QStringLiteral("blue"), QStringLiteral("color4") },
        { QStringLiteral("magenta"), QStringLiteral("color5") },
        { QStringLiteral("cyan"), QStringLiteral("color6") },
        { QStringLiteral("bright_red"), QStringLiteral("color9") },
        { QStringLiteral("bright_green"), QStringLiteral("color10") },
        { QStringLiteral("bright_yellow"), QStringLiteral("color11") },
        { QStringLiteral("bright_blue"), QStringLiteral("color12") },
        { QStringLiteral("bright_magenta"), QStringLiteral("color13") },
        { QStringLiteral("bright_cyan"), QStringLiteral("color14") },
    };
    for (const auto &alias : legacyAliases)
        fill(alias.first, get(alias.second));

    fill(QStringLiteral("magenta"), get(QStringLiteral("purple")));
    fill(QStringLiteral("bright_magenta"), get(QStringLiteral("bright_purple")));

    fillFrom(QStringLiteral("light_foreground"),
             { QStringLiteral("color7"), QStringLiteral("foreground") });
    fillFrom(QStringLiteral("bright_foreground"),
             { QStringLiteral("color15"), QStringLiteral("foreground") });
    m_colors.insert(QStringLiteral("cursor"), get(QStringLiteral("bright_foreground")));
    fillFrom(QStringLiteral("lighter_background"),
             { QStringLiteral("color0"), QStringLiteral("background") });
    fillFrom(QStringLiteral("dark_foreground"),
             { QStringLiteral("color8"), QStringLiteral("foreground") });
    fillFrom(QStringLiteral("muted"),
             { QStringLiteral("color8"), QStringLiteral("dark_foreground") });
    fillFrom(QStringLiteral("selection"),
             { QStringLiteral("selection_background"), QStringLiteral("color8"),
               QStringLiteral("color0"), QStringLiteral("background") });
    fill(QStringLiteral("selection_background"), get(QStringLiteral("selection")));
    fill(QStringLiteral("selection_foreground"), get(QStringLiteral("bright_foreground")));
    fill(QStringLiteral("orange"), get(QStringLiteral("yellow")));
    fill(QStringLiteral("brown"),
         mix(get(QStringLiteral("orange")), QStringLiteral("#000000"), 0.50));

    // Derive shades the theme did not define.
    fill(QStringLiteral("dark_background"),
         mix(get(QStringLiteral("background")), QStringLiteral("#000000"), 0.25));
    fill(QStringLiteral("darker_background"),
         mix(get(QStringLiteral("background")), QStringLiteral("#000000"), 0.50));

    static const QVector<QPair<QString, QString>> brightenFrom = {
        { QStringLiteral("bright_red"), QStringLiteral("red") },
        { QStringLiteral("bright_yellow"), QStringLiteral("yellow") },
        { QStringLiteral("bright_green"), QStringLiteral("green") },
        { QStringLiteral("bright_cyan"), QStringLiteral("cyan") },
        { QStringLiteral("bright_blue"), QStringLiteral("blue") },
        { QStringLiteral("bright_magenta"), QStringLiteral("magenta") },
    };
    for (const auto &pair : brightenFrom)
        fill(pair.first, mix(get(pair.second), QStringLiteral("#ffffff"), 0.20));

    fill(QStringLiteral("purple"), get(QStringLiteral("magenta")));
    fill(QStringLiteral("bright_purple"), get(QStringLiteral("bright_magenta")));

    // Keep consumers that still reference ANSI names working.
    static const QVector<QPair<QString, QString>> ansiAliases = {
        { QStringLiteral("color0"), QStringLiteral("background") },
        { QStringLiteral("color1"), QStringLiteral("red") },
        { QStringLiteral("color2"), QStringLiteral("green") },
        { QStringLiteral("color3"), QStringLiteral("yellow") },
        { QStringLiteral("color4"), QStringLiteral("blue") },
        { QStringLiteral("color5"), QStringLiteral("magenta") },
        { QStringLiteral("color6"), QStringLiteral("cyan") },
        { QStringLiteral("color7"), QStringLiteral("foreground") },
        { QStringLiteral("color8"), QStringLiteral("muted") },
        { QStringLiteral("color9"), QStringLiteral("bright_red") },
        { QStringLiteral("color10"), QStringLiteral("bright_green") },
        { QStringLiteral("color11"), QStringLiteral("bright_yellow") },
        { QStringLiteral("color12"), QStringLiteral("bright_blue") },
        { QStringLiteral("color13"), QStringLiteral("bright_magenta") },
        { QStringLiteral("color14"), QStringLiteral("bright_cyan") },
        { QStringLiteral("color15"), QStringLiteral("bright_foreground") },
    };
    for (const auto &alias : ansiAliases)
        fill(alias.first, get(alias.second));

    resolveMode(themeDir);
    m_colors.insert(QStringLiteral("theme_type"), get(QStringLiteral("mode")));

    // Last line of defense: a theme missing a key entirely still yields a usable UI.
    const QHash<QString, QString> &defaults = fallbackPalette();
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it)
        fill(it.key(), it.value());
}

// mode precedence: `mode` key, legacy `theme_type`, a light.mode file beside
// colors.toml, background luminance, then dark.
void Theme::resolveMode(const QDir &themeDir)
{
    if (!m_colors.value(QStringLiteral("mode")).isEmpty())
        return;

    const QString legacy = m_colors.value(QStringLiteral("theme_type"));
    if (!legacy.isEmpty()) {
        m_colors.insert(QStringLiteral("mode"), legacy);
        return;
    }

    if (!themeDir.path().isEmpty() && themeDir.exists(QStringLiteral("light.mode"))) {
        m_colors.insert(QStringLiteral("mode"), QStringLiteral("light"));
        return;
    }

    const QString background = m_colors.value(QStringLiteral("background"));
    static const QRegularExpression hexColor(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (hexColor.match(background).hasMatch()) {
        const int luminance = background.mid(1, 2).toInt(nullptr, 16)
            + background.mid(3, 2).toInt(nullptr, 16)
            + background.mid(5, 2).toInt(nullptr, 16);
        m_colors.insert(QStringLiteral("mode"),
                        luminance > 382 ? QStringLiteral("light") : QStringLiteral("dark"));
        return;
    }

    m_colors.insert(QStringLiteral("mode"), QStringLiteral("dark"));
}

QString Theme::mix(const QString &start, const QString &end, double amount)
{
    const QColor from(start);
    const QColor to(end);
    if (!from.isValid() || !to.isValid())
        return QString();

    amount = qBound(0.0, amount, 1.0);
    const int red = qRound(from.red() * (1 - amount) + to.red() * amount);
    const int green = qRound(from.green() * (1 - amount) + to.green() * amount);
    const int blue = qRound(from.blue() * (1 - amount) + to.blue() * amount);

    return QColor(red, green, blue).name();
}

QString Theme::value(const QString &key, const QString &fallback) const
{
    const QString found = m_colors.value(key);
    if (!found.isEmpty())
        return found;
    if (fallback.isEmpty())
        return QString();

    // A fallback is tried as another palette key first, then used verbatim.
    const QString aliased = m_colors.value(fallback);
    return aliased.isEmpty() ? fallback : aliased;
}

QColor Theme::color(const QString &key, const QString &fallback) const
{
    return QColor(value(key, fallback));
}

QStringList Theme::keys() const
{
    QStringList all = m_colors.keys();
    all.sort();
    return all;
}

// omarchy-theme-set replaces the whole theme directory (rm -rf + mv), so a watch on
// colors.toml or on the theme dir itself is destroyed by every theme switch. The
// stable path is the parent, `current/`, which also receives the theme.name write
// that marks the swap as finished.
void Theme::rewatch()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &Theme::onWatchedPathChanged);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Theme::onWatchedPathChanged);
    }

    QStringList wanted;
    const QStringList candidates = themeDirCandidates();
    for (const QString &dir : candidates) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (QFileInfo::exists(parent))
            wanted << parent;
        if (QFileInfo::exists(dir))
            wanted << dir;
        const QString colors = dir + QStringLiteral("/colors.toml");
        if (QFileInfo::exists(colors))
            wanted << colors;
    }

    const QStringList current = m_watcher->files() + m_watcher->directories();
    if (current == wanted)
        return;

    if (!current.isEmpty())
        m_watcher->removePaths(current);
    if (!wanted.isEmpty())
        m_watcher->addPaths(wanted);
}

void Theme::onWatchedPathChanged()
{
    // Coalesce the burst of events a theme switch produces into one reload.
    m_debounce.start();
    rewatch();
}
