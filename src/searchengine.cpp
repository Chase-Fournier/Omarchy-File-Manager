#include "searchengine.h"

#include "fuzzyscorer.h"
#include "mounts.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {

// §6: a bounded top-N heap, flushed on a timer so the list fills visibly while the walk
// continues.
constexpr int kMaxResults = 500;
constexpr int kFlushMs = 16;
// ~200k paths is roughly 20 MB of QString, which §6 names as the ceiling worth paying.
constexpr int kCacheCap = 200000;
// How long to wait for output before re-checking cancellation.
constexpr int kPollMs = 40;
// How deep to walk a remote mount when the far end cannot search for itself. Deep enough
// to be useful, shallow enough that a 40 ms round trip per directory stays bearable.
constexpr int kRemoteFallbackDepth = 6;

QString executable(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

QString relativeTo(const QString &root, const QString &path)
{
    if (path.startsWith(root + QLatin1Char('/')))
        return path.mid(root.size() + 1);
    return path;
}

// Keeps the list bounded without sorting on every candidate: prune to the best N only
// once it has grown to twice that.
void prune(QList<SearchHit> &hits)
{
    if (hits.size() < kMaxResults * 2)
        return;
    std::nth_element(hits.begin(), hits.begin() + kMaxResults, hits.end(),
                     [](const SearchHit &a, const SearchHit &b) { return a.score > b.score; });
    hits.resize(kMaxResults);
}

QList<SearchHit> best(QList<SearchHit> hits)
{
    std::stable_sort(hits.begin(), hits.end(),
                     [](const SearchHit &a, const SearchHit &b) { return a.score > b.score; });
    if (hits.size() > kMaxResults)
        hits.resize(kMaxResults);
    return hits;
}

} // namespace

SearchEngine::SearchEngine(QObject *parent)
    : QObject(parent)
{
}

bool SearchEngine::hasNameSearch()
{
    return !executable(QStringLiteral("fd")).isEmpty();
}

bool SearchEngine::hasContentSearch()
{
    return !executable(QStringLiteral("rg")).isEmpty();
}

bool SearchEngine::hasLocate()
{
    return !executable(QStringLiteral("plocate")).isEmpty();
}

void SearchEngine::invalidateCache()
{
    m_cacheRoot.clear();
    m_cache.clear();
    m_cacheComplete = false;
}

void SearchEngine::search(const QString &root, const QString &query, int mode,
                          quint64 generation)
{
    if (cancelled(generation))
        return;

    if (mode == Content) {
        searchContent(root, query, generation);
        return;
    }

    // §6: a query starting with '/' means the whole filesystem, which plocate answers
    // without walking anything.
    if (query.startsWith(QLatin1Char('/')) && hasLocate()) {
        searchLocate(query, generation);
        return;
    }

    if (m_cacheComplete && m_cacheRoot == root) {
        rankCached(root, query, generation);
        return;
    }

    searchNames(root, query, generation);
}

void SearchEngine::rankCached(const QString &root, const QString &query, quint64 generation)
{
    QList<SearchHit> hits;
    hits.reserve(qMin(m_cache.size(), kMaxResults * 2));

    for (const QString &path : std::as_const(m_cache)) {
        if (cancelled(generation))
            return;

        const QString display = relativeTo(root, path);
        const FuzzyScorer::Result match = FuzzyScorer::scorePath(query, display);
        if (!match.matched)
            continue;

        SearchHit hit;
        hit.path = path;
        hit.display = display;
        hit.score = match.score;
        hit.positions = match.positions;
        hits.append(std::move(hit));
        prune(hits);
    }

    emit results(generation, best(std::move(hits)));
    emit finished(generation, int(m_cache.size()));
}

void SearchEngine::searchNames(const QString &root, const QString &query, quint64 generation,
                               bool allowRemote, int depthLimit)
{
    const QString program = executable(QStringLiteral("fd"));
    if (program.isEmpty()) {
        emit failed(generation, QStringLiteral("fd is not installed"));
        return;
    }

    // --print0 rather than newlines: §14 is explicit that a filename containing a newline
    // is the classic breakage for anything that shells out, and this is the only place
    // omafile does.
    const QStringList fdArguments = { QStringLiteral("--hidden"),
                                      QStringLiteral("--no-ignore-vcs"),
                                      QStringLiteral("--color"), QStringLiteral("never"),
                                      QStringLiteral("--absolute-path"),
                                      QStringLiteral("--print0"), QStringLiteral(".") };

    // §10.1: walking an sshfs mount is agonising, so when omafile owns the mount the walk
    // runs on the far end and the returned paths are rewritten back into local mount
    // paths. A 30-second remote search becomes a 300 ms one.
    const QString sshHost = allowRemote ? Mounts::sshHostFor(root) : QString();
    QString remotePrefix;
    QString localPrefix;

    QProcess process;
    if (!sshHost.isEmpty()) {
        const QString mountRoot = Mounts::runtimeMountRoot() + QLatin1Char('/') + sshHost;
        remotePrefix = root.mid(mountRoot.size());
        if (remotePrefix.isEmpty())
            remotePrefix = QStringLiteral("/");
        localPrefix = mountRoot;

        process.setProgram(QStringLiteral("ssh"));
        process.setArguments({ QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                               sshHost,
                               QStringLiteral("fd %1 -- %2")
                                   .arg(fdArguments.join(QLatin1Char(' ')),
                                        QStringLiteral("'%1'").arg(remotePrefix)) });
    } else {
        QStringList arguments = fdArguments;
        // §10.6: never silently walk a slow mount to completion. The fallback is bounded.
        if (depthLimit > 0)
            arguments << QStringLiteral("--max-depth") << QString::number(depthLimit);
        process.setProgram(program);
        process.setArguments(arguments << root);
    }
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start();

    if (!process.waitForStarted(3000)) {
        emit failed(generation, QStringLiteral("could not run fd"));
        return;
    }

    QByteArray pending;
    QList<SearchHit> hits;
    QStringList cache;
    const bool remote = !sshHost.isEmpty();
    int scanned = 0;
    QElapsedTimer flush;
    flush.start();

    const auto consume = [&](const QByteArray &raw) {
        QString path = QFile::decodeName(raw);
        if (path.isEmpty())
            return;
        // The far end reported its own absolute paths; map them onto the mount so that
        // everything downstream — opening, copying, dragging — sees an ordinary path.
        if (remote) {
            if (remotePrefix != QLatin1String("/") && path.startsWith(remotePrefix))
                path = localPrefix + path;
            else if (remotePrefix == QLatin1String("/"))
                path = localPrefix + path;
            else
                return;
        }
        ++scanned;
        if (cache.size() < kCacheCap)
            cache.append(path);

        const QString display = relativeTo(root, path);
        const FuzzyScorer::Result match = FuzzyScorer::scorePath(query, display);
        if (!match.matched)
            return;

        SearchHit hit;
        hit.path = path;
        hit.display = display;
        hit.score = match.score;
        hit.positions = match.positions;
        hits.append(std::move(hit));
        prune(hits);
    };

    forever {
        if (cancelled(generation)) {
            process.kill();
            process.waitForFinished(1000);
            return;
        }

        const bool ready = process.waitForReadyRead(kPollMs);
        if (ready)
            pending += process.readAllStandardOutput();

        int start = 0;
        int nul;
        while ((nul = pending.indexOf('\0', start)) >= 0) {
            consume(pending.mid(start, nul - start));
            start = nul + 1;
        }
        if (start > 0)
            pending.remove(0, start);

        if (flush.elapsed() >= kFlushMs && !hits.isEmpty()) {
            emit results(generation, best(hits));
            flush.restart();
        }

        if (!ready && process.state() == QProcess::NotRunning) {
            // Drain whatever arrived between the last read and the child exiting.
            pending += process.readAllStandardOutput();
            start = 0;
            while ((nul = pending.indexOf('\0', start)) >= 0) {
                consume(pending.mid(start, nul - start));
                start = nul + 1;
            }
            break;
        }
    }

    if (cancelled(generation))
        return;

    // The far end may not have fd at all — a perfectly ordinary server. Rather than
    // reporting nothing, walk the mount locally instead, bounded so a slow link cannot
    // turn a search into a hang (§10.6).
    if (remote && scanned == 0 && process.exitCode() != 0) {
        searchNames(root, query, generation, false, kRemoteFallbackDepth);
        return;
    }

    // Only a complete walk is worth caching; a cancelled one would answer later queries
    // with a truncated tree.
    m_cacheRoot = root;
    m_cache = std::move(cache);
    // A depth-limited walk did not see the whole tree, so it must not be reused as if
    // it had.
    m_cacheComplete = m_cache.size() < kCacheCap && depthLimit == 0;

    emit results(generation, best(std::move(hits)));
    emit finished(generation, scanned);
}

void SearchEngine::searchLocate(const QString &query, quint64 generation)
{
    QProcess process;
    process.setProgram(executable(QStringLiteral("plocate")));
    process.setArguments({ QStringLiteral("--limit"), QString::number(kMaxResults * 4),
                           query });
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start();

    if (!process.waitForStarted(3000) || !process.waitForFinished(5000)) {
        emit failed(generation, QStringLiteral("plocate failed"));
        return;
    }
    if (cancelled(generation))
        return;

    QList<SearchHit> hits;
    const QList<QByteArray> lines = process.readAllStandardOutput().split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty())
            continue;
        const QString path = QFile::decodeName(line);
        const FuzzyScorer::Result match = FuzzyScorer::scorePath(query, path);

        SearchHit hit;
        hit.path = path;
        hit.display = path;
        hit.score = match.matched ? match.score : 0;
        hit.positions = match.positions;
        hits.append(std::move(hit));
    }

    emit results(generation, best(std::move(hits)));
    emit finished(generation, int(lines.size()));
}

void SearchEngine::searchContent(const QString &root, const QString &query, quint64 generation)
{
    const QString program = executable(QStringLiteral("rg"));
    if (program.isEmpty()) {
        emit failed(generation, QStringLiteral("ripgrep is not installed"));
        return;
    }
    if (query.isEmpty()) {
        emit results(generation, {});
        emit finished(generation, 0);
        return;
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments({ QStringLiteral("--json"), QStringLiteral("--line-number"),
                           QStringLiteral("--max-count"), QStringLiteral("5"),
                           QStringLiteral("--smart-case"), QStringLiteral("--"), query,
                           root });
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start();

    if (!process.waitForStarted(3000)) {
        emit failed(generation, QStringLiteral("could not run ripgrep"));
        return;
    }

    QByteArray pending;
    QList<SearchHit> hits;
    int scanned = 0;
    QElapsedTimer flush;
    flush.start();

    // rg --json emits one JSON object per line; only "match" events carry a hit.
    const auto consume = [&](const QByteArray &line) {
        if (line.isEmpty())
            return;
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (event.value(QStringLiteral("type")).toString() != QLatin1String("match"))
            return;

        const QJsonObject data = event.value(QStringLiteral("data")).toObject();
        const QString path = data.value(QStringLiteral("path")).toObject()
                                 .value(QStringLiteral("text")).toString();
        if (path.isEmpty())
            return; // binary or non-UTF-8 path; rg reports those as base64 instead

        ++scanned;
        SearchHit hit;
        hit.path = path;
        hit.display = relativeTo(root, path);
        hit.line = data.value(QStringLiteral("line_number")).toInt();
        hit.preview = data.value(QStringLiteral("lines")).toObject()
                          .value(QStringLiteral("text")).toString().trimmed();
        // Content hits are ranked by where they are, since the match itself is exact.
        hit.score = -hit.display.count(QLatin1Char('/'));
        hits.append(std::move(hit));
        prune(hits);
    };

    forever {
        if (cancelled(generation)) {
            process.kill();
            process.waitForFinished(1000);
            return;
        }

        const bool ready = process.waitForReadyRead(kPollMs);
        if (ready)
            pending += process.readAllStandardOutput();

        int start = 0;
        int newline;
        while ((newline = pending.indexOf('\n', start)) >= 0) {
            consume(pending.mid(start, newline - start));
            start = newline + 1;
        }
        if (start > 0)
            pending.remove(0, start);

        if (flush.elapsed() >= kFlushMs && !hits.isEmpty()) {
            emit results(generation, best(hits));
            flush.restart();
        }

        if (!ready && process.state() == QProcess::NotRunning) {
            pending += process.readAllStandardOutput();
            start = 0;
            while ((newline = pending.indexOf('\n', start)) >= 0) {
                consume(pending.mid(start, newline - start));
                start = newline + 1;
            }
            break;
        }
    }

    if (cancelled(generation))
        return;

    emit results(generation, best(std::move(hits)));
    emit finished(generation, scanned);
}
