// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTOUTLINE_H
#define DOCUMENTOUTLINE_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class BlockModel;

// The document outline: a GUI-free projection over the block model's heading
// blocks, exposed as the `documentOutline` context property on the
// revision-counter contract. It walks the heading blocks into a tree (level,
// text, block id, computed slug), flattens the VISIBLE rows like
// FolderTreeModel (children of a collapsed heading are not rows), and answers
// the queries the outline panel, the table-of-contents block, and
// internal-link resolution all consume. The panel renders and never owns the
// tree.
//
// One shared pure slug function (baseSlug) feeds all three consumers so they
// agree: lowercase, spaces to hyphens, punctuation stripped, and collisions in
// document order disambiguated with numeric suffixes (-1, -2, …). rebuild()
// runs behind a compressed queued call so a burst of heading edits recomputes
// once, off the keystroke path.
class DocumentOutline : public QAbstractListModel
{
    Q_OBJECT
    // Bumped on every observable change of the tree, the level filter, or the
    // current section; panel bindings depend on it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY revisionChanged)
    Q_PROPERTY(int slugsRevision READ slugsRevision NOTIFY slugsChanged)
    // Which heading levels appear, as a bitmask of (1 << (level-1)) for levels
    // 1..4. A heading whose level is filtered out is not a row, but its
    // descendants still nest under the nearest shown ancestor. Default: all.
    Q_PROPERTY(int levelMask READ levelMask WRITE setLevelMask NOTIFY levelMaskChanged)
    // True while the document has at least one heading (the panel shows an
    // empty-state hint otherwise).
    Q_PROPERTY(bool hasHeadings READ hasHeadings NOTIFY revisionChanged)
    // The visible row whose section contains the caret, for the panel to
    // scroll to and light up; -1 when before the first heading. Updated off
    // the caret-move signal without rebuilding the tree.
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)

public:
    enum Roles {
        LevelRole = Qt::UserRole + 1, // heading level 1..4
        TextRole,                     // display text (inline markers stripped)
        BlockIndexRole,               // row in the block model
        BlockIdRole,
        SlugRole,                     // final collision-disambiguated slug
        DepthRole,                    // indentation depth in the visible tree
        CollapsedRole,
        HasChildrenRole,
        IsCurrentRole                 // the section containing the caret
    };

    explicit DocumentOutline(QObject *parent = nullptr);

    void setModel(BlockModel *model);
    BlockModel *model() const { return m_model; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int revision() const { return m_revision; }
    int slugsRevision() const { return m_slugsRevision; }
    bool hasHeadings() const { return !m_nodes.isEmpty(); }
    int currentRow() const { return m_currentRow; }
    int levelMask() const { return m_levelMask; }
    void setLevelMask(int mask);

    // The shared slug function: pure, no collision handling — lowercase,
    // spaces/underscores to hyphens, other punctuation stripped, runs of
    // hyphens collapsed, ends trimmed. Collision disambiguation is layered
    // on top by the tree walk (see slugForBlockIndex).
    static QString baseSlug(const QString &text);

    // Resolution shared by the outline, the TOC, and internal links. All slug
    // comparisons are over the final (disambiguated) slugs. A slug matching no
    // heading returns -1 / false — the caller renders the recoverable
    // "unresolved" state.
    Q_INVOKABLE int blockIndexForSlug(const QString &slug) const;
    Q_INVOKABLE bool hasSlug(const QString &slug) const;
    Q_INVOKABLE QString slugForBlockIndex(int blockIndex) const;
    // QML-reachable baseSlug: a wiki-link's #heading part is raw heading
    // TEXT; the follow path slugs it here before blockIndexForSlug.
    Q_INVOKABLE QString slugForText(const QString &text) const
    { return baseSlug(text); }

    // The visible row whose section contains the given block index (live
    // highlighting); -1 if before the first heading or no headings. Considers
    // the level filter and collapse (a block under a collapsed heading maps to
    // that collapsed row).
    Q_INVOKABLE int rowForBlock(int blockIndex) const;

    // Collapse a heading's subtree by its row; keyed by block id so it
    // survives rebuilds. Bumps the revision (the visible-row set changes).
    Q_INVOKABLE void toggleCollapsed(int row);

    // Update the current-section highlight from the caret's block index. Cheap:
    // it re-marks at most two rows and never rebuilds the tree.
    Q_INVOKABLE void setCurrentBlock(int blockIndex);

    // The block index a visible row points at, for click-to-scroll.
    Q_INVOKABLE int blockIndexAt(int row) const;

    // Every heading as {level, text, slug, blockIndex}, document order — the
    // Ctrl+K "link to heading" target list. Ignores the level filter and
    // collapse (Ctrl+K offers all headings).
    Q_INVOKABLE QVariantList headings() const;

    // The table-of-contents body: a nested markdown list of [text](#slug)
    // internal links, two spaces of indent per level below the shallowest
    // heading present. Regenerated whenever headings change.
    Q_INVOKABLE QString tocMarkdown() const;

    // Synchronous rebuild; model signals schedule this compressed through a
    // queued call. Public for tests.
    Q_INVOKABLE void rebuildNow();

signals:
    void revisionChanged();
    void levelMaskChanged();
    void currentRowChanged();
    // Emitted only when the heading projection actually changes (heading
    // added/removed/moved/renamed or link target index changed). Consumers
    // that restyle or regenerate heading-derived UI listen to this rather
    // than every outline-panel revision.
    void slugsChanged();

private:
    struct Node {
        int level = 1;        // heading level 1..4
        QString text;         // display text (markers stripped)
        QString blockId;
        int blockIndex = -1;
        QString slug;         // final, collision-disambiguated
        int parent = -1;      // nearest shallower-level heading, -1 for none
        int depth = 0;        // indentation depth in the visible tree
        bool hasChildren = false;
    };

    void scheduleRebuild();
    void rebuild();
    bool isShown(const Node &node) const;
    // Rows visible given the level filter and collapse state.
    void recomputeVisibleRows();

    BlockModel *m_model = nullptr;
    int m_revision = 0;
    int m_slugsRevision = 0;
    int m_levelMask = 0xF;          // all four levels

    QList<Node> m_nodes;            // every heading, document order (the tree)
    QList<int> m_visibleRows;       // indexes into m_nodes that are rows
    QHash<QString, int> m_slugToNode; // final slug -> node index
    QSet<QString> m_collapsedIds;   // headings collapsed, by block id
    int m_currentBlockIndex = -1;
    int m_currentRow = -1;          // visible row of the caret's section

    bool m_rebuildQueued = false;

    void updateCurrentRow();
};

#endif // DOCUMENTOUTLINE_H
