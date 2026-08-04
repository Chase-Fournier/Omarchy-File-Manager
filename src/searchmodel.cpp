#include "searchmodel.h"

#include <QFileInfo>

SearchModel::SearchModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Short enough to feel immediate, long enough that a fast typist does not start a
    // walk per character. Re-ranking from the warm cache goes through the same path and
    // is effectively free.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(60);
    connect(&m_debounce, &QTimer::timeout, this, &SearchModel::restart);
}

SearchModel::~SearchModel()
{
    if (!m_engine)
        return;
    m_engine->cancelTo(m_generation + 1);
    m_thread->quit();
    m_thread->wait();
}

void SearchModel::ensureEngine()
{
    if (m_engine)
        return;

    qRegisterMetaType<QList<SearchHit>>("QList<SearchHit>");

    m_thread = new QThread(this);
    m_engine = new SearchEngine;
    m_engine->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_engine, &QObject::deleteLater);

    connect(m_engine, &SearchEngine::results, this, &SearchModel::onResults);
    connect(m_engine, &SearchEngine::finished, this, &SearchModel::onFinished);
    connect(m_engine, &SearchEngine::failed, this, &SearchModel::onFailed);

    m_thread->start();
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_hits.size());
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {
        { PathRole, "path" },
        { DisplayRole, "display" },
        { MatchPositionsRole, "matchPositions" },
        { LineRole, "line" },
        { PreviewRole, "preview" },
        { IsDirRole, "isDir" },
    };
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_hits.size())
        return {};

    const SearchHit &hit = m_hits.at(index.row());
    switch (role) {
    case PathRole:
        return hit.path;
    case DisplayRole:
        return hit.display;
    case MatchPositionsRole: {
        QVariantList positions;
        positions.reserve(hit.positions.size());
        for (int position : hit.positions)
            positions.append(position);
        return positions;
    }
    case LineRole:
        return hit.line;
    case PreviewRole:
        return hit.preview;
    case IsDirRole:
        return hit.isDir;
    default:
        return {};
    }
}

void SearchModel::begin(const QString &root, int mode)
{
    ensureEngine();
    m_root = root;
    m_mode = mode;
    m_query.clear();
    emit queryChanged();

    if (!m_active) {
        m_active = true;
        emit activeChanged();
    }

    beginResetModel();
    m_hits.clear();
    endResetModel();
    m_scanned = 0;
    emit countChanged();
    setCurrentIndex(-1);

    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }

    // A name search with no query is a useful thing to show — it is the whole tree, which
    // is what makes Ctrl+F then arrow keys work. A content search with no query is not.
    if (m_mode == SearchEngine::Names)
        restart();
}

void SearchModel::end()
{
    // Bump the generation first so anything in flight is dropped rather than delivered
    // into a closed search.
    if (m_engine)
        m_engine->cancelTo(++m_generation);
    m_debounce.stop();

    if (m_active) {
        m_active = false;
        emit activeChanged();
    }
    setBusy(false);

    beginResetModel();
    m_hits.clear();
    endResetModel();
    emit countChanged();
    setCurrentIndex(-1);
}

void SearchModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();

    if (m_active)
        m_debounce.start();
}

void SearchModel::refreshQuery()
{
    if (m_active)
        restart();
}

void SearchModel::invalidateCache()
{
    // Deliberately does not build the engine: this fires on every directory change, and
    // there is nothing to invalidate until a search has actually run.
    if (m_engine)
        QMetaObject::invokeMethod(m_engine, "invalidateCache", Qt::QueuedConnection);
}

void SearchModel::restart()
{
    if (!m_active)
        return;
    ensureEngine();

    // Every keystroke cancels the previous walk outright; §6 is explicit that results are
    // dropped rather than merged.
    m_engine->cancelTo(++m_generation);
    setBusy(true);

    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }

    QMetaObject::invokeMethod(m_engine, "search", Qt::QueuedConnection,
                              Q_ARG(QString, m_root), Q_ARG(QString, m_query),
                              Q_ARG(int, m_mode), Q_ARG(quint64, m_generation));
}

void SearchModel::onResults(quint64 generation, const QList<SearchHit> &hits)
{
    if (generation != m_generation)
        return;

    // Ranking is global, so a batch replaces rather than appends: a path found late in
    // the walk can outrank everything already on screen.
    const QString keepPath = rowPath(m_currentIndex);

    beginResetModel();
    m_hits = hits;
    endResetModel();
    emit countChanged();

    int restored = -1;
    if (!keepPath.isEmpty()) {
        for (int i = 0; i < m_hits.size(); ++i) {
            if (m_hits.at(i).path == keepPath) {
                restored = i;
                break;
            }
        }
    }
    setCurrentIndex(restored >= 0 ? restored : (m_hits.isEmpty() ? -1 : 0));
}

void SearchModel::onFinished(quint64 generation, int scanned)
{
    if (generation != m_generation)
        return;
    m_scanned = scanned;
    emit countChanged();
    setBusy(false);
}

void SearchModel::onFailed(quint64 generation, const QString &message)
{
    if (generation != m_generation)
        return;
    m_error = message;
    emit errorChanged();
    setBusy(false);
}

void SearchModel::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void SearchModel::setCurrentIndex(int index)
{
    const int clamped = m_hits.isEmpty() ? -1 : qBound(0, index, count() - 1);
    if (clamped == m_currentIndex)
        return;
    m_currentIndex = clamped;
    emit currentIndexChanged();
}

void SearchModel::moveCurrent(int delta)
{
    if (m_hits.isEmpty())
        return;
    setCurrentIndex((m_currentIndex < 0 ? 0 : m_currentIndex) + delta);
}

QString SearchModel::rowPath(int row) const
{
    if (row < 0 || row >= m_hits.size())
        return QString();
    return m_hits.at(row).path;
}

bool SearchModel::rowIsDir(int row) const
{
    if (row < 0 || row >= m_hits.size())
        return false;
    // Resolved lazily: the walk deliberately does not stat, and only the row the user
    // actually activates needs to know.
    return QFileInfo(m_hits.at(row).path).isDir();
}

int SearchModel::rowLine(int row) const
{
    if (row < 0 || row >= m_hits.size())
        return 0;
    return m_hits.at(row).line;
}
