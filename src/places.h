#pragma once

#include "hosts.h"

#include <QAbstractListModel>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>

// One row in the sidebar.
struct Place
{
    // Device is appended, not inserted: QML checks kinds by number and renumbering
    // would silently change what those checks mean.
    enum Kind { Folder, Bookmark, SshHost, RcloneRemote, Volume, Device };

    Kind kind = Folder;
    QString name;
    QString glyph;
    QString target;    // a Location string, or a host/remote name to connect to
    QString note;      // why it is unavailable, shown instead of acting
    QString mountPath; // set once mounted
    bool available = true;
    bool mounted = false;
    bool ejectable = false;
};

// The sidebar (§4, hidden behind Ctrl+B) and the whole of §10's "mount it and forget it
// is remote" strategy. Everything here either navigates to a path or runs one mount
// helper and then navigates to a path — there is no second browsing code path.
class Places : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        GlyphRole,
        TargetRole,
        NoteRole,
        AvailableRole,
        MountedRole,
        EjectableRole,
        KindRole,
    };

    explicit Places(QObject *parent = nullptr);
    ~Places() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_places.size()); }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void refresh();
    // Navigates, mounting first when the row is a host or remote that is not up yet.
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void eject(int row);

    Q_INVOKABLE void addBookmark(const QString &path);
    Q_INVOKABLE bool isBookmarked(const QString &path) const;
    Q_INVOKABLE void removeBookmark(const QString &path);

    // Ctrl+S: accepts ssh://, sftp://, smb://, davs://, mtp://, rclone:remote:path, a
    // bare ~/.ssh/config host name, or a plain path (§10.5).
    Q_INVOKABLE void connectTo(const QString &input);
    // Answers passwordRequired. The password is used once and never stored (§10.7).
    Q_INVOKABLE void providePassword(const QString &password);
    Q_INVOKABLE void cancelPassword();
    // The escape hatch: run the mount in a real terminal so ssh can ask whatever it
    // needs — passphrase, 2FA, a host key to trust — and answer it directly.
    Q_INVOKABLE void connectInTerminal(const QString &hostAlias);
    Q_INVOKABLE QStringList completions() const;

    // Releases every mount this process is holding, unmounting those nobody else wants.
    void releaseMounts();

signals:
    void countsChanged();
    void busyChanged();
    void navigate(const QString &location);
    void status(const QString &message);
    // The key or agent was not enough. The UI prompts, masked, and calls back.
    void passwordRequired(const QString &prompt);
    // Nothing automatic worked. The UI offers to hand the user a terminal.
    void connectFailed(const QString &hostAlias, const QString &message);

private:
    void rebuild();

    // The kernel reports POLLPRI on /proc/self/mountinfo whenever the mount table
    // changes, so a drive appearing is an event rather than something to poll for. Set
    // up on the first rebuild, which is the first time the sidebar is actually shown —
    // a window with it closed never opens the descriptor.
    void startWatchingMounts();
    // udisks answers "what is plugged in", which the mount table cannot: a drive nobody
    // has mounted appears nowhere in it. Created with the watcher, on first rebuild.
    void startWatchingDevices();
    QString deviceSignature() const;
    // The condition stays raised until the file is read again; leaving it would turn the
    // notifier into a busy loop.
    void drainMountInfo();

    int m_mountFd = -1;
    QSocketNotifier *m_mountNotifier = nullptr;
    QTimer *m_mountSettle = nullptr;
    // What the last rebuild saw, so unrelated churn does not reset the model.
    QString m_mountSignature;
    class UDisks *m_udisks = nullptr;
    // §14: an unclean exit must not leave a mount behind forever. Anything whose only
    // claimants are dead processes is swept on the next start.
    void sweepOrphanedMounts();
    void setBusy(bool busy);

    // Mount helpers. Each returns the local path the remote now appears at, or empty on
    // failure with `error` set.
    // An empty password means "try the agent and keys only, and fail fast".
    QString mountSsh(const QString &hostAlias, const QString &password, QString *error);
    // gvfs' own sftp backend, which is how every other file manager on this desktop does
    // it: it owns the authentication dialogs, so passphrases, host-key trust and
    // keyboard-interactive all just work.
    static bool hasGvfsSftp();
    static SshHost hostFor(const QString &alias);
    // Where to open once mounted: the remote home if we can find it, else the mount root.
    static QString landingPathFor(const QString &mountPath, const SshHost &host);
    QString mountSftpViaGio(const SshHost &host, QString *error);
    // Distinguishes "wrong credentials" from "host is down", so we only prompt when a
    // password could actually help.
    static bool looksLikeAuthFailure(const QString &error);
    void finishConnect(const QString &mountPath);
    QString mountRclone(const QString &remote, QString *error);
    QString mountGio(const QString &uri, QString *error);

    // §10.1.4: a mount is shared between windows, so it is only torn down when the last
    // holder lets go. Held as one file per PID in the runtime directory, which also
    // makes a crashed window's claim self-evidently stale.
    void claimMount(const QString &key);
    bool releaseMount(const QString &key); // true when nobody else holds it
    static QString refsDir(const QString &key);

    // §12: the sidebar's contents cost PATH lookups and a config parse, so they are not
    // built until something actually asks for them.
    mutable bool m_built = false;
    QList<Place> m_places;
    QStringList m_bookmarks;
    QStringList m_heldMounts;
    bool m_busy = false;

    // What a pending password prompt is for. Cleared as soon as it is answered.
    QString m_pendingHost;
    QString m_pendingSubPath;
    // Watches for a terminal-driven mount to appear.
    class QTimer *m_terminalWatch = nullptr;
    QString m_terminalHost;
};
