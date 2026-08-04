#include "tst_fuzzyscorer.h"

#include "fuzzyscorer.h"

#include <QTest>

using FuzzyScorer::Result;

namespace {

int pathScore(const QString &needle, const QString &path)
{
    return FuzzyScorer::scorePath(needle, path).score;
}

// Ranks candidates the way the search list would, best first.
QStringList ranked(const QString &needle, const QStringList &candidates)
{
    QList<QPair<int, QString>> scored;
    for (const QString &candidate : candidates) {
        const Result result = FuzzyScorer::scorePath(needle, candidate);
        if (result.matched)
            scored.append({ result.score, candidate });
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });

    QStringList out;
    for (const auto &pair : scored)
        out.append(pair.second);
    return out;
}

} // namespace

void TestFuzzyScorer::matchesSubsequences_data()
{
    QTest::addColumn<QString>("needle");
    QTest::addColumn<QString>("haystack");

    QTest::newRow("exact") << "readme" << "readme";
    QTest::newRow("prefix") << "read" << "readme.md";
    QTest::newRow("scattered") << "rme" << "readme";
    QTest::newRow("across separators") << "omf" << "oma/file";
    QTest::newRow("single char") << "z" << "zebra";
    QTest::newRow("extension") << "md" << "readme.md";
}

void TestFuzzyScorer::matchesSubsequences()
{
    QFETCH(QString, needle);
    QFETCH(QString, haystack);
    QVERIFY(FuzzyScorer::score(needle, haystack).matched);
}

void TestFuzzyScorer::rejectsNonSubsequences_data()
{
    QTest::addColumn<QString>("needle");
    QTest::addColumn<QString>("haystack");

    QTest::newRow("missing letter") << "readmex" << "readme";
    QTest::newRow("wrong order") << "emdaer" << "readme";
    QTest::newRow("longer than haystack") << "readme.md" << "readme";
    QTest::newRow("empty haystack") << "a" << "";
    // A repeated character needs to appear that many times.
    QTest::newRow("not enough repeats") << "aaa" << "banal";
}

void TestFuzzyScorer::rejectsNonSubsequences()
{
    QFETCH(QString, needle);
    QFETCH(QString, haystack);
    QVERIFY(!FuzzyScorer::score(needle, haystack).matched);
}

void TestFuzzyScorer::reportsMatchPositions()
{
    const Result result = FuzzyScorer::score(QStringLiteral("rdm"),
                                             QStringLiteral("readme.md"));
    QVERIFY(result.matched);
    // r-e-a-d-m-e -> r(0) d(3) m(4); the UI bolds exactly these.
    QCOMPARE(result.positions, QList<int>({ 0, 3, 4 }));

    // Positions are absolute in the path, not relative to the basename.
    const Result inPath = FuzzyScorer::scorePath(QStringLiteral("ma"),
                                                 QStringLiteral("src/main.cpp"));
    QVERIFY(inPath.matched);
    QCOMPARE(inPath.positions, QList<int>({ 4, 5 }));
}

void TestFuzzyScorer::emptyNeedleMatchesEverything()
{
    QVERIFY(FuzzyScorer::score(QString(), QStringLiteral("anything")).matched);
    QVERIFY(FuzzyScorer::scorePath(QString(), QStringLiteral("a/b/c")).matched);
    QVERIFY(FuzzyScorer::score(QString(), QStringLiteral("anything")).positions.isEmpty());
}

void TestFuzzyScorer::prefixBeatsMidWord()
{
    QVERIFY(pathScore("read", "readme.md") > pathScore("read", "unreadable.md"));
}

void TestFuzzyScorer::wordBoundaryBeatsScattered()
{
    // "fb" as two word starts beats the same letters buried inside one word.
    QVERIFY(pathScore("fb", "foo-bar.txt") > pathScore("fb", "fubar.txt"));
}

void TestFuzzyScorer::consecutiveRunBeatsGaps()
{
    QVERIFY(pathScore("abc", "abcdef.txt") > pathScore("abc", "axbxcx.txt"));
}

void TestFuzzyScorer::camelCaseCountsAsBoundary()
{
    QVERIFY(pathScore("dm", "DirectoryModel.cpp") > pathScore("dm", "downmix.cpp"));
}

// §6: "strong bonus for matching the basename rather than a parent directory".
void TestFuzzyScorer::basenameBeatsParentDirectory()
{
    QVERIFY(pathScore("src", "lib/src.txt") > pathScore("src", "src/lib/other.txt"));
    QVERIFY(pathScore("main", "a/b/c/main.cpp") > pathScore("main", "main/b/c/other.cpp"));
}

void TestFuzzyScorer::shallowPathsRankFirst()
{
    // Same basename match quality, so depth is the only thing separating them.
    QVERIFY(pathScore("x", "x.txt") > pathScore("x", "a/b/c/d/x.txt"));
    // With no basename match at all, depth still breaks the tie.
    QVERIFY(pathScore("ab", "a/b.txt") > pathScore("ab", "a/x/y/z/b.txt"));
}

// The example §14 names explicitly.
void TestFuzzyScorer::ranksPlanExample()
{
    const QStringList candidates = { QStringLiteral("some/other/file"),
                                     QStringLiteral("omafile.pro") };
    QCOMPARE(ranked(QStringLiteral("omf"), candidates).first(),
             QStringLiteral("omafile.pro"));
}

void TestFuzzyScorer::smartCaseIgnoresCaseForLowercaseQueries()
{
    QVERIFY(FuzzyScorer::score(QStringLiteral("readme"), QStringLiteral("README")).matched);
    QVERIFY(FuzzyScorer::score(QStringLiteral("rm"), QStringLiteral("ReadMe")).matched);
}

void TestFuzzyScorer::smartCaseRespectsUppercaseQueries()
{
    // An uppercase character in the query means the user meant it.
    QVERIFY(FuzzyScorer::score(QStringLiteral("README"), QStringLiteral("README.md")).matched);
    QVERIFY(!FuzzyScorer::score(QStringLiteral("README"), QStringLiteral("readme.md")).matched);
    QVERIFY(!FuzzyScorer::score(QStringLiteral("RM"), QStringLiteral("readme")).matched);
    // Mixed case is still case-sensitive throughout.
    QVERIFY(FuzzyScorer::score(QStringLiteral("Rm"), QStringLiteral("ReadmeFile")).matched);
}

void TestFuzzyScorer::ordersCandidates_data()
{
    QTest::addColumn<QString>("needle");
    QTest::addColumn<QStringList>("candidates");
    QTest::addColumn<QString>("expectedFirst");

    QTest::newRow("basename over path")
        << "omf" << QStringList({ "some/other/file", "omafile.pro" }) << "omafile.pro";
    QTest::newRow("exact name wins")
        << "main.cpp" << QStringList({ "src/main.cpp.bak", "src/main.cpp" }) << "src/main.cpp";
    QTest::newRow("prefix over infix")
        << "test" << QStringList({ "src/contest.cpp", "tests/test.cpp" }) << "tests/test.cpp";
    QTest::newRow("shallow over deep")
        << "notes" << QStringList({ "a/b/c/d/notes.md", "notes.md" }) << "notes.md";
    QTest::newRow("word boundary over buried")
        << "tm" << QStringList({ "atomic.h", "theme-manager.h" }) << "theme-manager.h";
}

void TestFuzzyScorer::ordersCandidates()
{
    QFETCH(QString, needle);
    QFETCH(QStringList, candidates);
    QFETCH(QString, expectedFirst);

    const QStringList order = ranked(needle, candidates);
    QVERIFY2(!order.isEmpty(), "nothing matched at all");
    QCOMPARE(order.first(), expectedFirst);
}
