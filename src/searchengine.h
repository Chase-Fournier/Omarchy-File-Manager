#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>

// One result row. `display` is the path relative to the search root, which is both what
// the list shows and what the match positions index into.
struct SearchHit
{
    QString path;
    QString display;
    int score = 0;
    QList<int> positions;
    bool isDir = false;

    // Content search only.
    int line = 0;
    QString preview;
};

Q_DECLARE_METATYPE(SearchHit)

// Tiers 2 and 3 of §6: recursive name search over `fd`, and content search over
// ripgrep. Lives on its own thread and blocks it freely — the point of the thread is
// that the walk can take 400 ms without the UI noticing.
//
// Cancellation is the same generation counter the Lister uses: the GUI bumps it, the
// read loop notices within one poll interval, kills the child and drops its results.
// Nothing is ever merged across generations.
class SearchEngine : public QObject
{
    Q_OBJECT

public:
    enum Mode { Names, Content };
    Q_ENUM(Mode)

    explicit SearchEngine(QObject *parent = nullptr);

    // Safe from any thread, and the only way to stop a walk in flight.
    void cancelTo(quint64 generation)
    {
        m_generation.store(generation, std::memory_order_relaxed);
    }

    // Soft dependencies, detected once (§2). The UI hides what is not installed rather
    // than failing when it is used.
    static bool hasNameSearch();
    static bool hasContentSearch();
    static bool hasLocate();

public slots:
    void search(const QString &root, const QString &query, int mode, quint64 generation);
    // Dropped when the watcher sees the directory change, so a warm cache cannot go stale.
    void invalidateCache();

signals:
    // The current best results, replacing anything sent earlier for this generation:
    // ranking is global, so a late arrival can outrank everything already shown.
    void results(quint64 generation, const QList<SearchHit> &hits);
    void finished(quint64 generation, int scanned);
    void failed(quint64 generation, const QString &message);

private:
    bool cancelled(quint64 generation) const
    {
        return m_generation.load(std::memory_order_relaxed) != generation;
    }

    void searchNames(const QString &root, const QString &query, quint64 generation);
    void searchContent(const QString &root, const QString &query, quint64 generation);
    void searchLocate(const QString &query, quint64 generation);

    // Ranks an already-known path list with no I/O at all — the warm-cache path, and the
    // reason a second search in the same directory feels instant (§6).
    void rankCached(const QString &root, const QString &query, quint64 generation);

    std::atomic<quint64> m_generation { 0 };

    // The warm cache: every path under one root, capped so a pathological tree cannot
    // eat the process.
    QString m_cacheRoot;
    QStringList m_cache;
    bool m_cacheComplete = false;
};
