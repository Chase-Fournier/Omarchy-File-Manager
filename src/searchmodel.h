#pragma once

#include "searchengine.h"

#include <QAbstractListModel>
#include <QThread>
#include <QTimer>

// The results list for Ctrl+F and Ctrl+Alt+F. Owns the SearchEngine and its thread, and
// is what the ListView switches to while a search is running.
class SearchModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(int mode READ mode NOTIFY activeChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int scanned READ scanned NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

    Q_PROPERTY(bool nameSearchAvailable READ nameSearchAvailable CONSTANT)
    Q_PROPERTY(bool contentSearchAvailable READ contentSearchAvailable CONSTANT)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        DisplayRole,
        MatchPositionsRole,
        LineRole,
        PreviewRole,
        IsDirRole,
    };

    explicit SearchModel(QObject *parent = nullptr);
    ~SearchModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return m_active; }
    int mode() const { return m_mode; }
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int count() const { return int(m_hits.size()); }
    int scanned() const { return m_scanned; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);

    static bool nameSearchAvailable() { return SearchEngine::hasNameSearch(); }
    static bool contentSearchAvailable() { return SearchEngine::hasContentSearch(); }

    // Enter search mode rooted at `root`. Idempotent for the same mode.
    Q_INVOKABLE void begin(const QString &root, int mode);
    Q_INVOKABLE void end();
    Q_INVOKABLE void refreshQuery();
    Q_INVOKABLE QString rowPath(int row) const;
    Q_INVOKABLE bool rowIsDir(int row) const;
    Q_INVOKABLE int rowLine(int row) const;
    Q_INVOKABLE void moveCurrent(int delta);

    // The watcher saw the tree change, so the warm cache is no longer trustworthy.
    Q_INVOKABLE void invalidateCache();

signals:
    void activeChanged();
    void queryChanged();
    void countChanged();
    void busyChanged();
    void errorChanged();
    void currentIndexChanged();

private slots:
    void onResults(quint64 generation, const QList<SearchHit> &hits);
    void onFinished(quint64 generation, int scanned);
    void onFailed(quint64 generation, const QString &message);

private:
    void restart();
    void setBusy(bool busy);
    // §12: nothing about search is built until a search happens. Starting a thread and
    // its engine at construction cost measurable milliseconds off the startup budget for
    // a feature most windows never use.
    void ensureEngine();

    QList<SearchHit> m_hits;
    QString m_root;
    QString m_query;
    int m_mode = SearchEngine::Names;
    bool m_active = false;
    bool m_busy = false;
    int m_scanned = 0;
    int m_currentIndex = -1;
    QString m_error;

    quint64 m_generation = 0;
    // Typing should not spawn a process per keystroke; the warm cache makes the
    // re-ranking free, but the first walk in a directory should still wait for a pause.
    QTimer m_debounce;

    QThread *m_thread = nullptr;
    SearchEngine *m_engine = nullptr;
};
