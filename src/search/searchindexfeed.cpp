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
    return m_index && !rootPath.isEmpty() && m_openRoot == rootPath;
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
    if (m_openRoot != rootPath) {
        m_index->openForRoot(rootPath);
        m_openRoot = rootPath;
    }
    m_index->reconcile(m_listing());
}

void SearchIndexFeed::openFor(const QString &rootPath)
{
    if (!m_index || rootPath.isEmpty())
        return;
    m_index->openForRoot(rootPath);
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
