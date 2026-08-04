#pragma once

#include "entry.h"

#include <QList>
#include <QObject>
#include <QStringList>

#include <atomic>

// Enumerates directories off the GUI thread. Lives on DirectoryModel's worker thread;
// every public slot runs there.
//
// Cancellation is a generation counter rather than a flag: the GUI thread bumps it
// (cancelTo, callable from any thread) and every in-flight walk that no longer matches
// abandons its work and drops its results. Nothing is ever merged across generations,
// so navigating away mid-listing cannot leak stale rows into the new directory.
class Lister : public QObject
{
    Q_OBJECT

public:
    explicit Lister(QObject *parent = nullptr);

    // Safe to call from the GUI thread while a walk is running — that is the point.
    void cancelTo(quint64 generation) { m_generation.store(generation, std::memory_order_relaxed); }

public slots:
    // First pass: names and d_type only, no stat at all. Emits batch() repeatedly,
    // then finished(), or failed() if the directory cannot be opened.
    void list(const QString &path, quint64 generation);

    // Second pass: stat the named entries, which the model asks for a visible window
    // at a time. Also resolves entries whose d_type came back DT_UNKNOWN.
    void statRange(const QString &path, quint64 generation, int firstRow, const QStringList &names);

signals:
    void batch(quint64 generation, const QList<Entry> &entries);
    void finished(quint64 generation, int total);
    void failed(quint64 generation, const QString &message);
    void statsReady(quint64 generation, int firstRow, const QList<Entry> &entries);

private:
    bool cancelled(quint64 generation) const
    {
        return m_generation.load(std::memory_order_relaxed) != generation;
    }

    std::atomic<quint64> m_generation { 0 };
};
