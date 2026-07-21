// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef FUZZYMATCH_H
#define FUZZYMATCH_H

#include <QString>
#include <QStringList>

// The one fuzzy matcher (features.md §4.3, lifted from BlockMenuModel):
// case-insensitive subsequence matching with tiered ranking — a whole-string
// prefix beats a word prefix beats a bare subsequence. The block menu, the
// quick switcher, and the [[ completion popup all rank through this header
// so they can never disagree.
namespace FuzzyMatch {

// Match quality; smaller is better. NoMatch excludes the candidate.
enum Tier { PrefixMatch = 0, WordPrefixMatch, SubsequenceMatch, NoMatch };

inline bool isSubsequence(const QString &needle, const QString &haystack)
{
    int n = 0;
    for (int h = 0; h < haystack.size() && n < needle.size(); ++h) {
        if (haystack.at(h) == needle.at(n))
            ++n;
    }
    return n == needle.size();
}

// Best tier of `loweredQuery` over the candidate strings (the primary name
// first, then any aliases). Both sides must already be lowercased by the
// caller for the query; candidates are lowercased here.
inline Tier tierFor(const QString &loweredQuery, const QStringList &candidates)
{
    Tier best = NoMatch;
    for (const QString &candidate : candidates) {
        const QString lowered = candidate.toLower();
        if (lowered.startsWith(loweredQuery))
            return PrefixMatch;  // nothing beats it; done
        if (best > WordPrefixMatch) {
            const QStringList words = lowered.split(QLatin1Char(' '),
                                                    Qt::SkipEmptyParts);
            for (const QString &word : words) {
                if (word.startsWith(loweredQuery)) {
                    best = WordPrefixMatch;
                    break;
                }
            }
        }
        if (best > SubsequenceMatch && isSubsequence(loweredQuery, lowered))
            best = SubsequenceMatch;
    }
    return best;
}

} // namespace FuzzyMatch

#endif // FUZZYMATCH_H
