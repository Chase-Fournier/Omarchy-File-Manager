#include "fuzzyscorer.h"

#include <QtGlobal>

namespace {

// Weights, in the spirit of fzf. The ordering between them is what encodes §6's
// preference: exact prefix > word boundary > camelCase > consecutive run > scattered.
constexpr int kMatch = 16;
constexpr int kBonusBoundary = 8;
constexpr int kBonusCamel = 7;
constexpr int kBonusConsecutive = 8;
constexpr int kBonusFirstChar = 2; // multiplier on the first matched character's bonus
constexpr int kPenaltyGapStart = -3;
constexpr int kPenaltyGapExtension = -1;

// A match wholly inside the basename beats one smeared across parent directories by
// more than any arrangement of the characters could make up.
constexpr int kBonusBasename = 96;
constexpr int kPenaltyDepth = 4;
// Between two equally good matches, the shorter name is the more exact one: "main.cpp"
// should beat "main.cpp.bak" for the query "main.cpp". Capped, so that length only ever
// breaks a near-tie — uncapped it swamps the boundary and camelCase bonuses, and a long
// well-structured name loses to a short mushy one.
constexpr int kPenaltyLength = 1;
constexpr int kPenaltyLengthCap = 8;

bool isBoundaryChar(QChar c)
{
    return c == QLatin1Char('/') || c == QLatin1Char('_') || c == QLatin1Char('-')
        || c == QLatin1Char('.') || c == QLatin1Char(' ');
}

// How much a match at `index` is worth beyond the base, given what precedes it.
int bonusAt(const QString &haystack, int index)
{
    if (index == 0)
        return kBonusBoundary;

    const QChar previous = haystack.at(index - 1);
    const QChar current = haystack.at(index);

    if (isBoundaryChar(previous))
        return kBonusBoundary;
    if (previous.isLower() && current.isUpper())
        return kBonusCamel;
    if (previous.isDigit() != current.isDigit())
        return kBonusCamel;
    return 0;
}

bool sameChar(QChar a, QChar b, bool caseSensitive)
{
    return caseSensitive ? a == b : a.toLower() == b.toLower();
}

// Greedy forward scan: the earliest subsequence match, or -1.
int matchEnd(const QString &needle, const QString &haystack, bool caseSensitive)
{
    int n = 0;
    for (int h = 0; h < haystack.size(); ++h) {
        if (sameChar(needle.at(n), haystack.at(h), caseSensitive)) {
            if (++n == needle.size())
                return h + 1;
        }
    }
    return -1;
}

// Walking back from the end of a greedy match gives the tightest window containing it,
// which is what stops "abc" scoring against half the string when the letters happen to
// be spread out early on.
int matchStart(const QString &needle, const QString &haystack, int end, bool caseSensitive)
{
    int n = needle.size() - 1;
    for (int h = end - 1; h >= 0; --h) {
        if (sameChar(needle.at(n), haystack.at(h), caseSensitive)) {
            if (--n < 0)
                return h;
        }
    }
    return -1;
}

} // namespace

namespace FuzzyScorer {

Result score(const QString &needle, const QString &haystack)
{
    Result result;
    if (needle.isEmpty()) {
        result.matched = true;
        return result;
    }
    if (haystack.isEmpty() || needle.size() > haystack.size())
        return result;

    // Smart case: typing lowercase is a wildcard on case, typing uppercase means it.
    bool caseSensitive = false;
    for (const QChar c : needle) {
        if (c.isUpper()) {
            caseSensitive = true;
            break;
        }
    }

    const int end = matchEnd(needle, haystack, caseSensitive);
    if (end < 0)
        return result;
    const int start = matchStart(needle, haystack, end, caseSensitive);
    if (start < 0)
        return result;

    // Score the tightened window, greedily taking each needle character at its earliest
    // position within it — good enough in practice and O(n), which the §12 budget needs.
    int total = 0;
    int consecutive = 0;
    int previousMatch = -1;
    int n = 0;

    for (int h = start; h < end && n < needle.size(); ++h) {
        if (!sameChar(needle.at(n), haystack.at(h), caseSensitive))
            continue;

        int bonus = bonusAt(haystack, h);
        if (previousMatch >= 0 && h == previousMatch + 1) {
            ++consecutive;
            // A run keeps the strongest bonus it started with rather than decaying to
            // zero mid-word, so "file" in "myfile" still reads as one unit.
            bonus = qMax(bonus, kBonusConsecutive);
        } else {
            consecutive = 0;
            if (previousMatch >= 0) {
                const int gap = h - previousMatch - 1;
                total += kPenaltyGapStart + kPenaltyGapExtension * (gap - 1);
            }
        }

        if (n == 0)
            bonus *= kBonusFirstChar;

        total += kMatch + bonus;
        result.positions.append(h);
        previousMatch = h;
        ++n;
    }

    if (n != needle.size())
        return result;

    result.matched = true;
    result.score = total;
    return result;
}

Result scorePath(const QString &needle, const QString &path)
{
    if (needle.isEmpty()) {
        Result all;
        all.matched = true;
        return all;
    }

    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString basename = slash >= 0 ? path.mid(slash + 1) : path;

    // The basename is what people mean. Try it first, and only fall back to the whole
    // path when the query genuinely spans directories.
    // Shallower results first: a hit six directories down is rarely the one wanted. This
    // applies to both branches, because two files with the identical basename are
    // separated by nothing else.
    const int depth = kPenaltyDepth * path.count(QLatin1Char('/'));

    Result inName = score(needle, basename);
    if (inName.matched) {
        inName.score += kBonusBasename - depth
            - kPenaltyLength
                * qMin(kPenaltyLengthCap, int(basename.size() - needle.size()));
        if (slash >= 0) {
            for (int &position : inName.positions)
                position += slash + 1;
        }
        return inName;
    }

    Result inPath = score(needle, path);
    if (!inPath.matched)
        return inPath;

    inPath.score -= depth;
    return inPath;
}

} // namespace FuzzyScorer
