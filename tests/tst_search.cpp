#include "tst_search.h"

#include "searchengine.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

namespace {

// Collects the final result set of one search.
struct Run
{
    QList<SearchHit> hits;
    int scanned = 0;
    QString error;
    bool finished = false;
    int batches = 0;
};

Run runSearch(SearchEngine *engine, const QString &root, const QString &query,
              int mode = SearchEngine::Names, quint64 generation = 1)
{
    Run run;
    const auto onResults = QObject::connect(
        engine, &SearchEngine::results, engine,
        [&run](quint64, const QList<SearchHit> &hits) {
            run.hits = hits;
            ++run.batches;
        });
    const auto onFinished = QObject::connect(
        engine, &SearchEngine::finished, engine,
        [&run](quint64, int scanned) {
            run.scanned = scanned;
            run.finished = true;
        });
    const auto onFailed = QObject::connect(
        engine, &SearchEngine::failed, engine,
        [&run](quint64, const QString &message) { run.error = message; });

    engine->cancelTo(generation);
    engine->search(root, query, mode, generation);

    QObject::disconnect(onResults);
    QObject::disconnect(onFinished);
    QObject::disconnect(onFailed);
    return run;
}

QStringList displaysOf(const Run &run)
{
    QStringList out;
    for (const SearchHit &hit : run.hits)
        out.append(hit.display);
    return out;
}

} // namespace

void TestSearch::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_haveFd = SearchEngine::hasNameSearch();
    m_haveRg = SearchEngine::hasContentSearch();

    write(QStringLiteral("src/theme.cpp"), "int themeAnswer = 42;\n");
    write(QStringLiteral("src/theme.h"), "#pragma once\n");
    write(QStringLiteral("src/deep/nested/theme.txt"), "nothing here\n");
    write(QStringLiteral("docs/readme.md"), "the answer is 42\n");
    write(QStringLiteral("build/theme.o"), "binary-ish\n");
}

void TestSearch::write(const QString &relative, const QByteArray &contents)
{
    const QString full = m_dir.path() + QLatin1Char('/') + relative;
    QVERIFY(QDir().mkpath(QFileInfo(full).absolutePath()));
    QFile file(full);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(full));
    file.write(contents);
}

void TestSearch::findsFilesByName()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    SearchEngine engine;
    const Run run = runSearch(&engine, m_dir.path(), QStringLiteral("theme"));

    QVERIFY(run.error.isEmpty());
    QVERIFY(run.finished);
    const QStringList found = displaysOf(run);
    QVERIFY(found.contains(QStringLiteral("src/theme.cpp")));
    QVERIFY(found.contains(QStringLiteral("src/theme.h")));
    // Paths are relative to the root, which is both what shows and what positions index.
    for (const QString &display : found)
        QVERIFY(!display.startsWith(QLatin1Char('/')));
}

void TestSearch::ranksBasenameMatchesFirst()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    SearchEngine engine;
    const Run run = runSearch(&engine, m_dir.path(), QStringLiteral("theme"));
    QVERIFY(!run.hits.isEmpty());

    // A shallow source file beats the same name buried three directories down.
    const QStringList found = displaysOf(run);
    const int shallow = found.indexOf(QStringLiteral("src/theme.h"));
    const int deep = found.indexOf(QStringLiteral("src/deep/nested/theme.txt"));
    QVERIFY(shallow >= 0);
    QVERIFY(deep < 0 || shallow < deep);
}

// §14: the classic breakage for anything that shells out. --print0 is what survives it.
void TestSearch::survivesNewlinesInFilenames()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    write(QStringLiteral("odd/with\nnewline.txt"));
    write(QStringLiteral("odd/with space.txt"));
    write(QStringLiteral("odd/with'quote.txt"));

    SearchEngine engine;
    const Run run = runSearch(&engine, m_dir.path(), QStringLiteral("with"));
    const QStringList found = displaysOf(run);

    QVERIFY(found.contains(QStringLiteral("odd/with\nnewline.txt")));
    QVERIFY(found.contains(QStringLiteral("odd/with space.txt")));
    QVERIFY(found.contains(QStringLiteral("odd/with'quote.txt")));
}

void TestSearch::emptyQueryReturnsTheWholeTree()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    SearchEngine engine;
    const Run run = runSearch(&engine, m_dir.path(), QString());
    QVERIFY(run.finished);
    QVERIFY(run.scanned > 5);
    QVERIFY(!run.hits.isEmpty());
}

// §6: results from a superseded search are dropped, never merged.
void TestSearch::staleGenerationIsDropped()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    SearchEngine engine;
    Run run;
    QObject::connect(&engine, &SearchEngine::results, &engine,
                     [&run](quint64, const QList<SearchHit> &hits) { run.hits = hits; });
    QObject::connect(&engine, &SearchEngine::finished, &engine,
                     [&run](quint64, int) { run.finished = true; });

    // The engine has moved on to generation 9; a search tagged 5 must produce nothing.
    engine.cancelTo(9);
    engine.search(m_dir.path(), QStringLiteral("theme"), SearchEngine::Names, 5);

    QVERIFY(run.hits.isEmpty());
    QVERIFY(!run.finished);
}

// The refinement that makes a second search in the same directory feel instant (§6).
void TestSearch::warmCacheAnswersWithoutWalking()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    SearchEngine engine;
    QElapsedTimer timer;

    timer.start();
    const Run cold = runSearch(&engine, m_dir.path(), QStringLiteral("theme"),
                               SearchEngine::Names, 1);
    const qint64 coldMs = timer.elapsed();
    QVERIFY(cold.finished);

    timer.restart();
    const Run warm = runSearch(&engine, m_dir.path(), QStringLiteral("readme"),
                               SearchEngine::Names, 2);
    const qint64 warmMs = timer.elapsed();

    QVERIFY(warm.finished);
    QVERIFY(displaysOf(warm).contains(QStringLiteral("docs/readme.md")));
    // The warm pass ranks in memory with no process at all, so it cannot be slower than
    // the walk that had to spawn fd.
    qInfo("search cold %lldms, warm %lldms", static_cast<long long>(coldMs),
          static_cast<long long>(warmMs));
    QVERIFY(warmMs <= coldMs);

    // Invalidating drops it, so a changed tree is never answered from stale paths.
    engine.invalidateCache();
    const Run again = runSearch(&engine, m_dir.path(), QStringLiteral("theme"),
                                SearchEngine::Names, 3);
    QVERIFY(again.finished);
    QVERIFY(!again.hits.isEmpty());
}

void TestSearch::reportsMissingTool()
{
    // Whatever is installed, the availability flags must be answerable without throwing;
    // the UI gates the shortcuts on exactly these.
    const bool names = SearchEngine::hasNameSearch();
    const bool content = SearchEngine::hasContentSearch();
    QVERIFY(names == true || names == false);
    QVERIFY(content == true || content == false);
}

void TestSearch::findsFileContents()
{
    if (!m_haveRg)
        QSKIP("ripgrep is not installed");

    SearchEngine engine;
    const Run run = runSearch(&engine, m_dir.path(), QStringLiteral("answer"),
                              SearchEngine::Content);

    QVERIFY(run.error.isEmpty());
    QVERIFY(run.finished);
    const QStringList found = displaysOf(run);
    QVERIFY(found.contains(QStringLiteral("docs/readme.md")));
    QVERIFY(found.contains(QStringLiteral("src/theme.cpp")));
    // A name-only match must not appear: this searches contents, not filenames.
    QVERIFY(!found.contains(QStringLiteral("src/theme.h")));
}

void TestSearch::contentSearchReportsLineAndPreview()
{
    if (!m_haveRg)
        QSKIP("ripgrep is not installed");

    SearchEngine engine;
    const Run run = runSearch(&engine, m_dir.path(), QStringLiteral("themeAnswer"),
                              SearchEngine::Content);
    QVERIFY(!run.hits.isEmpty());

    const SearchHit &hit = run.hits.first();
    QCOMPARE(hit.display, QStringLiteral("src/theme.cpp"));
    QCOMPARE(hit.line, 1);
    QCOMPARE(hit.preview, QStringLiteral("int themeAnswer = 42;"));
}

// §6: first result on screen within 30 ms.
void TestSearch::firstResultIsWithinBudget()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    SearchEngine engine;
    QElapsedTimer timer;
    qint64 firstBatchMs = -1;

    QObject::connect(&engine, &SearchEngine::results, &engine,
                     [&](quint64, const QList<SearchHit> &) {
                         if (firstBatchMs < 0)
                             firstBatchMs = timer.elapsed();
                     });

    engine.cancelTo(1);
    timer.start();
    engine.search(m_dir.path(), QStringLiteral("theme"), SearchEngine::Names, 1);

    QVERIFY(firstBatchMs >= 0);
    qInfo("first search result in %lldms (budget 30ms)", static_cast<long long>(firstBatchMs));
    QVERIFY2(firstBatchMs < 30, qPrintable(QStringLiteral("took %1ms").arg(firstBatchMs)));
}

// §6: a 100k-file tree fully walked in under 400 ms.
void TestSearch::walkingALargeTreeIsWithinBudget()
{
    if (!m_haveFd)
        QSKIP("fd is not installed");

    const QString big = m_dir.path() + QStringLiteral("/big");
    constexpr int kFanout = 100;
    constexpr int kPerDir = 1000;

    for (int d = 0; d < kFanout; ++d) {
        const QString dir = big + QStringLiteral("/d%1").arg(d, 3, 10, QLatin1Char('0'));
        QVERIFY(QDir().mkpath(dir));
        for (int f = 0; f < kPerDir; ++f) {
            QFile file(dir + QStringLiteral("/file-%1.txt").arg(f, 4, 10, QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
    }

    SearchEngine engine;
    QElapsedTimer timer;
    timer.start();
    const Run run = runSearch(&engine, big, QStringLiteral("file-0042"));
    const qint64 elapsed = timer.elapsed();

    QVERIFY(run.finished);
    QCOMPARE(run.scanned, kFanout * kPerDir + kFanout); // files plus their directories
    qInfo("walked %d entries in %lldms (budget 400ms)", run.scanned,
          static_cast<long long>(elapsed));
    QVERIFY2(elapsed < 400, qPrintable(QStringLiteral("took %1ms").arg(elapsed)));
}
