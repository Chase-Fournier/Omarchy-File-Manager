#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// The same shape as Omarchy's own colors.toml: `key = value`, one per line, comments
// with '#'. Small enough that a dependency would be absurd.
QHash<QString, QString> readToml(const QString &path)
{
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;

        QString value = line.mid(equals + 1).trimmed();
        value.remove(QLatin1Char('"'));
        value.remove(QLatin1Char('\''));
        values.insert(line.left(equals).trimmed().toLower(), value.toLower());
    }
    return values;
}

// Case-folded here rather than relying on the caller: a command-line flag and a config
// value both reach this, and only one of them has been normalised on the way in.
bool isTrue(const QString &value)
{
    const QString lower = value.trimmed().toLower();
    return lower == QLatin1String("true") || lower == QLatin1String("on")
        || lower == QLatin1String("yes") || lower == QLatin1String("1");
}

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &Settings::save);
    load();
}

Settings::~Settings()
{
    // A window closed straight after a toggle must still remember it.
    if (m_saveTimer.isActive())
        save();
}

QString Settings::configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/omafile/config.toml");
}

QString Settings::statePath()
{
    // XDG_STATE_HOME is the right place for "what it looked like last time" — the same
    // move Omarchy itself made for its current theme.
    QString state = QString::fromLocal8Bit(qgetenv("XDG_STATE_HOME"));
    if (state.isEmpty())
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/omafile/state.toml");
}

bool Settings::resolve(const QString &override, const QString &configured, bool remembered)
{
    // An explicit instruction always wins, whichever file or flag it came from.
    if (!override.isEmpty())
        return isTrue(override);
    if (!configured.isEmpty() && configured.trimmed().toLower() != QLatin1String("remember"))
        return isTrue(configured);
    return remembered;
}

void Settings::load()
{
    const QHash<QString, QString> state = readToml(statePath());
    m_sidebar = isTrue(state.value(QStringLiteral("sidebar")));
    m_preview = isTrue(state.value(QStringLiteral("preview")));
    m_loaded = true;
}

void Settings::applyOverrides(const QString &sidebar, const QString &preview)
{
    const QHash<QString, QString> config = readToml(configPath());

    m_sidebar = resolve(sidebar, config.value(QStringLiteral("sidebar")), m_sidebar);
    m_preview = resolve(preview, config.value(QStringLiteral("preview")), m_preview);
    emit changed();
}

void Settings::setSidebar(bool on)
{
    if (m_sidebar == on)
        return;
    m_sidebar = on;
    emit changed();
    m_saveTimer.start();
}

void Settings::setPreview(bool on)
{
    if (m_preview == on)
        return;
    m_preview = on;
    emit changed();
    m_saveTimer.start();
}

void Settings::save()
{
    if (!m_loaded)
        return;

    const QString path = statePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << "# Written by omafile. Set these in "
        << QStringLiteral("config.toml") << " to override.\n";
    out << "sidebar = " << (m_sidebar ? "true" : "false") << '\n';
    out << "preview = " << (m_preview ? "true" : "false") << '\n';
}
