// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "searchindexfeed.h"

#include "collectionsearchindex.h"

#include <utility>

SearchIndexFeed::SearchIndexFeed(ListingProvider listing,
                                 AbsolutePathResolver absolutePath)
    : m_listing(std::move(listing))
    , m_absolutePath(std::move(absolutePath))
{
}

SearchIndexFeed::~SearchIndexFeed() = default;

void SearchIndexFeed::setIndex(CollectionSearchIndex *index)
{
    m_index = index;
    m_openRoot.clear();
}

bool SearchIndexFeed::servesRoot(const QString &rootPath) const
{
    // Two questions, and the second one is the index's to answer. This object
    // knows which root it asked for; whether the index took it is a fact about
    // the index, and asking it here is what makes a failed open visible to the
    // next sync instead of being remembered as a success.
    return m_index && !rootPath.isEmpty() && m_openRoot == rootPath
        && m_index->openRoot() == rootPath;
}

void SearchIndexFeed::close()
{
    if (m_index && !m_openRoot.isEmpty()) {
        // Non-blocking: closing a vault must not park the caller behind a
        // reconcile of the vault being left. The connections close on the
        // worker threads, and the next openFor() is delivered behind them.
        m_index->requestClose();
        m_openRoot.clear();
    }
}

void SearchIndexFeed::syncTo(const QString &rootPath)
{
    if (!m_index)
        return;
    if (rootPath.isEmpty()) {
        close();
        return;
    }
    // Open (or reopen) the cache database for the current root, then reconcile
    // it against the on-disk listing: parse new or changed notes, drop missing
    // ones. Reconcile compares each note's content fingerprint, so an
    // unchanged note costs a read and a hash rather than a reparse, and the
    // first cold build remains the expensive one.
    //
    // Reopening covers a root that has changed and a root whose open did not
    // take: an open can fail for reasons that pass — the cache directory was
    // briefly unwritable, the file was locked by something else — and a sync
    // that assumed otherwise left search dead for as long as the vault stayed
    // open.
    if (!servesRoot(rootPath))
        openFor(rootPath);
    // The reconcile goes to the same worker thread as the open and is
    // delivered behind it, so it reaches the new root's database rather than
    // needing that database to be ready first.
    m_index->reconcile(m_listing());
}

void SearchIndexFeed::openFor(const QString &rootPath)
{
    if (!m_index || rootPath.isEmpty())
        return;
    m_index->openForRoot(rootPath);
    // What is recorded here is the root the index has been told to take, which
    // is what stops the next sync reopening it. The open itself finishes on the
    // search threads; whether it succeeded is the index's to report, and
    // servesRoot() asks it rather than storing an answer that had not been
    // given yet.
    m_openRoot = rootPath;
}

void SearchIndexFeed::reindexNoteFromText(const QString &rootPath,
                                          const QString &relPath,
                                          const QString &fileText,
                                          qint64 fileSize,
                                          qint64 modifiedMs)
{
    if (servesRoot(rootPath))
        m_index->replaceFromText(relPath, fileText, fileSize, modifiedMs);
}

void SearchIndexFeed::reindexNote(const QString &rootPath,
                                  const QString &relPath)
{
    if (servesRoot(rootPath))
        m_index->replaceFromPath(relPath, m_absolutePath(relPath));
}

void SearchIndexFeed::dropNote(const QString &rootPath, const QString &relPath)
{
    if (servesRoot(rootPath))
        m_index->removePath(relPath);
}
