// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef RESERVEDSUBTREES_H
#define RESERVEDSUBTREES_H

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <vector>

// A subtree of the vault the application manages, and the few files inside it
// the note index is allowed to see.
//
// The scanner skips every dot-prefixed directory, so a tree such as
// `.reports/` or the trash is invisible to the index, to search, to links and
// to the note counts. That default is right for almost everything an
// application keeps beside a person's notes: working copies, control data,
// caches. What it cannot express is the narrow case where ONE file in each of
// those folders is a document in its own right — a report, a summary, a log
// somebody may want to find again — while everything around it stays out.
//
// A registration says three things:
//
//   - the subtree is reserved: the walk enters it, and nothing in it is
//     treated as a folder of the user's;
//   - files whose path inside the subtree matches `admitPattern` are admitted
//     to the index, and nothing else there is;
//   - admitted files form a REALM of their own, named by `label`, kept apart
//     from the user's notes everywhere the two would otherwise be mixed.
//
// A realm file is indexed, searchable, and reachable by a folder-qualified
// wiki-link. It is not one of the user's notes: it never resolves from a bare
// [[basename]], it is not in the note counts or the statistics, it is not a
// query-block result, and a vault-wide export leaves it where it is.
//
// The pattern is matched against the path RELATIVE TO THE SUBTREE, so
// `*/report.md` admits `.reports/<anything>/report.md` and admits neither
// `.reports/<anything>/staged/report.md` nor anything deeper: a wildcard does
// not cross a path separator. Dot-prefixed directories inside the subtree are
// skipped by the ordinary rule, so a subtree's own control data stays out
// whatever the pattern says.
//
// `requiredType` is a cross-check rather than the rule: a file admitted by
// path is dropped again if its front matter does not carry
// `kvit-type: <requiredType>`. Recognition is by path so the walk need not
// read every file it passes; the front matter is what stops a file that merely
// landed in the right place from being taken for one of the application's.
// Leave it empty to admit on the path alone.
//
// With nothing registered — which is what the open editor runs — every query
// here answers "no" against an empty vector, and the walk, the link index and
// the view models behave exactly as they did before this existed.
struct ReservedSubtree {
    // The directory name at the vault root, dot prefix included:
    // ".reports".
    QString name;
    // What the realm is called wherever it is shown: "Reports".
    QString label;
    // A wildcard over the path inside the subtree: "*/report.md".
    QString admitPattern;
    // The `kvit-type` front-matter value an admitted file must carry, or
    // empty to admit on the path alone.
    QString requiredType;
};

class ReservedSubtrees
{
public:
    // Ignores a registration with no name or no pattern, and a second
    // registration of a name already reserved. Both are a caller's mistake
    // rather than a state to represent.
    void add(const ReservedSubtree &subtree);
    void clear();

    bool isEmpty() const { return m_subtrees.empty(); }
    int count() const { return static_cast<int>(m_subtrees.size()); }

    // The reserved subtree names, in registration order. The walk asks this
    // to decide whether to enter a dot-directory at the vault root.
    QStringList names() const;
    // The realms, in registration order: their labels, indexed alike.
    QStringList labels() const;
    // The label of the realm `name` belongs to, or empty for a name nobody
    // reserved.
    QString labelForName(const QString &name) const;

    // Whether `relPath` — a note path relative to the vault root — lies
    // inside a reserved subtree at all, admitted or not.
    bool isReservedPath(const QString &relPath) const;
    // Whether `relDir` IS a reserved subtree or lies inside one. A walk that
    // starts partway down the tree — a re-read after a watcher event — asks
    // this to know which rules it is under.
    bool isReservedDir(const QString &relDir) const;

    // The realm label for a path the walk has admitted, or empty when the
    // path is an ordinary note or an unadmitted file inside a subtree.
    QString admittedLabel(const QString &relPath) const;
    // The `kvit-type` an admitted path has to carry, or empty when the
    // registration asks for none.
    QString requiredTypeFor(const QString &relPath) const;

    // Whether a front-matter type satisfies the registration that admitted
    // `relPath`. True for a path nobody admitted, so a caller that asks about
    // an ordinary note is not told its note is wrong.
    bool typeSatisfies(const QString &relPath, const QString &type) const;

private:
    struct Compiled {
        ReservedSubtree spec;
        QRegularExpression pattern;
    };

    const Compiled *subtreeFor(const QString &relPath) const;

    std::vector<Compiled> m_subtrees;
};

#endif // RESERVEDSUBTREES_H
