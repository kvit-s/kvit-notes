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

} // namespace VaultPaths

#endif // VAULTPATHS_H
