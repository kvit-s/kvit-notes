// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTEXPORTER_H
#define DOCUMENTEXPORTER_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include "block.h"

#include <memory>

// Both are held as guarded pointers, which needs the complete type; both are
// already on the include path of every translation unit that has an exporter.
#include "embedmetadata.h"
#include "notecollection.h"

class BlockKindDef;
class BlockKindRegistry;
class BlockModel;
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

    // A collection or selection export runs as a job: one note per turn of the
    // event loop, so the window repaints, the reader can cancel, and a large
    // vault does not build the whole output in memory before anything reaches
    // disk. These describe that job to the UI.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    // Why the last export refused to start, or how it failed. Empty after a
    // clean run.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit DocumentExporter(QObject *parent = nullptr);
    ~DocumentExporter() override;

    void setTheme(Theme *theme) { m_theme = theme; }
    // Which kinds exist. Without one, an export renders the built-in kinds
    // and a block whose fence language a linked module claimed falls back to
    // a plain code block — which is what it looked like before that module
    // was installed, and is wrong once it is.
    void setBlockKindRegistry(const BlockKindRegistry *registry)
    {
        m_blockKinds = registry;
    }
    // The collection a query block is evaluated against. A `query` fence is
    // not content: it is a question about the vault, and the answer only
    // exists while a vault is open. Without one — single-file mode, or a unit
    // test that never opened a collection — a query exports as its source
    // text, which is the only truthful thing left to write.
    void setCollection(NoteCollection *collection) { m_collection = collection; }
    // The embed-preview cache, for the title and description an embed card
    // shows. Read-only and offline: an export never fetches, so a URL nobody
    // has opened yet exports as a plain link to itself.
    void setEmbedMetadata(EmbedMetadata *metadata) { m_embedMetadata = metadata; }
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
    // tree, or a single concatenated file. Returns the number written, and 0
    // when the export was refused — lastError() then says why.
    // The collection is passed as QObject* so QML can marshal the context
    // property (NoteCollection is not a registered QML type); cast internally.
    //
    // These run to completion before returning and are kept for tests and for
    // small, known-bounded scopes. The UI uses the start*() pair below.
    Q_INVOKABLE int exportCollection(QObject *collection,
                                     const QString &destDir,
                                     const QString &format, bool singleFile);
    // Export a specific set of notes (the note-list selection). Same writer as
    // exportCollection, over the given relPaths.
    Q_INVOKABLE int exportNotes(QObject *collection,
                                const QStringList &relPaths,
                                const QString &destDir,
                                const QString &format, bool singleFile);

    // The same two exports as a job: the whole output plan is resolved and
    // checked first, then one note is rendered per turn of the event loop.
    // Return false when the export was refused before doing anything, or when
    // another job is already running; exportFinished() reports the outcome of
    // one that did start. Progress and cancellation are the properties and
    // cancelExport() above.
    Q_INVOKABLE bool startExportCollection(QObject *collection,
                                           const QString &destDir,
                                           const QString &format,
                                           bool singleFile);
    Q_INVOKABLE bool startExportNotes(QObject *collection,
                                      const QStringList &relPaths,
                                      const QString &destDir,
                                      const QString &format, bool singleFile);
    // Stop after the note being rendered. Nothing already written is removed:
    // a cancelled per-note export leaves the files it finished.
    Q_INVOKABLE void cancelExport();

    bool busy() const { return m_job != nullptr; }
    int progress() const;
    int total() const;
    QString lastError() const { return m_lastError; }

    // Budgets. An export used to read every attachment fully, Base64-expand it
    // and hold every note's rendered output at once, so a vault with large
    // media could exhaust memory before a single file was written.
    //
    // An image over the attachment budget is left out of the output rather
    // than failing the export. A combined document over the document budget
    // aborts the export, because there is no partial answer to give: a single
    // file is all or nothing, and the PDF printer needs the whole string.
    Q_INVOKABLE void setMaxAttachmentBytes(double bytes);
    Q_INVOKABLE void setMaxCombinedChars(double chars);

    // The PDF print seam (static so it is trivially reusable/testable): load
    // the HTML into a QTextDocument and print through QPdfWriter.
    static bool htmlToPdf(const QString &html, const QString &path);

    // The file extension for a format ("md", "html", "pdf", "txt").
    Q_INVOKABLE static QString extensionFor(const QString &format);

signals:
    void busyChanged();
    void progressChanged();
    void lastErrorChanged();
    // done notes of total have been rendered; relPath is the one just done.
    void exportProgress(int done, int total, const QString &relPath);
    void exportFinished(int written, int total, bool cancelled,
                        const QString &error);
    // The plan was rejected before anything was written. Separate from a
    // failure: nothing went wrong, the export was not allowed to happen.
    void exportRefused(const QString &reason);

private:
    // What a block kind renders through.
    //
    // A kind writes its own markup and lives in kvit-domain, so it cannot
    // reach the theme, the open collection, the embed cache, the image
    // context or the attachment budget — and it cannot rasterise, because
    // none of that belongs down there. It calls this instead, and this is
    // the exporter answering.
    //
    // Built per render rather than held, because two of its answers are
    // whole-document ones: the table of contents reads every heading in the
    // note and the collision-suffixed slug table that goes with them. That
    // scan stays here, where the document is, rather than being handed to a
    // block that can only see itself.
    class RenderServices;

    // An export renders Block::State, the same snapshot a block holds and a
    // parse produces. It used to render a private copy of that struct with
    // `attributes` left out, and both loops that built it dropped the field
    // silently — which is why alignment, drop caps, divider styles, image
    // effects, embed sizes and table column widths were missing from every
    // HTML and PDF export. There is one snapshot type now.
    // The definition a block renders through: the registry's answer when one
    // is wired, the built-in resolution otherwise.
    const BlockKindDef *kindFor(const Block::State &state) const;

    QList<Block::State> blocksFromModel(BlockModel *model) const;
    QList<Block::State> blocksFromMarkdown(const QString &markdown) const;

    // browserTarget distinguishes HTML export (MathJax TeX by default,
    // image embeds under KVIT_MATH_RENDER) from the PDF print seam (always
    // PNG math — QTextDocument runs no JavaScript).
    QString buildHtml(const QList<Block::State> &blocks, const QString &title,
                      bool browserTarget) const;

    // The two halves of buildHtml, split so a combined export can assemble
    // several notes into ONE document: the <body> contents for one note, and
    // the wrapper that closes over them. sawMath/sawMermaid report which
    // shared assets a fragment needs, so the wrapper injects each script tag
    // once for the whole file however many notes asked for it.
    QString buildHtmlBody(const QList<Block::State> &blocks, bool browserTarget,
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

    QString buildPlainText(const QList<Block::State> &blocks) const;

    // ---- output plan ----
    // Where every note in scope will be written, resolved and canonicalised
    // before a single byte is produced.
    //
    // Choosing the vault itself as the destination for a per-note Markdown
    // export mirrors each output path straight back onto the note it came
    // from, and the export writes only the body — so the note's tags, pinned
    // and favourite state, custom fields and any foreign front matter were
    // destroyed by exporting it. Deciding that one note at a time, while
    // writing, is too late: the first notes are already gone by the time the
    // conflict is noticed. So the whole plan is built and checked first, and
    // an unsafe plan refuses the entire export.
    struct PlannedOutput {
        QString relPath;   // empty for the combined single file
        QString outPath;
    };
    struct OutputPlan {
        QList<PlannedOutput> outputs;
        QString destDir;
        QString error;     // non-empty: refuse, do not write anything
        bool singleFile = false;
    };
    OutputPlan buildOutputPlan(NoteCollection *collection,
                               const QStringList &relPaths,
                               const QString &destDir,
                               const QString &format, bool singleFile) const;
    // Absolute, symlink-resolved form of a path that need not exist yet: the
    // deepest existing ancestor is resolved and the remainder appended, so a
    // destination inside a symlinked directory still compares equal to the
    // source it aliases.
    static QString canonicalTarget(const QString &path);
    static bool isInsideDirectory(const QString &canonicalPath,
                                  const QString &canonicalDir);
    void setLastError(const QString &error);

    // ---- job ----
    // One note per turn of the event loop. The rendering itself stays on the
    // GUI thread: it reads the theme, and the maths and diagram rasterisers
    // are not safe to call from another thread. Yielding between notes is what
    // buys the repaint, the progress report and the cancel.
    struct Job {
        NoteCollection *collection = nullptr;
        QString format;
        QString destDir;
        QList<PlannedOutput> outputs;
        QPair<QString, QString> savedContext;
        QString combinedBody;   // html body fragments, or text/markdown
        QString error;
        int next = 0;
        int written = 0;
        bool singleFile = false;
        bool sawMath = false;
        bool sawMermaid = false;
        bool firstNote = true;
        bool cancelled = false;
    };
    bool startJob(NoteCollection *collection, const QStringList &relPaths,
                  const QString &destDir, const QString &format,
                  bool singleFile);
    void stepJob();
    void finishJob();
    // One note's contribution, shared by the synchronous and job paths.
    bool exportOneNote(NoteCollection *collection, const PlannedOutput &output,
                       const QString &format);
    // Appends one note to a combined document. Returns false when the
    // document budget is exhausted.
    bool appendCombinedNote(Job *job, const QString &relPath);
    bool writeCombined(Job *job);

    QString cssBlock() const;
    // A `query` fence's answer as static markup: the table or the board the
    // block shows on screen, evaluated once against the collection. Falls
    // back to the fence source when there is nothing to evaluate against,
    // and reports a spec error the way the block does rather than hiding it.
    QString queryHtml(const QString &spec) const;
    // The same answer as text: the header line, then the table or the board.
    // Falls back to the spec for the same reason queryHtml does.
    QString queryPlainText(const QString &spec) const;
    // An `![](url)` naming a web page rather than an image file: the preview
    // card, as a titled link. Never a broken <img> — the URL is a page.
    // `attributes` is the block's parsed <!--kvit …--> payload, which carries
    // the size the reader dragged the card to.
    QString embedCardHtml(const QString &url, const QString &alt,
                          const QString &caption,
                          const QString &attributes) const;
    // The same card as text: what it is called and where it points.
    QString embedCardPlainText(const QString &url, const QString &alt,
                               const QString &caption) const;
    QString dataUriForImagePath(const QString &storedPath) const;
    QString dataUriForMath(const QString &tex) const;
    // Rasterize a natively-supported Mermaid diagram to a PNG data URI at 2x
    // for the PDF path; empty when the source is invalid or an unsupported
    // family, so the caller falls back to escaped source.
    QString dataUriForMermaid(const QString &source) const;
    // Slugs for heading anchors, matching DocumentOutline (collision-suffixed).
    QStringList headingSlugs(const QList<Block::State> &blocks) const;

    Theme *m_theme = nullptr;
    const BlockKindRegistry *m_blockKinds = nullptr;
    // Guarded pointers: an export outlives neither, but it is re-pointed at
    // whichever collection a run was handed, and a collection that closes and
    // goes away must leave a query with nothing to evaluate rather than a
    // stale address to follow.
    QPointer<NoteCollection> m_collection;
    QPointer<EmbedMetadata> m_embedMetadata;
    QString m_noteDir;
    QString m_collectionRoot;
    QString m_liveRelPath;
    QString m_liveMarkdown;

    QString m_lastError;
    std::unique_ptr<Job> m_job;
    QTimer m_jobTimer;
    // 64 MiB of image, and 128 M characters of combined document (roughly
    // 256 MiB as QString). Both are far above any real note and far below
    // what exhausts a desktop.
    qint64 m_maxAttachmentBytes = 64LL * 1024 * 1024;
    qint64 m_maxCombinedChars = 128LL * 1024 * 1024;
};

#endif // DOCUMENTEXPORTER_H
