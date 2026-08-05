// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTDECORATIONS_H
#define DOCUMENTDECORATIONS_H

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

// What a linked module may draw inside the document view without changing the
// document.
//
// The three UI slots in ExtensionRegistry put module content above, below or
// beside the editor. Annotations, review markers, comment threads and inline
// panels all want to draw BETWEEN and BESIDE a note's blocks instead, and
// there was no way to reach that area at all. This is that seam, and it is
// deliberately narrow: a module says where it wants something drawn and which
// QML draws it, and the document view honours that at render time.
//
// Two kinds of entry:
//
//   - A CONTAINER is drawn after a block, full column width, sized to its own
//     content. It is rendered, never inserted. Nothing reaches BlockModel, so
//     `BlockModel::count()` and every row index are identical with and without
//     containers present, and a consumer that stores anchors as block indices
//     keeps them. Typing inside a container is not an edit to the note: the
//     container's content is a separate editing context and the note's undo
//     stack neither records it nor is crossed by it.
//
//   - A MARGIN ITEM is drawn in the reserved column at the right edge of the
//     document, addressed by block AND by visual text line within that block,
//     so a glyph can sit beside one wrapped line of a paragraph. The column
//     exists only once a module reserves it (see marginColumnReserved), and it
//     is reserved for as long as the editor runs rather than appearing with
//     the first glyph, since the text would otherwise shift horizontally
//     under the reader the moment anything was drawn.
//
// Placement is dynamic. A module computes where its entries sit as the
// document changes and moves them with setContainerBlock() /
// setMarginItemPosition(); each entry keeps its id across a move, so nothing
// is torn down and rebuilt to shift an anchor by one block.
//
// Every entry carries an `owner`, which is the module's name. Two modules can
// contribute at once without seeing each other's entries, and removeAll()
// takes one module's contributions back without touching the others'.
// Ownership of the context objects handed in stays with the module: this class
// holds QPointers and never deletes one.
//
// The open editor registers nothing, and with nothing registered the view asks
// this object one cheap question per row and gets an empty answer, the margin
// column has zero width, and the layout is what it was before this existed.
class DocumentDecorations : public QObject
{
    Q_OBJECT

    // Bumped whenever anything about the entries changes. QML bindings read it
    // to re-run a query — a method call alone would not subscribe to anything,
    // which is the same idiom the search and selection models already use.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

    // Whether the document reserves its right-hand margin column. A module
    // sets this once, at install time; the width is a fixed measure of the
    // reading font (marginColumnEms) so the column is proportional to the text
    // beside it.
    Q_PROPERTY(bool marginColumnReserved READ marginColumnReserved
                   WRITE setMarginColumnReserved NOTIFY marginColumnReservedChanged)
    Q_PROPERTY(qreal marginColumnEms READ marginColumnEms CONSTANT)

    // False while nothing is registered at all, which is the state the open
    // build stays in. The view checks it before doing any per-row work.
    Q_PROPERTY(bool active READ isActive NOTIFY changed)

public:
    explicit DocumentDecorations(QObject *parent = nullptr);
    ~DocumentDecorations() override;

    int revision() const { return m_revision; }
    bool isActive() const;

    bool marginColumnReserved() const { return m_marginColumnReserved; }
    void setMarginColumnReserved(bool reserved);
    qreal marginColumnEms() const { return 1.5; }

    // ---- registration (a module's side) --------------------------------
    //
    // Each returns the entry's id, which is what later moves and removals
    // name. `source` is the QML file drawing the entry; `context` is an object
    // the module owns and the drawn item can read (see the property contract
    // below). A null or empty source registers nothing and returns an empty
    // id.

    // Draw `source` after block `afterBlock`. An anchor naming a block the
    // document does not have draws nothing and costs nothing, which is what a
    // module's anchor looks like for the moment between the note shrinking
    // and the module re-placing its entries.
    QString addContainer(const QString &owner, int afterBlock, const QUrl &source,
                         QObject *context = nullptr);
    bool setContainerBlock(const QString &id, int afterBlock);
    bool removeContainer(const QString &id);

    // Draw `source` beside visual line `line` of block `block`, in the
    // reserved margin column. Lines are numbered from zero within the block's
    // rendered text; a line past the block's last one draws beside the last.
    QString addMarginItem(const QString &owner, int block, int line,
                          const QUrl &source, QObject *context = nullptr);
    bool setMarginItemPosition(const QString &id, int block, int line);
    bool removeMarginItem(const QString &id);

    // Everything one module registered. A module calls this when it is done,
    // and a test calls it to isolate a case.
    void removeAll(const QString &owner);
    void clear();

    int containerCount() const { return static_cast<int>(m_containers.size()); }
    int marginItemCount() const { return static_cast<int>(m_marginItems.size()); }

    // ---- what the view reads -------------------------------------------
    //
    // Both return the entries in registration order, so two modules
    // contributing after the same block draw in a stable, explainable order
    // rather than one hiding the other. Each element is a map of
    // { id, owner, source, context } — plus `line` for a margin item.
    //
    // The drawn QML may declare `decorationContext` and `decorationBlock`
    // properties; the view fills them in when they exist and leaves an item
    // that declares neither alone.
    Q_INVOKABLE QVariantList containersAfter(int blockIndex) const;
    Q_INVOKABLE QVariantList marginItemsForBlock(int blockIndex) const;

    // ---- geometry ------------------------------------------------------
    //
    // Consumers implement scroll policies of their own ("keep this block fixed
    // while content expands below it"), which needs positions. The document
    // view installs itself here when it loads and answers these; each returns
    // a null rectangle when no view is loaded, when the row is outside the
    // virtualized window the list keeps alive, or when the id is unknown.
    //
    // Rectangles are in the block list's content coordinates — the same space
    // its contentY moves through — so a module can compare a position with the
    // scroll offset directly.
    // Called by the document view itself as it loads, so the geometry
    // questions below have something to ask.
    Q_INVOKABLE void setDocumentView(QObject *view);
    QObject *documentView() const { return m_view; }

    Q_INVOKABLE QRectF blockGeometry(int blockIndex) const;
    Q_INVOKABLE QRectF containerGeometry(const QString &id) const;
    // The vertical extent of visual line `line` of block `blockIndex`, which
    // is what places a glyph beside one line of a wrapped paragraph.
    Q_INVOKABLE QRectF lineGeometry(int blockIndex, int line) const;

signals:
    void changed();
    void marginColumnReservedChanged();

private:
    struct Entry
    {
        QString id;
        QString owner;
        QUrl source;
        QPointer<QObject> context;
        // A container reads `block` as the block it follows; a margin item
        // reads it as the block it sits beside, and `line` as the visual line
        // within that block. `line` is unused for containers.
        int block = 0;
        int line = 0;
    };

    QString nextId(const QString &prefix);
    static Entry *find(std::vector<Entry> &entries, const QString &id);
    QVariantMap describe(const Entry &entry, bool withLine) const;
    QRectF askView(const char *method, const QVariantList &arguments) const;
    void bump();

    std::vector<Entry> m_containers;
    std::vector<Entry> m_marginItems;
    // A QPointer because the view is a QML item in a window that can close
    // while this object lives on: a raw pointer would be answered with a
    // crash the first time a module asked for geometry afterwards.
    QPointer<QObject> m_view;
    int m_revision = 0;
    int m_nextId = 1;
    bool m_marginColumnReserved = false;
};

#endif // DOCUMENTDECORATIONS_H
