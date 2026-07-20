// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTEXPORTER_H
#define DOCUMENTEXPORTER_H

#include <QObject>
#include <QPair>
#include <QString>
#include "block.h"

class BlockModel;
class NoteCollection;
class Theme;

// Document export (features.md §12.5): one pipeline turns a block list into a
// single self-contained HTML string — theme CSS inlined, inline spans and
// links rendered, images and rendered math embedded as data: URIs, code
// highlighted as styled spans, tables and kanban boards as static <table>s,
// callouts as bordered blocks, headings carrying id anchors that the TOC and
// internal links target. HTML export writes that string; PDF export prints it
// through QTextDocument/QPdfWriter (the one seam that touches Qt's document
// engine). Plain-text export is display text with structural prefixes;
// Markdown export is the existing serializer.
//
// Math is format-aware: HTML export leaves the TeX in the
// document — display math as \[ … \] in a p.math-display, inline math as
// \( … \) — and injects one pinned MathJax CDN <script> tag when math is
// present, so the viewer's browser typesets it. The PDF print seam embeds
// PNG math data URIs (QTextDocument runs no JavaScript).
// KVIT_MATH_RENDER=png forces PNG embeds into the HTML export for fully
// self-contained/offline output.
//
// The HTML builder is a pure function of its inputs (asserted per block type
// by the unit tests). Exposed as the `documentExporter` context property.
class DocumentExporter : public QObject
{
    Q_OBJECT

public:
    explicit DocumentExporter(QObject *parent = nullptr);

    void setTheme(Theme *theme) { m_theme = theme; }
    // Image resolution context: the open note's folder and the collection root
    // (either may be empty in single-file mode). A collection or selection
    // export overrides this per note as it goes, so relative media resolve
    // against the folder each note actually lives in.
    Q_INVOKABLE void setImageContext(const QString &noteDir,
                                     const QString &collectionRoot);

    // The note open in the editor, with its unsaved markdown, so a
    // collection or selection export reflects what the user is looking at
    // rather than what last reached disk. Export snapshots rather than
    // saving, because exporting must not write to the user's notes.
    // The snapshot is taken here, at call time.
    Q_INVOKABLE void setLiveNote(const QString &relPath, BlockModel *model);
    Q_INVOKABLE void clearLiveNote();

    // ---- HTML ----
    // Self-contained HTML for a live model / for stored markdown.
    Q_INVOKABLE QString htmlForModel(BlockModel *model,
                                     const QString &title = QString()) const;
    Q_INVOKABLE QString htmlForMarkdown(const QString &markdown,
                                        const QString &title = QString()) const;

    // ---- Plain text ----
    Q_INVOKABLE QString plainTextForModel(BlockModel *model) const;
    Q_INVOKABLE QString plainTextForMarkdown(const QString &markdown) const;

    // ---- Write to disk ----
    // format is one of "markdown", "html", "pdf", "text". markdown() writes the
    // serializer output; the caller supplies it for a live model.
    Q_INVOKABLE bool writeModel(BlockModel *model, const QString &title,
                                const QString &format, const QString &path);
    // Write from stored markdown (collection export path).
    Q_INVOKABLE bool writeMarkdownAs(const QString &markdown,
                                     const QString &title,
                                     const QString &format, const QString &path);

    // Collection export: one file per note under destDir, mirroring the folder
    // tree, or a single concatenated file. Returns the number written.
    // The collection is passed as QObject* so QML can marshal the context
    // property (NoteCollection is not a registered QML type); cast internally.
    Q_INVOKABLE int exportCollection(QObject *collection,
                                     const QString &destDir,
                                     const QString &format, bool singleFile);
    // Export a specific set of notes (the note-list selection). Same writer as
    // exportCollection, over the given relPaths.
    Q_INVOKABLE int exportNotes(QObject *collection,
                                const QStringList &relPaths,
                                const QString &destDir,
                                const QString &format, bool singleFile);

    // The PDF print seam (static so it is trivially reusable/testable): load
    // the HTML into a QTextDocument and print through QPdfWriter.
    static bool htmlToPdf(const QString &html, const QString &path);

    // The file extension for a format ("md", "html", "pdf", "txt").
    Q_INVOKABLE static QString extensionFor(const QString &format);

private:
    struct Blk {
        Block::BlockType type = Block::Paragraph;
        QString content;
        int indentLevel = 0;
        bool checked = false;
        QString language;
        QString calloutTitle;
    };
    QList<Blk> blocksFromModel(BlockModel *model) const;
    QList<Blk> blocksFromMarkdown(const QString &markdown) const;

    // browserTarget distinguishes HTML export (MathJax TeX by default,
    // image embeds under KVIT_MATH_RENDER) from the PDF print seam (always
    // PNG math — QTextDocument runs no JavaScript).
    QString buildHtml(const QList<Blk> &blocks, const QString &title,
                      bool browserTarget) const;

    // The two halves of buildHtml, split so a combined export can assemble
    // several notes into ONE document: the <body> contents for one note, and
    // the wrapper that closes over them. sawMath/sawMermaid report which
    // shared assets a fragment needs, so the wrapper injects each script tag
    // once for the whole file however many notes asked for it.
    QString buildHtmlBody(const QList<Blk> &blocks, bool browserTarget,
                          bool *sawMath, bool *sawMermaid) const;
    QString wrapHtmlDocument(const QString &body, const QString &title,
                             bool browserTarget, bool sawMath,
                             bool sawMermaid) const;

    // Point image resolution at the folder holding relPath. Returns the
    // previous (noteDir, collectionRoot) so a caller can restore it.
    QPair<QString, QString> useImageContextFor(NoteCollection *collection,
                                               const QString &relPath);
    // The markdown to export for relPath: the editor's unsaved snapshot when
    // this is the live note, otherwise the saved body.
    QString bodyForExport(NoteCollection *collection,
                          const QString &relPath) const;

    QString buildPlainText(const QList<Blk> &blocks) const;

    // Inline markdown -> HTML (recursive over the span registry). With
    // mathJax, $x$ spans become \( … \) delimiters and *sawMath is set;
    // otherwise the TeX falls through as literal text, as before.
    QString renderInline(const QString &md, bool mathJax = false,
                         bool *sawMath = nullptr) const;

    QString cssBlock() const;
    QString dataUriForImagePath(const QString &storedPath) const;
    QString dataUriForMath(const QString &tex) const;
    // Rasterize a natively-supported Mermaid diagram to a PNG data URI at 2x
    // for the PDF path; empty when the source is invalid or an unsupported
    // family, so the caller falls back to escaped source.
    QString dataUriForMermaid(const QString &source) const;
    // Slugs for heading anchors, matching DocumentOutline (collision-suffixed).
    QStringList headingSlugs(const QList<Blk> &blocks) const;

    Theme *m_theme = nullptr;
    QString m_noteDir;
    QString m_collectionRoot;
    QString m_liveRelPath;
    QString m_liveMarkdown;
};

#endif // DOCUMENTEXPORTER_H
