#include "directorymodel.h"

#include "formatting.h"
#include "fuzzyscorer.h"
#include "lister.h"
#include "mounts.h"
#include "opener.h"
#include "watcher.h"

#include <QDateTime>
#include <QDir>

namespace {

// Rows just outside the viewport are stat'd too, so scrolling reveals filled-in rows
// rather than blank ones that populate a frame later.
constexpr int kStatBuffer = 24;

// Nerd Font glyphs, from the font Omarchy already ships. One weight, one color — the
// type is a hint, not a decoration, per §4.
QString glyphFor(const Entry &entry)
{
    if (entry.isBrokenSymlink())
        return QStringLiteral(""); // broken link
    if (entry.isDir())
        return QStringLiteral("");
    if (entry.type == Entry::Other)
        return QStringLiteral(""); // device, socket, fifo

    const int dot = entry.name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return QStringLiteral("");

    const QString suffix = entry.name.mid(dot + 1).toLower();
    static const QHash<QString, QString> bySuffix = {
        { QStringLiteral("png"), QStringLiteral("") },
        { QStringLiteral("jpg"), QStringLiteral("") },
        { QStringLiteral("jpeg"), QStringLiteral("") },
        { QStringLiteral("gif"), QStringLiteral("") },
        { QStringLiteral("webp"), QStringLiteral("") },
        { QStringLiteral("svg"), QStringLiteral("") },
        { QStringLiteral("bmp"), QStringLiteral("") },
        { QStringLiteral("mp4"), QStringLiteral("") },
        { QStringLiteral("mkv"), QStringLiteral("") },
        { QStringLiteral("webm"), QStringLiteral("") },
        { QStringLiteral("mov"), QStringLiteral("") },
        { QStringLiteral("mp3"), QStringLiteral("") },
        { QStringLiteral("flac"), QStringLiteral("") },
        { QStringLiteral("wav"), QStringLiteral("") },
        { QStringLiteral("ogg"), QStringLiteral("") },
        { QStringLiteral("pdf"), QStringLiteral("") },
        { QStringLiteral("zip"), QStringLiteral("") },
        { QStringLiteral("gz"), QStringLiteral("") },
        { QStringLiteral("xz"), QStringLiteral("") },
        { QStringLiteral("zst"), QStringLiteral("") },
        { QStringLiteral("bz2"), QStringLiteral("") },
        { QStringLiteral("tar"), QStringLiteral("") },
        { QStringLiteral("7z"), QStringLiteral("") },
        { QStringLiteral("rar"), QStringLiteral("") },
        { QStringLiteral("md"), QStringLiteral("") },
        { QStringLiteral("txt"), QStringLiteral("") },
        { QStringLiteral("log"), QStringLiteral("") },
    };

    const auto found = bySuffix.constFind(suffix);
    if (found != bySuffix.constEnd())
        return found.value();

    // Anything else that looks like source or config gets the code glyph.
    static const QSet<QString> codeSuffixes = {
        QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("cpp"),
        QStringLiteral("hpp"), QStringLiteral("cc"), QStringLiteral("qml"),
        QStringLiteral("py"), QStringLiteral("rs"), QStringLiteral("go"),
        QStringLiteral("js"), QStringLiteral("ts"), QStringLiteral("jsx"),
        QStringLiteral("tsx"), QStringLiteral("sh"), QStringLiteral("bash"),
        QStringLiteral("rb"), QStringLiteral("lua"), QStringLiteral("json"),
        QStringLiteral("toml"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("ini"), QStringLiteral("conf"), QStringLiteral("pro"),
        QStringLiteral("qrc"), QStringLiteral("html"), QStringLiteral("css"),
    };
    return codeSuffixes.contains(suffix) ? QStringLiteral("")
                                         : QStringLiteral("");
}

int compareNames(const QString &a, const QString &b)
{
    const int insensitive = a.compare(b, Qt::CaseInsensitive);
    if (insensitive != 0)
        return insensitive;
    // Tiebreak so equal-ignoring-case names have a stable, reproducible order.
    return a.compare(b, Qt::CaseSensitive);
}

} // namespace

DirectoryModel::DirectoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    qRegisterMetaType<QList<Entry>>("QList<Entry>");

    m_lister = new Lister;
    m_watcher = new Watcher;
    m_lister->moveToThread(&m_thread);
    m_watcher->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_lister, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, m_watcher, &QObject::deleteLater);

    connect(m_lister, &Lister::batch, this, &DirectoryModel::onBatch);
    connect(m_lister, &Lister::finished, this, &DirectoryModel::onFinished);
    connect(m_lister, &Lister::failed, this, &DirectoryModel::onFailed);
    connect(m_lister, &Lister::statsReady, this, &DirectoryModel::onStatsReady);
    connect(m_watcher, &Watcher::changed, this, &DirectoryModel::onDirectoryChanged);

    m_thread.start();
}

DirectoryModel::~DirectoryModel()
{
    // Make any in-flight walk abandon its work before we tear the thread down.
    m_lister->cancelTo(m_generation + 1);
    m_thread.quit();
    m_thread.wait();
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { GlyphRole, "glyph" },
        { IsDirRole, "isDir" },
        { IsHiddenRole, "isHidden" },
        { IsBrokenRole, "isBroken" },
        { IsSelectedRole, "isSelected" },
        { SizeTextRole, "sizeText" },
        { TimeTextRole, "timeText" },
        { MatchPositionsRole, "matchPositions" },
    };
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    const Entry &entry = row.entry;

    switch (role) {
    case NameRole:
        return entry.name;
    case GlyphRole:
        return glyphFor(entry);
    case IsDirRole:
        return entry.isDir();
    case IsHiddenRole:
        return entry.isHidden();
    case IsBrokenRole:
        return entry.isBrokenSymlink();
    case IsSelectedRole:
        return m_selected.contains(entry.name);
    case SizeTextRole:
        // Directories have a size on disk, but it means nothing to a user reading a list.
        if (entry.isDir() || !entry.statted)
            return QString();
        return Formatting::humanSize(entry.size);
    case TimeTextRole:
        if (!entry.statted)
            return QString();
        return Formatting::relativeTime(entry.mtime, QDateTime::currentSecsSinceEpoch());
    case MatchPositionsRole: {
        QVariantList positions;
        positions.reserve(row.positions.size());
        for (int position : row.positions)
            positions.append(position);
        return positions;
    }
    default:
        return {};
    }
}

QString DirectoryModel::homePath()
{
    return QDir::homePath();
}

void DirectoryModel::setLocation(const Location &location)
{
    if (!location.isValid() || location == m_location)
        return;

    // Note where the cursor was before leaving, so coming back lands on it.
    rememberCursor();

    m_location = location;
    emit locationChanged();

    // A new location means the old filter is meaningless.
    if (!m_filter.isEmpty()) {
        m_filter.clear();
        emit filterChanged();
    }

    m_currentIndex = -1;
    emit currentIndexChanged();
    emit currentDetailsChanged();

    m_selected.clear();
    emit selectionChanged();

    startListing(false);
}

void DirectoryModel::startListing(bool diff)
{
    // Bumping the generation cancels any walk still running, immediately and from this
    // thread — the worker sees it inside its loop rather than after finishing.
    ++m_generation;
    m_lister->cancelTo(m_generation);

    m_incoming.clear();
    m_diffPending = diff;
    m_statRequested.clear();
    setLoading(true);

    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }

    if (!diff) {
        m_all.clear();
        m_indexInAll.clear();
        resetRows();
    }

    const QString local = m_location.localPath();

    // Watch first, list second. Both run on the same worker thread, so ordering the
    // calls this way closes the window where a file created during the initial listing
    // would be missed by inotify and never show up.
    QMetaObject::invokeMethod(m_watcher, "watch", Qt::QueuedConnection, Q_ARG(QString, local));
    QMetaObject::invokeMethod(m_lister, "list", Qt::QueuedConnection, Q_ARG(QString, local),
                              Q_ARG(quint64, m_generation));
}

void DirectoryModel::onBatch(quint64 generation, const QList<Entry> &entries)
{
    if (generation != m_generation)
        return;
    m_incoming.append(entries);
}

void DirectoryModel::onFinished(quint64 generation, int)
{
    if (generation != m_generation)
        return;

    // Carry stat results across a re-list so rows don't blank out while the watcher
    // churns. The visible window is re-stat'd right after, so nothing stays stale.
    if (m_diffPending && !m_all.isEmpty()) {
        for (Entry &entry : m_incoming) {
            const auto found = m_indexInAll.constFind(entry.name);
            if (found == m_indexInAll.constEnd())
                continue;
            const Entry &previous = m_all.at(found.value());
            if (previous.statted) {
                entry.size = previous.size;
                entry.mtime = previous.mtime;
                entry.mode = previous.mode;
                entry.linkTarget = previous.linkTarget;
                entry.statted = true;
            }
        }
    }

    QString keepName = currentName();
    if (!m_pendingSelect.isEmpty()) {
        // An explicit request — --select, the directory just left, or what an operation
        // produced — outranks anything remembered.
        keepName = m_pendingSelect;
        m_pendingSelect.clear();
    } else if (keepName.isEmpty()) {
        keepName = m_cursorMemory.value(m_location.toString());
    }

    m_all = std::move(m_incoming);
    m_incoming.clear();
    std::sort(m_all.begin(), m_all.end(),
              [this](const Entry &a, const Entry &b) { return lessThan(a, b); });
    rebuildIndex();

    if (m_diffPending && m_filter.isEmpty())
        applyRows(buildRows());
    else
        resetRows();

    // Drop selected names that no longer exist, or a trashed file would keep haunting
    // the count in the status bar.
    if (!m_selected.isEmpty()) {
        const int before = int(m_selected.size());
        QSet<QString> surviving;
        for (const Entry &entry : m_all) {
            if (m_selected.contains(entry.name))
                surviving.insert(entry.name);
        }
        m_selected = std::move(surviving);
        if (int(m_selected.size()) != before)
            emit selectionChanged();
    }

    m_diffPending = false;
    setLoading(false);
    emit countsChanged();

    restoreCurrentByName(keepName);

    if (!m_pendingSelectNames.isEmpty()) {
        applySelectNames(m_pendingSelectNames);
        m_pendingSelectNames.clear();
    }

    // Size and time sorting cannot be done from d_type alone.
    if (m_sortMode != SortName)
        requestAllStats();
}

void DirectoryModel::onFailed(quint64 generation, const QString &message)
{
    if (generation != m_generation)
        return;

    m_all.clear();
    m_indexInAll.clear();
    m_incoming.clear();
    resetRows();

    m_error = message;
    m_diffPending = false;
    emit errorChanged();
    emit countsChanged();
    setLoading(false);
}

void DirectoryModel::onStatsReady(quint64 generation, int firstRow, const QList<Entry> &entries)
{
    if (generation != m_generation)
        return;

    for (int i = 0; i < entries.size(); ++i) {
        const Entry &fresh = entries.at(i);

        const auto inAll = m_indexInAll.constFind(fresh.name);
        if (inAll != m_indexInAll.constEnd())
            m_all[inAll.value()] = fresh;

        // Rows usually sit exactly where they were asked for; only look further if the
        // directory changed between the request and the reply.
        int row = firstRow + i;
        if (row < 0 || row >= m_rows.size() || m_rows.at(row).entry.name != fresh.name) {
            row = -1;
            for (int j = 0; j < m_rows.size(); ++j) {
                if (m_rows.at(j).entry.name == fresh.name) {
                    row = j;
                    break;
                }
            }
        }
        if (row < 0)
            continue;

        m_rows[row].entry = fresh;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed);
    }

    // A stat can change what a symlink resolves to, and size/time sorting depends on
    // values that only exist now.
    if (m_sortMode != SortName) {
        std::sort(m_all.begin(), m_all.end(),
                  [this](const Entry &a, const Entry &b) { return lessThan(a, b); });
        rebuildIndex();
        const QString keepName = currentName();
        applyRows(buildRows());
        restoreCurrentByName(keepName);
    }

    // Deliberately not currentIndexChanged: the cursor has not moved, only its row's
    // details have. Emitting the index signal here made a stat arriving mid-scroll drag
    // the view back to the selection, which broke scrolling outright.
    emit currentDetailsChanged();
}

void DirectoryModel::onDirectoryChanged()
{
    startListing(true);
}

bool DirectoryModel::lessThan(const Entry &a, const Entry &b) const
{
    // Directories always come first, regardless of sort direction — reversing the sort
    // should not bury the folders at the bottom.
    if (a.isDir() != b.isDir())
        return a.isDir();

    int result = 0;
    switch (m_sortMode) {
    case SortName:
        result = compareNames(a.name, b.name);
        break;
    case SortSize:
        result = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
        if (result == 0)
            result = compareNames(a.name, b.name);
        break;
    case SortTime:
        result = a.mtime < b.mtime ? -1 : (a.mtime > b.mtime ? 1 : 0);
        if (result == 0)
            result = compareNames(a.name, b.name);
        break;
    }

    return m_sortReversed ? result > 0 : result < 0;
}

QList<DirectoryModel::Row> DirectoryModel::buildRows() const
{
    QList<Row> rows;
    rows.reserve(m_all.size());

    for (const Entry &entry : m_all) {
        if (!m_showHidden && entry.isHidden())
            continue;

        Row row;
        row.entry = entry;

        if (!m_filter.isEmpty()) {
            // Tier 1 of §6: fuzzy, in memory, no syscalls at all.
            const FuzzyScorer::Result match = FuzzyScorer::score(m_filter, entry.name);
            if (!match.matched)
                continue;
            row.positions = match.positions;
            row.score = match.score;
        }

        rows.append(std::move(row));
    }

    // With a filter active the list is a ranking, not a directory listing: best match
    // first, exactly like the launcher. Directories keep their priority only as a
    // tiebreak between equally good matches.
    if (!m_filter.isEmpty()) {
        std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
            if (a.score != b.score)
                return a.score > b.score;
            if (a.entry.isDir() != b.entry.isDir())
                return a.entry.isDir();
            return false;
        });
    }

    return rows;
}

void DirectoryModel::resetRows()
{
    beginResetModel();
    m_rows = buildRows();
    endResetModel();
}

// Merge-walk two lists that are sorted by the same comparator, emitting one insert or
// remove per difference. Cheap for the common case (a file appears or vanishes) and
// correct for the rest.
void DirectoryModel::applyRows(const QList<Row> &target)
{
    int i = 0;
    int j = 0;

    while (i < m_rows.size() || j < target.size()) {
        if (j >= target.size()) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
            continue;
        }
        if (i >= m_rows.size()) {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, target.at(j));
            endInsertRows();
            ++i;
            ++j;
            continue;
        }

        const Row &existing = m_rows.at(i);
        const Row &fresh = target.at(j);

        if (existing.entry.name == fresh.entry.name) {
            if (existing.entry.size != fresh.entry.size
                || existing.entry.mtime != fresh.entry.mtime
                || existing.entry.type != fresh.entry.type
                || existing.entry.linkTarget != fresh.entry.linkTarget
                || existing.positions != fresh.positions) {
                m_rows[i] = fresh;
                const QModelIndex changed = index(i);
                emit dataChanged(changed, changed);
            }
            ++i;
            ++j;
            continue;
        }

        if (lessThan(fresh.entry, existing.entry)) {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, fresh);
            endInsertRows();
            ++i;
            ++j;
        } else {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }

    emit countsChanged();
}

void DirectoryModel::rebuildIndex()
{
    m_indexInAll.clear();
    m_indexInAll.reserve(m_all.size());
    for (int i = 0; i < m_all.size(); ++i)
        m_indexInAll.insert(m_all.at(i).name, i);
}

void DirectoryModel::restoreCurrentByName(const QString &name)
{
    if (m_rows.isEmpty()) {
        setCurrentIndex(-1);
        return;
    }
    if (name.isEmpty()) {
        setCurrentIndex(m_currentIndex < 0 ? 0 : qBound(0, m_currentIndex, count() - 1));
        return;
    }

    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).entry.name == name) {
            setCurrentIndex(i);
            return;
        }
    }

    // The selected file is gone; stay where it was rather than jumping to the top.
    setCurrentIndex(qBound(0, m_currentIndex, count() - 1));
}

// Cheap and bounded: a few hundred remembered positions is a rounding error next to one
// directory listing, and forgetting the oldest is never surprising.
void DirectoryModel::rememberCursor()
{
    constexpr int kRemembered = 256;

    const QString name = currentName();
    if (name.isEmpty() || !m_location.isValid())
        return;

    const QString key = m_location.toString();
    if (!m_cursorMemory.contains(key))
        m_cursorOrder.append(key);
    m_cursorMemory.insert(key, name);

    while (m_cursorOrder.size() > kRemembered)
        m_cursorMemory.remove(m_cursorOrder.takeFirst());
}

void DirectoryModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void DirectoryModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;

    m_filter = filter;
    emit filterChanged();

    // Filtering reorders nothing, so a reset is both correct and the cheapest option;
    // it also drops the scroll position, which is what typing a filter should do.
    resetRows();
    emit countsChanged();
    setCurrentIndex(m_rows.isEmpty() ? -1 : 0);
}

void DirectoryModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;

    m_showHidden = show;
    emit showHiddenChanged();

    const QString keepName = currentName();
    applyRows(buildRows());
    restoreCurrentByName(keepName);
}

void DirectoryModel::setSortMode(SortMode mode)
{
    if (m_sortMode == mode)
        return;

    m_sortMode = mode;
    emit sortChanged();

    const QString keepName = currentName();
    std::sort(m_all.begin(), m_all.end(),
              [this](const Entry &a, const Entry &b) { return lessThan(a, b); });
    rebuildIndex();
    applyRows(buildRows());
    restoreCurrentByName(keepName);

    if (m_sortMode != SortName)
        requestAllStats();
}

void DirectoryModel::setSortReversed(bool reversed)
{
    if (m_sortReversed == reversed)
        return;

    m_sortReversed = reversed;
    emit sortChanged();

    const QString keepName = currentName();
    std::sort(m_all.begin(), m_all.end(),
              [this](const Entry &a, const Entry &b) { return lessThan(a, b); });
    rebuildIndex();
    applyRows(buildRows());
    restoreCurrentByName(keepName);
}

void DirectoryModel::setCurrentIndex(int index)
{
    setCurrent(index, true);
}

void DirectoryModel::setCurrent(int index, bool resetAnchor)
{
    const int clamped = m_rows.isEmpty() ? -1 : qBound(0, index, count() - 1);
    if (resetAnchor)
        m_anchor = clamped;
    if (clamped == m_currentIndex)
        return;
    m_currentIndex = clamped;
    emit currentIndexChanged();
    emit currentDetailsChanged();
}

QString DirectoryModel::currentName() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_rows.size())
        return QString();
    return m_rows.at(m_currentIndex).entry.name;
}

QString DirectoryModel::currentSizeText() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_rows.size())
        return QString();
    const Entry &entry = m_rows.at(m_currentIndex).entry;
    if (entry.isDir() || !entry.statted)
        return QString();
    return Formatting::humanSize(entry.size);
}

void DirectoryModel::navigate(const QString &input)
{
    const Location target = Location::parse(input, m_location);
    if (target.isValid())
        setLocation(target);
}

void DirectoryModel::navigateToSegment(int segmentIndex)
{
    const QStringList parts = segments();
    if (segmentIndex < 0 || segmentIndex >= parts.size())
        return;

    // Walking up from the current location keeps the scheme and host without having to
    // rebuild a URI out of display strings.
    Location target = m_location;
    for (int i = parts.size() - 1; i > segmentIndex; --i)
        target = target.parent();
    setLocation(target);
}

void DirectoryModel::goParent()
{
    if (m_location.isRoot())
        return;
    // The root of a mount omafile made is as far up as this goes: above it is only
    // $XDG_RUNTIME_DIR/omafile, which is plumbing, not a place.
    if (Mounts::isOwnMountRoot(m_location.localPath()))
        return;

    // Select the directory we came out of, so Backspace then Enter is a no-op.
    const QString leaving = m_location.displayName();
    const Location parent = m_location.parent();
    setLocation(parent);
    m_pendingSelect = leaving;
}

void DirectoryModel::activate(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;

    const Entry &entry = m_rows.at(row).entry;
    if (entry.isDir())
        setLocation(m_location.child(entry.name));
    else
        Opener::open(m_location.child(entry.name).localPath());
}

void DirectoryModel::activateInNewWindow(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;

    const Entry &entry = m_rows.at(row).entry;
    const Location target = m_location.child(entry.name);
    if (entry.isDir())
        Opener::openInNewWindow(target.toString());
    else
        Opener::open(target.localPath());
}

void DirectoryModel::openNewWindowHere()
{
    Opener::openInNewWindow(m_location.toString());
}

void DirectoryModel::refresh()
{
    startListing(true);
}

void DirectoryModel::ensureStats(int first, int last)
{
    if (m_rows.isEmpty())
        return;

    const int from = qBound(0, first - kStatBuffer, count() - 1);
    const int to = qBound(0, last + kStatBuffer, count() - 1);

    QStringList names;
    int firstRow = -1;
    for (int i = from; i <= to; ++i) {
        const Entry &entry = m_rows.at(i).entry;
        if (entry.statted || m_statRequested.contains(entry.name))
            continue;
        if (firstRow < 0)
            firstRow = i;
        names.append(entry.name);
        m_statRequested.insert(entry.name);
    }

    if (names.isEmpty())
        return;

    // firstRow is only a hint for reuniting the reply with its rows; onStatsReady
    // falls back to a name lookup when the directory shifted in the meantime.
    QMetaObject::invokeMethod(m_lister, "statRange", Qt::QueuedConnection,
                              Q_ARG(QString, m_location.localPath()),
                              Q_ARG(quint64, m_generation), Q_ARG(int, firstRow),
                              Q_ARG(QStringList, names));
}

void DirectoryModel::requestAllStats()
{
    QStringList names;
    names.reserve(m_all.size());
    for (const Entry &entry : m_all) {
        if (entry.statted || m_statRequested.contains(entry.name))
            continue;
        names.append(entry.name);
        m_statRequested.insert(entry.name);
    }
    if (names.isEmpty())
        return;

    QMetaObject::invokeMethod(m_lister, "statRange", Qt::QueuedConnection,
                              Q_ARG(QString, m_location.localPath()),
                              Q_ARG(quint64, m_generation), Q_ARG(int, -1),
                              Q_ARG(QStringList, names));
}

void DirectoryModel::selectByName(const QString &name)
{
    if (name.isEmpty())
        return;
    if (m_loading) {
        m_pendingSelect = name;
        return;
    }
    restoreCurrentByName(name);
}

void DirectoryModel::moveCurrent(int delta)
{
    if (m_rows.isEmpty())
        return;
    setCurrentIndex(qBound(0, (m_currentIndex < 0 ? 0 : m_currentIndex) + delta, count() - 1));
}

void DirectoryModel::setCurrentToEdge(bool last)
{
    if (m_rows.isEmpty())
        return;
    setCurrentIndex(last ? count() - 1 : 0);
}

QString DirectoryModel::rowPath(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return QString();
    return m_location.child(m_rows.at(row).entry.name).toString();
}

bool DirectoryModel::rowIsDir(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return false;
    return m_rows.at(row).entry.isDir();
}

void DirectoryModel::toggleSelection(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;

    const QString &name = m_rows.at(row).entry.name;
    if (m_selected.contains(name))
        m_selected.remove(name);
    else
        m_selected.insert(name);

    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, { IsSelectedRole });
    emit selectionChanged();
}

void DirectoryModel::selectAll()
{
    if (m_rows.isEmpty())
        return;

    m_selected.clear();
    for (const Row &row : m_rows)
        m_selected.insert(row.entry.name);

    emit dataChanged(index(0), index(count() - 1), { IsSelectedRole });
    emit selectionChanged();
}

void DirectoryModel::clearSelection()
{
    if (m_selected.isEmpty())
        return;

    m_selected.clear();
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(count() - 1), { IsSelectedRole });
    emit selectionChanged();
}

void DirectoryModel::extendSelection(int delta)
{
    if (m_rows.isEmpty())
        return;
    const int from = m_currentIndex < 0 ? 0 : m_currentIndex;
    selectTo(qBound(0, from + delta, count() - 1));
}

// Selects the whole run between the anchor and `row`, which is what both Shift+click and
// Shift+arrow mean. Anything selected outside that run is left alone, so a range can be
// added to an existing Ctrl+click selection.
void DirectoryModel::selectTo(int row)
{
    if (m_rows.isEmpty())
        return;

    const int target = qBound(0, row, count() - 1);
    if (m_anchor < 0 || m_anchor >= count())
        m_anchor = m_currentIndex < 0 ? target : m_currentIndex;

    const int first = qMin(m_anchor, target);
    const int last = qMax(m_anchor, target);
    for (int i = first; i <= last; ++i)
        m_selected.insert(m_rows.at(i).entry.name);

    setCurrent(target, false);
    emit dataChanged(index(first), index(last), { IsSelectedRole });
    emit selectionChanged();
}

void DirectoryModel::selectNames(const QStringList &names)
{
    if (names.isEmpty())
        return;
    if (m_loading) {
        m_pendingSelectNames = names;
        return;
    }
    applySelectNames(names);
}

void DirectoryModel::applySelectNames(const QStringList &names)
{
    if (names.isEmpty() || m_rows.isEmpty())
        return;

    m_selected.clear();
    int firstRow = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!names.contains(m_rows.at(i).entry.name))
            continue;
        m_selected.insert(m_rows.at(i).entry.name);
        if (firstRow < 0)
            firstRow = i;
    }

    if (firstRow >= 0)
        setCurrent(firstRow, true);
    emit dataChanged(index(0), index(count() - 1), { IsSelectedRole });
    emit selectionChanged();
}

QStringList DirectoryModel::actionNames() const
{
    QStringList names;

    if (!m_selected.isEmpty()) {
        // Return them in view order, not hash order, so "Trashed 3 items" reads sensibly
        // and undo unwinds in a predictable sequence.
        for (const Row &row : m_rows) {
            if (m_selected.contains(row.entry.name))
                names.append(row.entry.name);
        }
        return names;
    }

    const QString current = currentName();
    if (!current.isEmpty())
        names.append(current);
    return names;
}

QStringList DirectoryModel::actionPaths() const
{
    QStringList paths;
    const QStringList names = actionNames();
    paths.reserve(names.size());
    for (const QString &name : names)
        paths.append(m_location.child(name).localPath());
    return paths;
}

void DirectoryModel::cycleSort()
{
    switch (m_sortMode) {
    case SortName:
        setSortMode(SortSize);
        break;
    case SortSize:
        setSortMode(SortTime);
        break;
    case SortTime:
        setSortMode(SortName);
        break;
    }
}
