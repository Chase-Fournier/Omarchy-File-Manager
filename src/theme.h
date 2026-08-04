#pragma once

#include <QColor>
#include <QDir>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class QFileSystemWatcher;

// Reads Omarchy's active theme and exposes it to QML as a singleton.
//
// Quattro (4.x) keeps the palette in ~/.local/state/omarchy/current/theme/colors.toml;
// Omarchy 3.x kept it in ~/.config/omarchy/current/theme/. Both are checked, newest
// layout first, so one binary works on either.
//
// The resolution cascade below is a port of omarchy's `omarchy-theme-color`. Keeping
// it identical is the point: omafile must land on the same palette every other themed
// app does, including for themes that only define a partial key set.
class Theme : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(bool isDark READ isDark NOTIFY changed)

    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor bgDark READ bgDark NOTIFY changed)
    Q_PROPERTY(QColor bgDarker READ bgDarker NOTIFY changed)
    Q_PROPERTY(QColor bgLight READ bgLight NOTIFY changed)

    Q_PROPERTY(QColor fg READ fg NOTIFY changed)
    Q_PROPERTY(QColor fgLight READ fgLight NOTIFY changed)
    Q_PROPERTY(QColor fgBright READ fgBright NOTIFY changed)
    Q_PROPERTY(QColor dim READ dim NOTIFY changed)

    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)

    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    // The one entry point, per the build plan: everything about parsing lives behind it.
    void load(const QDir &themeDir);

    // Locate the active theme directory, or an empty string when Omarchy isn't installed.
    static QString currentThemeDir();

    QString name() const { return m_name; }
    QString mode() const { return value(QStringLiteral("mode")); }
    bool isDark() const { return mode() != QLatin1String("light"); }

    QColor bg() const { return color(QStringLiteral("background")); }
    QColor bgDark() const { return color(QStringLiteral("dark_background")); }
    QColor bgDarker() const { return color(QStringLiteral("darker_background")); }
    QColor bgLight() const { return color(QStringLiteral("lighter_background")); }

    QColor fg() const { return color(QStringLiteral("foreground")); }
    QColor fgLight() const { return color(QStringLiteral("light_foreground")); }
    QColor fgBright() const { return color(QStringLiteral("bright_foreground")); }
    QColor dim() const { return color(QStringLiteral("dark_foreground")); }

    QColor accent() const { return color(QStringLiteral("accent"), QStringLiteral("blue")); }
    QColor selection() const { return color(QStringLiteral("selection")); }
    QColor muted() const { return color(QStringLiteral("muted")); }

    QColor error() const { return color(QStringLiteral("red")); }
    QColor warning() const { return color(QStringLiteral("yellow")); }
    QColor success() const { return color(QStringLiteral("green")); }

    // Escape hatch for palette keys with no dedicated property (ansi names, brown, ...).
    Q_INVOKABLE QColor color(const QString &key, const QString &fallback = QString()) const;
    Q_INVOKABLE QString value(const QString &key, const QString &fallback = QString()) const;

    // Every resolved key, sorted. Used by --dump-theme and the tests.
    QStringList keys() const;

signals:
    void changed();

private slots:
    void onWatchedPathChanged();

private:
    void reload();
    void rewatch();
    void parse(const QString &colorsFile);
    void resolve(const QDir &themeDir);
    void resolveMode(const QDir &themeDir);
    void applyFallbackPalette();

    // Blend two hex colors; `amount` is 0..1 toward `end`. Mirrors omarchy's mix_color.
    static QString mix(const QString &start, const QString &end, double amount);

    QHash<QString, QString> m_colors;
    QString m_name;
    QString m_themeDir;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_debounce;
};
