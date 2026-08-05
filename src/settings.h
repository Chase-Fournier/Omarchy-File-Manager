#pragma once

#include <QObject>
#include <QTimer>

// What the window looked like last time, and how to override that.
//
// §1 rules out a settings UI: this is a small file nobody needs to open. Two files,
// because they answer different questions and only one of them is ours to write:
//
//   $XDG_CONFIG_HOME/omafile/config.toml   yours. "always start with the sidebar open."
//   $XDG_STATE_HOME/omafile/state.toml     ours. "the sidebar was open last time."
//
// Precedence, strongest first: a command-line flag, then the config file, then the
// remembered state, then off. So the remembered state is a convenience that never fights
// an explicit instruction.
class Settings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool sidebar READ sidebar WRITE setSidebar NOTIFY changed)
    Q_PROPERTY(bool preview READ preview WRITE setPreview NOTIFY changed)

public:
    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    bool sidebar() const { return m_sidebar; }
    void setSidebar(bool on);
    bool preview() const { return m_preview; }
    void setPreview(bool on);

    // Called once at startup with whatever the command line said. An empty string means
    // "not specified", which is different from "off".
    void applyOverrides(const QString &sidebar, const QString &preview);

    // Split out so the precedence rules are testable without a home directory.
    // `configured` is the config file's value: "on", "off", "remember" or empty.
    static bool resolve(const QString &override, const QString &configured, bool remembered);

    static QString configPath();
    static QString statePath();

signals:
    void changed();

private:
    void load();
    void save();

    bool m_sidebar = false;
    bool m_preview = false;
    // Writing on every keystroke-fast toggle would be silly; this coalesces.
    QTimer m_saveTimer;
    bool m_loaded = false;
};
