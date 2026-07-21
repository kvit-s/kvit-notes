// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef VAULTPATHS_H
#define VAULTPATHS_H

#include <QString>

// Vault containment.
//
// A vault is the directory subtree the user selected, and every path the
// repository hands to the filesystem must land inside it. Textual prefix
// tests are not enough: a symbolic link is a path inside the root that names
// a file outside it, so containment is decided on canonical paths, with every
// link along the way already resolved.
//
// This is the one place the rule is implemented. Persisted state — a
// recovery-journal filename, collection.json's last-open note, an import
// destination — arrives as untrusted input, and each of those checks goes
// through here rather than reimplementing the comparison.
namespace VaultPaths {

// The canonical form of a path that need not exist yet. QFileInfo's own
// canonicalFilePath returns an empty string for anything missing, which
// would leave containment unanswerable for a note about to be created, so
// the deepest existing ancestor is canonicalized and the remaining segments
// are appended to it.
QString canonicalizeMissingOk(const QString &path);

// True when `absPath` resolves to `canonicalRoot` itself or to something
// beneath it. Both sides are canonical, so a link pointing out of the vault
// fails here however innocent its textual path looks.
bool isWithinCanonicalRoot(const QString &canonicalRoot, const QString &absPath);

// True when `relPath` is a plain relative path: not empty, not absolute, no
// backslashes, no "." or ".." segment, no empty segment, and already in
// canonical form. This is the shape test that runs before containment,
// because a value failing it is malformed rather than merely outside.
bool isPlainRelativePath(const QString &relPath);

// --- Repository-owned directories --------------------------------------
//
// The vault contains two kinds of directory. The note tree is the user's, and
// a symbolic link inside it is theirs to place: the scan simply does not
// follow it. The control directories — `.kvit` and everything under it, and
// `assets` — are the repository's own, and it creates, rewrites and deletes
// them without asking. A link standing where one of those should be therefore
// turns a routine internal write into a write outside the vault, and the
// legacy-cache cleanup into a recursive delete of somebody else's directory.
//
// The rule for those paths is stricter than containment: an owned directory
// must be a real directory. A link is refused even when it points back inside
// the vault, because "the repository owns this subtree" stops being true the
// moment something else decides where it lives.

// True when no component of `relDir` below `rootPath` is a symbolic link, a
// Windows junction or a shortcut, and the whole path still resolves inside
// the vault. `rootPath` itself may be reached through a link — that is the
// user's choice of where the vault lives, and canonical containment is
// decided against its resolved form.
bool ownedDirIsSound(const QString &rootPath, const QString &relDir);

// The absolute path of a repository-owned directory, or "" when it is not
// sound. "" is a refusal: the caller must not fall back to the textual path.
QString ownedDir(const QString &rootPath, const QString &relDir);

// ownedDir(), creating the directory when it is missing. The soundness test
// runs again after creation, so a link that appeared in between is still
// caught before anything is written through it.
QString ensureOwnedDir(const QString &rootPath, const QString &relDir);

// A file inside a repository-owned directory: `<rootPath>/<relDir>/<name>`,
// or "" when the directory is unsound or `name` is not a single plain
// segment.
QString ownedFile(const QString &rootPath, const QString &relDir,
                  const QString &name);

} // namespace VaultPaths

#endif // VAULTPATHS_H
