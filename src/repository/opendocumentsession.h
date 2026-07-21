// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef OPENDOCUMENTSESSION_H
#define OPENDOCUMENTSESSION_H

#include <QString>

// The one note the repository does not own.
//
// Every other note in a vault is a file the repository reads and writes
// directly. The open one is different: its authoritative body lives in
// memory, in the block model, and only the document session may write it —
// that exclusive-writer rule is what keeps a metadata change from committing
// a stale body, and a rename from racing a save that is already in flight.
//
// So the repository states what it needs of that session as an interface and
// nothing more. DocumentManager implements it; the repository does not know
// that class exists, and a test can supply its own.
//
// The three things the repository asks for, in the order a mutation needs
// them: find out whether a path is the live one, bring the session to a
// quiescent state before touching the filesystem, and tell it where the note
// went afterwards.
class OpenDocumentSession
{
public:
    virtual ~OpenDocumentSession() = default;

    // Absolute path of the note the session currently owns; empty when no
    // note is open.
    virtual QString openFilePath() const = 0;

    // Bring delegate-local edits into the document. Anything debounced in the
    // UI belongs to the document before a dirty check, a serialization or a
    // write decides anything.
    virtual void flushPendingEdits() = 0;

    // Cancel and await any write already running. This must return only once
    // no worker can still commit to the current path, because a filesystem
    // rename after that point would otherwise let the worker recreate the
    // old one.
    virtual void cancelPendingWrites() = 0;

    // Follow the note to its new path without reloading it.
    virtual void rebindFilePath(const QString &newPath) = 0;

    // Persist repository-owned front matter together with the live in-memory
    // body, as one revisioned snapshot. False means nothing was written and
    // the session restored its previous metadata, so the caller can roll its
    // own index mutation back.
    virtual bool saveWithFrontMatter(const QString &frontMatterBlock) = 0;

    // Detach from the note entirely — it no longer exists.
    virtual void closeDocument() = 0;
};

#endif // OPENDOCUMENTSESSION_H
