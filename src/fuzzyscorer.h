#pragma once

#include <QList>
#include <QString>

// A small fzf-style matcher (§6). Pure functions, no state, no fzf dependency — which is
// what makes it the most testable and most test-worthy thing in the repo.
//
// The contract is (needle, haystack) -> (score, positions). Positions are indices into
// the haystack so the UI can bold exactly the characters that matched.
namespace FuzzyScorer {

struct Result
{
    bool matched = false;
    int score = 0;
    QList<int> positions;

    bool operator<(const Result &other) const { return score < other.score; }
};

// Matches `needle` as a subsequence of `haystack`, preferring tight, boundary-aligned
// matches. Smart case: an all-lowercase needle matches case-insensitively, but any
// uppercase character makes the whole match case-sensitive.
Result score(const QString &needle, const QString &haystack);

// Scores a path, strongly preferring a match inside the basename over one scattered
// through parent directories (§6), and penalising depth so shallow hits rank first.
Result scorePath(const QString &needle, const QString &path);

} // namespace FuzzyScorer
