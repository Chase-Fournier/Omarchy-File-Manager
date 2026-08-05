#pragma once

#include "entry.h"
#include "location.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QThread>

class Lister;
class Watcher;

// The list of entries for one Location, and the only thing the UI talks to.
//
// Two lists are kept: `m_all` is everything the Lister found, sorted; `m_rows` is what
// the view shows after the hidden-file and filter passes. A watcher-triggered re-list
// diffs into m_rows one row at a time rather than resetting the model, which is what
// keeps scroll position and the current selection stable while files change underneath.
class DirectoryModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString path READ path NOTIFY locationChanged)
    Q_PROPERTY(QString displayPath READ displayPath NOTIFY locationChanged)
    Q_PROPERTY(QStringList segments READ segments NOTIFY locationChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY locationChanged)

    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortChanged)
    Q_PROPERTY(bool sortReversed READ sortReversed WRITE setSortReversed NOTIFY sortChanged)

    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    // Notified by currentDetailsChanged, not currentIndexChanged: the size text also
    // changes when a stat lands for the row already under the cursor, and conflating the
    // two made every scroll snap the view back (see onStatsReady).
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentDetailsChanged)
    Q_PROPERTY(QString currentSizeText READ currentSizeText NOTIFY currentDetailsChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)

public:
    enum SortMode { SortName, SortSize, SortTime };
    Q_ENUM(SortMode)

    enum Roles {
        NameRole = Qt::UserRole + 1,
        GlyphRole,
        IsDirRole,
        IsHiddenRole,
        IsBrokenRole,
        IsSelectedRole,
        SizeTextRole,
        TimeTextRole,
        MatchPositionsRole,
    };

    explicit DirectoryModel(QObject *parent = nullptr);
    ~DirectoryModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Location location() const { return m_location; }
    void setLocation(const Location &location);

    QString path() const { return m_location.toString(); }
    QString displayPath() const { return m_location.displayPath(); }
    QStringList segments() const { return m_location.segments(); }
    bool atRoot() const { return m_location.isRoot(); }

    int count() const { return int(m_rows.size()); }
    int totalCount() const { return int(m_all.size()); }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);
    SortMode sortMode() const { return m_sortMode; }
    void setSortMode(SortMode mode);
    bool sortReversed() const { return m_sortReversed; }
    void setSortReversed(bool reversed);

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    // resetAnchor is false only for the extending moves, which must keep their origin.
    void setCurrent(int index, bool resetAnchor);
    QString currentName() const;
    QString currentSizeText() const;

    // Navigation. Everything the UI can do to change location goes through these.
    Q_INVOKABLE void navigate(const QString &input);
    Q_INVOKABLE void navigateToSegment(int segmentIndex);
    Q_INVOKABLE void goParent();
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void activateInNewWindow(int row);
    Q_INVOKABLE void openNewWindowHere();
    Q_INVOKABLE void refresh();

    // Called by the view as it scrolls: stat only what is on screen, plus a buffer.
    Q_INVOKABLE void ensureStats(int first, int last);

    // Select an entry by name. Applied when the listing lands if one is still running,
    // which is how --select works without blocking startup on I/O.
    Q_INVOKABLE void selectByName(const QString &name);

    Q_INVOKABLE void moveCurrent(int delta);
    Q_INVOKABLE void setCurrentToEdge(bool last);
    Q_INVOKABLE QString rowPath(int row) const;
    Q_INVOKABLE bool rowIsDir(int row) const;
    Q_INVOKABLE void cycleSort();

    // Selection. Tracked by name rather than row so it survives a watcher diff, a filter
    // change, and a re-sort.
    int selectionCount() const { return int(m_selected.size()); }
    Q_INVOKABLE void toggleSelection(int row);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    // Shift+Up/Down: move the cursor and take everything between the anchor and it.
    Q_INVOKABLE void extendSelection(int delta);
    // Shift+click: select everything between the anchor and this row.
    Q_INVOKABLE void selectTo(int row);
    // Select a set of entries by name, used to leave a drop's results selected. Like
    // selectByName, it waits for an in-flight listing rather than selecting nothing.
    Q_INVOKABLE void selectNames(const QStringList &names);

    // What an operation should act on: the explicit selection, or the current row when
    // nothing is explicitly selected. Every verb in the UI goes through this.
    Q_INVOKABLE QStringList actionPaths() const;
    Q_INVOKABLE QStringList actionNames() const;

signals:
    void locationChanged();
    void countsChanged();
    void loadingChanged();
    void errorChanged();
    void filterChanged();
    void showHiddenChanged();
    void sortChanged();
    // The cursor moved to a different row. This is what scrolls the view.
    void currentIndexChanged();
    // The current row's *contents* changed — a stat landed, say. Never scrolls anything.
    void currentDetailsChanged();
    void selectionChanged();

private slots:
    void onBatch(quint64 generation, const QList<Entry> &entries);
    void onFinished(quint64 generation, int total);
    void onFailed(quint64 generation, const QString &message);
    void onStatsReady(quint64 generation, int firstRow, const QList<Entry> &entries);
    void onDirectoryChanged();

private:
    // A visible row: the entry plus where the filter matched, for highlighting.
    struct Row
    {
        Entry entry;
        // Which characters of the name the filter matched, for highlighting. Empty when
        // no filter is active.
        QList<int> positions;
        int score = 0;
    };

    void startListing(bool diff);
    bool lessThan(const Entry &a, const Entry &b) const;
    QList<Row> buildRows() const;
    void applyRows(const QList<Row> &target);
    void resetRows();
    void rebuildIndex();
    void requestAllStats();
    void restoreCurrentByName(const QString &name);
    void applySelectNames(const QStringList &names);
    void setLoading(bool loading);

    Location m_location;
    QList<Entry> m_all;
    QList<Row> m_rows;
    QHash<QString, int> m_indexInAll;

    QList<Entry> m_incoming;
    quint64 m_generation = 0;
    bool m_diffPending = false;
    bool m_loading = false;
    QString m_error;

    QString m_filter;
    bool m_showHidden = false;
    SortMode m_sortMode = SortName;
    bool m_sortReversed = false;
    int m_currentIndex = -1;

    // Tracked by name because rows move as the directory changes underneath.
    QSet<QString> m_statRequested;

    // Set by goParent so the directory just left is selected once the listing lands.
    QString m_pendingSelect;

    // Where the cursor was in each directory previously visited, so walking back down a
    // path lands on the same entries it came up through. Bounded, because a session that
    // browses a hundred thousand directories should not remember all of them.
    void rememberCursor();
    QHash<QString, QString> m_cursorMemory;
    QStringList m_cursorOrder;
    // Set by an operation's results, applied on the same listing boundary.
    QStringList m_pendingSelectNames;

    QSet<QString> m_selected;
    // Where a range selection measures from. Plain cursor moves reset it; extending
    // moves deliberately do not, so Shift+Down Down Down grows one run.
    int m_anchor = -1;

    QThread m_thread;
    Lister *m_lister = nullptr;
    Watcher *m_watcher = nullptr;
};
