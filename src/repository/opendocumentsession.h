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

    // Whether the in-memory document holds edits the file does not.
    //
    // Deleting a note moves the file into the trash and then closes the
    // document, so with autosave off — or simply before the save debounce
    // fires — the trashed file was the last saved revision and everything
    // typed since went with the closed document. The repository has to be
    // able to ask, because only the session knows.
    //
    // Defaulted rather than pure so a session that has not implemented it
    // still compiles and behaves as it did; such a session reports no unsaved
    // work and the repository proceeds exactly as before.
    virtual bool hasUnsavedChanges() const { return false; }

    // Write the live document to its file now and return only once the file
    // holds it. False means the file does not, and the repository abandons
    // whatever destructive operation asked — the point is that nothing is
    // moved to the trash unless the trashed file is the newest revision.
    //
    // Synchronous by contract: an asynchronous save would be racing the
    // rename that follows it.
    virtual bool persistCurrentRevision() { return false; }
};

#endif // OPENDOCUMENTSESSION_H
