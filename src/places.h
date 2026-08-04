#pragma once

#include <QAbstractListModel>
#include <QProcess>

// One row in the sidebar.
struct Place
{
    enum Kind { Folder, Bookmark, SshHost, RcloneRemote, Volume };

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
    Q_INVOKABLE QStringList completions() const;

    // Releases every mount this process is holding, unmounting those nobody else wants.
    void releaseMounts();

signals:
    void countsChanged();
    void busyChanged();
    void navigate(const QString &location);
    void status(const QString &message);

private:
    void rebuild();
    void setBusy(bool busy);

    // Mount helpers. Each returns the local path the remote now appears at, or empty on
    // failure with `error` set.
    QString mountSsh(const QString &hostAlias, QString *error);
    QString mountRclone(const QString &remote, QString *error);
    QString mountGio(const QString &uri, QString *error);

    // §10.1.4: a mount is shared between windows, so it is only torn down when the last
    // holder lets go. Held as one file per PID in the runtime directory, which also
    // makes a crashed window's claim self-evidently stale.
    void claimMount(const QString &key);
    bool releaseMount(const QString &key); // true when nobody else holds it
    static QString refsDir(const QString &key);

    QList<Place> m_places;
    QStringList m_bookmarks;
    QStringList m_heldMounts;
    bool m_busy = false;
};
