#pragma once

#include <QObject>

// §14 calls this the highest-value test in the repo: the scorer is a pure function, and
// ranking quality is the whole difference between search that feels psychic and search
// that feels random.
class TestFuzzyScorer : public QObject
{
    Q_OBJECT

private slots:
    void matchesSubsequences_data();
    void matchesSubsequences();
    void rejectsNonSubsequences_data();
    void rejectsNonSubsequences();

    void reportsMatchPositions();
    void emptyNeedleMatchesEverything();

    void prefixBeatsMidWord();
    void wordBoundaryBeatsScattered();
    void consecutiveRunBeatsGaps();
    void camelCaseCountsAsBoundary();

    void basenameBeatsParentDirectory();
    void shallowPathsRankFirst();
    void ranksPlanExample();

    void smartCaseIgnoresCaseForLowercaseQueries();
    void smartCaseRespectsUppercaseQueries();

    void ordersCandidates_data();
    void ordersCandidates();
};
