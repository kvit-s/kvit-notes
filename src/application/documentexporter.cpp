// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentexporter.h"
#include "inlinemarkdown.h"
#include "blockmodel.h"
#include "documentserializer.h"
#include "markdownformatter.h"
#include "imageassets.h"
#include "blockattributes.h"
#include "blockkinddef.h"
#include "blockkindregistry.h"
#include "blockkinds.h"
#include "htmlinline.h"
#include "rendercontext.h"
#include "blockkinds/blockstyle.h"
#include "blockkinds/blocktext.h"
#include "todometa.h"
#include "tabledata.h"
#include "kanbandata.h"
#include "querydata.h"
#include "embedmetadata.h"
#include "extensionregistry.h"
#include "codelanguages.h"
#include "mathrenderer.h"
#include "diagrams/mermaidrenderer.h"
#include "diagrams/diagrampainter.h"
#include "documentoutline.h"
#include "notecollection.h"
#include "notefileio.h"
#include "vaultpaths.h"
#include "theme.h"
#include "perflog.h"

#include <QFile>
#include <QTextStream>
#include <QTextDocument>
#include <QPdfWriter>
#include <QPageSize>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QByteArray>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QHash>
#include <QSet>
#include <QtMath>
#include <functional>

namespace {

// HTML escaping and inline-markdown rendering now live in kvit-content, so
// that every block kind can reach them: a kind writes its own markup and
// lives in kvit-domain, which cannot include anything up here. Pulled in
// under their old names, because the exporter still renders the pieces that
// span blocks and its assertions compare exact strings.
using HtmlInline::esc;
using HtmlInline::escFlowing;
using HtmlInline::renderInline;

// Pinned MathJax build for HTML export. Pinned exactly —
// exports are long-lived documents and must not change rendering when the
// CDN publishes a new minor. tex-svg renders self-measuring SVG and prints
// reliably; \( \) / \[ \] are MathJax 3's default delimiters, so no config
// block is needed, and $-delimiters stay disabled so prose dollars can
// never be misparsed by the viewer-side scanner.
const char kMathJaxScriptTag[] =
    "<script async id=\"MathJax-script\" "
    "src=\"https://cdn.jsdelivr.net/npm/mathjax@3.2.2/es5/tex-svg.min.js\">"
    "</script>\n";

// Pinned Mermaid ESM module for browser-targeted HTML export. Pinned to the
// exact reviewed version, not the floating @11 tag, so an exported document
// never changes rendering when the CDN publishes a new minor.
// securityLevel:'strict' keeps HTML tags encoded and disables click handlers;
// htmlLabels:false narrows label output; the limits mirror the native renderer.
// Update only through an explicit dependency/security review. Emitted once, only
// when the document actually contains a Mermaid block.
const char kMermaidScriptTag[] =
    "<script type=\"module\">\n"
    "  import mermaid from "
    "'https://cdn.jsdelivr.net/npm/mermaid@11.16.0/dist/mermaid.esm.min.mjs';\n"
    "  mermaid.initialize({\n"
    "    startOnLoad: true,\n"
    "    securityLevel: 'strict',\n"
    "    htmlLabels: false,\n"
    "    maxTextSize: 262144,\n"
    "    maxEdges: 2000\n"
    "  });\n"
    "</script>\n";

// ---- what the document renderer still owns ----
//
// A block's plain text and its markup are the block kind's, and each one
// writes its own. Two answers are not a block's to give, because they read
// the whole note: the table of contents, and the collision-suffixed anchor
// each heading gets. Both stay here.
//
// The text-shaping pieces several kinds needed — indenting a run of lines,
// laying out a column-aligned table — moved to BlockText, which is where a
// kind can reach them.

// A table of contents, regenerated from this document's headings and indented
// by level from the shallowest one present. The fence's own body is written
// by the editor as the reader types and is stale in a note nobody has opened,
// so the export reads the document rather than the block. This needs every
// block, which is why it belongs to the document renderer rather than to the
// toc block.
QString tableOfContentsPlainText(const QList<Block::State> &blocks)
{
    int minLevel = 4;
    for (const Block::State &block : blocks) {
        const int level = BlockKindDefs::forState(block)->headingLevel();
        if (level > 0)
            minLevel = qMin(minLevel, level);
    }
    QStringList out;
    for (const Block::State &block : blocks) {
        const int level = BlockKindDefs::forState(block)->headingLevel();
        if (level == 0)
            continue;
        out << QString(2 * (level - minLevel), QLatin1Char(' '))
                   + BlockText::renderedFully(block.content);
    }
    return out.join(QLatin1Char('\n'));
}

// The same list as markup: one anchor per heading, indented from the
// shallowest level present. The anchors are the collision-suffixed slugs the
// document renderer resolved, which is the other reason this cannot be the
// block's own work — a link into a heading has to name the same slug the
// heading wrote, and only a whole-document pass knows what that is.
QString tableOfContentsHtml(const QList<Block::State> &blocks,
                            const QStringList &slugs)
{
    int minLevel = 4;
    for (const Block::State &block : blocks) {
        const int level = BlockKindDefs::forState(block)->headingLevel();
        if (level > 0)
            minLevel = qMin(minLevel, level);
    }
    QString out = QStringLiteral("<ul class=\"toc\">");
    for (int i = 0; i < blocks.size(); ++i) {
        const Block::State &block = blocks.at(i);
        const int level = BlockKindDefs::forState(block)->headingLevel();
        if (level == 0)
            continue;
        out += "<li style=\"margin-left:"
             + QString::number((level - minLevel) * 16) + "px\">"
             + "<a href=\"#" + esc(i < slugs.size() ? slugs.at(i) : QString())
             + "\">" + esc(BlockText::renderedFully(block.content))
             + "</a></li>";
    }
    return out + QStringLiteral("</ul>");
}

QString tokenColor(CodeLanguages::Token t, Theme *theme)
{
    switch (t) {
    case CodeLanguages::Token::Keyword:
        return theme ? theme->codeKeyword().name() : QStringLiteral("#a626a4");
    case CodeLanguages::Token::Type:
        return theme ? theme->codeType().name() : QStringLiteral("#4078f2");
    case CodeLanguages::Token::String:
        return theme ? theme->codeString().name() : QStringLiteral("#50a14f");
    case CodeLanguages::Token::Comment:
        return theme ? theme->codeComment().name() : QStringLiteral("#a0a1a7");
    case CodeLanguages::Token::Number:
        return theme ? theme->codeNumber().name() : QStringLiteral("#986801");
    default:
        return QString();
    }
}

} // namespace

DocumentExporter::DocumentExporter(QObject *parent)
    : QObject(parent)
{
    // Zero-interval single shot: each note is rendered from a fresh turn of
    // the event loop, so everything queued behind it — repaints, the Cancel
    // click, a close request — runs in between.
    m_jobTimer.setSingleShot(true);
    m_jobTimer.setInterval(0);
    connect(&m_jobTimer, &QTimer::timeout, this, &DocumentExporter::stepJob);
}

DocumentExporter::~DocumentExporter() = default;

void DocumentExporter::setImageContext(const QString &noteDir,
                                       const QString &collectionRoot)
{
    m_noteDir = noteDir;
    m_collectionRoot = collectionRoot;
}

void DocumentExporter::setLiveNote(const QString &relPath, BlockModel *model)
{
    if (relPath.isEmpty() || !model) {
        clearLiveNote();
        return;
    }
    DocumentSerializer serializer;
    m_liveRelPath = relPath;
    m_liveMarkdown = serializer.serialize(model);
}

void DocumentExporter::clearLiveNote()
{
    m_liveRelPath.clear();
    m_liveMarkdown.clear();
}

QPair<QString, QString>
DocumentExporter::useImageContextFor(NoteCollection *collection,
                                     const QString &relPath)
{
    const QPair<QString, QString> previous{m_noteDir, m_collectionRoot};
    if (collection) {
        m_noteDir =
            QFileInfo(collection->absolutePath(relPath)).absolutePath();
        m_collectionRoot = collection->rootPath();
        // A query in one of these notes asks about the vault being exported,
        // which is this one however the exporter was wired at startup.
        m_collection = collection;
    }
    return previous;
}

QString DocumentExporter::bodyForExport(NoteCollection *collection,
                                        const QString &relPath) const
{
    const QString body = !m_liveRelPath.isEmpty() && relPath == m_liveRelPath
        ? m_liveMarkdown
        : (collection
               ? collection->noteInfo(relPath)
                     .value(QStringLiteral("body")).toString()
               : QString());
    return appendMarkdown(body, appendixFor(relPath));
}

// ---- what a module contributes to a note's export ----

QString DocumentExporter::appendMarkdown(const QString &body,
                                         const QString &extra)
{
    if (extra.isEmpty())
        return body;
    if (body.isEmpty())
        return extra;
    QString out = body;
    while (out.endsWith(QLatin1Char('\n')))
        out.chop(1);
    return out + QStringLiteral("\n\n") + extra;
}

QString DocumentExporter::resolveAppendixPaths(const QString &markdown,
                                               const QString &baseDir)
{
    if (baseDir.isEmpty() || markdown.isEmpty())
        return markdown;
    const QDir base(baseDir);
    QStringList lines = markdown.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        const ImageAssets::Parsed parsed = ImageAssets::classifyLine(line);
        if (!parsed.valid || parsed.path.isEmpty())
            continue;
        // A URL, a data: payload and an already-absolute path are all anchored
        // somewhere of their own; only a relative path needs the module's base.
        if (QFileInfo(parsed.path).isAbsolute()
            || parsed.path.startsWith(QLatin1String("http"), Qt::CaseInsensitive)
            || parsed.path.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)
            || parsed.path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
            continue;
        }
        // Keep the leading whitespace: an image block nested inside a list
        // serializes indented, and classifyLine answered on the trimmed line.
        qsizetype indent = 0;
        while (indent < line.size() && line.at(indent).isSpace())
            ++indent;
        line = line.left(indent)
            + ImageAssets::buildMarkdown(base.absoluteFilePath(parsed.path),
                                         parsed.alt, parsed.caption,
                                         parsed.width);
    }
    return lines.join(QLatin1Char('\n'));
}

QString DocumentExporter::appendixFor(const QString &relPath) const
{
    if (!m_extensions)
        return QString();
    QString out;
    for (const auto &contribution : m_extensions->exportContributions(relPath)) {
        out = appendMarkdown(out, resolveAppendixPaths(contribution.markdown,
                                                       contribution.baseDir));
    }
    return out;
}

QList<Block::State>
DocumentExporter::withLiveAppendix(QList<Block::State> blocks) const
{
    const QString appendix = liveNoteAppendix();
    if (appendix.isEmpty())
        return blocks;
    blocks.append(blocksFromMarkdown(appendix));
    return blocks;
}

QString DocumentExporter::extensionFor(const QString &format)
{
    if (format == QLatin1String("html")) return QStringLiteral("html");
    if (format == QLatin1String("pdf")) return QStringLiteral("pdf");
    if (format == QLatin1String("text")) return QStringLiteral("txt");
    return QStringLiteral("md");
}

const BlockKindDef *DocumentExporter::kindFor(const Block::State &state) const
{
    return m_blockKinds ? m_blockKinds->defFor(state)
                        : BlockKindDefs::forState(state);
}

// ---- block-list assembly ----

QList<Block::State> DocumentExporter::blocksFromModel(BlockModel *model) const
{
    QList<Block::State> out;
    if (!model)
        return out;
    out.reserve(model->count());
    for (int i = 0; i < model->count(); ++i) {
        Block *b = model->blockAt(i);
        if (!b)
            continue;
        out.append(b->state());
    }
    return out;
}

QList<Block::State> DocumentExporter::blocksFromMarkdown(const QString &markdown) const
{
    DocumentSerializer serializer;
    return serializer.parse(markdown);
}

QList<int> DocumentExporter::validIndexes(const QVariantList &indexes, int count)
{
    QList<int> out;
    out.reserve(indexes.size());
    for (const QVariant &value : indexes) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok && index >= 0 && index < count && !out.contains(index))
            out.append(index);
    }
    return out;
}

QList<Block::State> DocumentExporter::blocksAtIndexes(
    const QList<Block::State> &blocks, const QList<int> &indexes)
{
    QList<Block::State> out;
    out.reserve(indexes.size());
    for (const int index : indexes)
        out.append(blocks.at(index));
    return out;
}

QStringList DocumentExporter::stringsAtIndexes(const QStringList &strings,
                                                const QList<int> &indexes)
{
    QStringList out;
    out.reserve(indexes.size());
    for (const int index : indexes)
        out.append(strings.at(index));
    return out;
}

// ---- inline rendering ----

// ---- embedded resources ----

QString DocumentExporter::dataUriForImagePath(const QString &storedPath) const
{
    const QString resolved =
        ImageAssets::resolveSource(storedPath, m_noteDir, m_collectionRoot);
    if (resolved.isEmpty())
        return QString();
    if (resolved.startsWith(QLatin1String("http")))
        return resolved; // remote: reference directly
    const QString local = QUrl(resolved).toLocalFile();
    // Budget: the file is read whole and then Base64-expands by a third, and
    // an export can carry hundreds of them at once. An oversized attachment is
    // left out rather than allowed to exhaust memory; the rest of the note
    // exports normally.
    if (m_maxAttachmentBytes > 0
        && QFileInfo(local).size() > m_maxAttachmentBytes)
        return QString();
    QFile f(local);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray bytes = f.readAll();
    QString mime = QStringLiteral("image/png");
    const QString lower = local.toLower();
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) mime = "image/jpeg";
    else if (lower.endsWith(".gif")) mime = "image/gif";
    else if (lower.endsWith(".svg")) mime = "image/svg+xml";
    else if (lower.endsWith(".webp")) mime = "image/webp";
    return "data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64());
}

QString DocumentExporter::dataUriForMath(const QString &tex) const
{
    const QColor fg = m_theme ? m_theme->textPrimary() : QColor(QStringLiteral("#222222"));
    QString error;
    const int textSize = 18;
    const int verticalPadding = qMax(2, qCeil(textSize * 0.12));
    const QImage img = MathRenderer::render(
        tex, textSize, fg, 2.0, &error, verticalPadding,
        MathRenderer::sideBearingPaddingPx(textSize));
    if (img.isNull())
        return QString();
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}

QString DocumentExporter::dataUriForMermaid(const QString &source) const
{
    Diagram::LayoutOptions opts;
    opts.fontFamily = QStringLiteral("sans-serif");
    opts.fontPixelSize = 15;
    const Diagram::RenderResult r = Diagram::render(source, opts);
    if (!r.valid || r.scene.isEmpty())
        return QString();   // invalid or unsupported: caller falls back to source

    const qreal dpr = 2.0;
    const int w = qMax(1, qCeil(r.scene.bounds.width() * dpr));
    const int h = qMax(1, qCeil(r.scene.bounds.height() * dpr));
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(m_theme ? m_theme->windowBackground() : QColor(Qt::white));

    Diagram::SceneColors colors;
    colors.background = Qt::transparent;
    colors.nodeFill = m_theme ? m_theme->chipBackground() : QColor("#eef3fb");
    colors.nodeStroke = m_theme ? m_theme->accent() : QColor("#4b6ea8");
    colors.edge = m_theme ? m_theme->textSecondary() : QColor("#4b5563");
    colors.label = m_theme ? m_theme->textPrimary() : QColor("#1f2937");
    colors.edgeLabel = m_theme ? m_theme->textMuted() : QColor("#374151");
    colors.edgeLabelBackground = m_theme ? m_theme->windowBackground() : QColor(Qt::white);
    colors.subgraphFill = m_theme ? m_theme->blockHoverTint() : QColor(0, 0, 0, 12);
    colors.subgraphStroke = m_theme ? m_theme->border() : QColor("#94a3b8");

    QPainter p(&img);
    p.scale(dpr, dpr);
    Diagram::paintScene(&p, r.scene, colors, opts.fontFamily);
    p.end();

    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}

// ---- slugs (mirror DocumentOutline) ----

QStringList DocumentExporter::headingSlugs(const QList<Block::State> &blocks) const
{
    QStringList slugs;
    QHash<QString, int> counts;
    for (const Block::State &b : blocks) {
        const bool heading = b.type == Block::Heading1 || b.type == Block::Heading2
                          || b.type == Block::Heading3 || b.type == Block::Heading4;
        if (!heading) {
            slugs.append(QString());
            continue;
        }
        const QString base =
            DocumentOutline::baseSlug(InlineMarkdown::displayText(b.content));
        const int seen = counts.value(base, 0);
        counts.insert(base, seen + 1);
        slugs.append(seen == 0 ? base
                               : base + QLatin1Char('-') + QString::number(seen));
    }
    return slugs;
}

// ---- CSS ----

QString DocumentExporter::cssBlock() const
{
    const QString fg = m_theme ? m_theme->textPrimary().name() : QStringLiteral("#222222");
    const QString muted = m_theme ? m_theme->textMuted().name() : QStringLiteral("#666666");
    const QString bg = m_theme ? m_theme->windowBackground().name() : QStringLiteral("#ffffff");
    const QString accent = m_theme ? m_theme->accent().name() : QStringLiteral("#2970c8");
    const QString border = m_theme ? m_theme->border().name() : QStringLiteral("#dddddd");
    const QString codeBg = m_theme ? m_theme->codePanelBackground().name()
                                   : QStringLiteral("#f4f4f2");
    const QString danger = m_theme ? m_theme->danger().name()
                                   : QStringLiteral("#c1121f");
    const QString highlight = m_theme ? m_theme->highlightBackground().name()
                                      : QStringLiteral("#fdf3a9");
    return QStringLiteral(
        "body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;"
        "font-size:15px;line-height:1.6;color:%1;background:%2;"
        "max-width:760px;margin:24px auto;padding:0 16px}"
        "h1,h2,h3,h4{line-height:1.25;margin:1.2em 0 .4em}"
        "a{color:%3;text-decoration:none}a:hover{text-decoration:underline}"
        "code{font-family:monospace;background:%6;padding:1px 4px;border-radius:3px}"
        "pre{background:%6;padding:12px;border-radius:6px;overflow:auto}"
        "pre code{background:none;padding:0}"
        "blockquote{border-left:3px solid %5;margin:0;padding:2px 12px;color:%4}"
        "table{border-collapse:collapse;margin:8px 0}"
        "th,td{border:1px solid %5;padding:5px 9px}th{background:%6}"
        "hr{border:none;border-top:1px solid %5;margin:1.5em 0}"
        // Plain "100%": QString::arg has no escape for a percent sign, so a
        // doubled one reaches the stylesheet as it was written and the whole
        // declaration is discarded — which is how an oversized image came to
        // run off the page in every export.
        "img{max-width:100%}"
        ".callout{border:1px solid %5;border-left:4px solid %3;border-radius:6px;"
        "padding:8px 12px;margin:10px 0}"
        ".callout .title{font-weight:bold;margin-bottom:4px}"
        ".kanban{display:flex;gap:12px;align-items:flex-start;flex-wrap:wrap}"
        ".kanban .col{border:1px solid %5;"
        "border-radius:6px;padding:8px;min-width:140px}"
        ".kanban .col .count{color:%4;font-weight:normal;font-size:12px}"
        ".kanban .card{border:1px solid %5;border-radius:4px;padding:4px 6px;"
        "margin:5px 0}"
        ".kanban .card .title{font-weight:bold}"
        ".kanban .card .meta{color:%4;font-size:12px}"
        ".kanban .card .chip{border:1px solid %5;border-radius:8px;"
        "padding:0 6px;margin-right:4px;font-size:11px;color:%4}"
        // A query block's answer, rendered as it stands at export time: a
        // table or a board, under a line saying what it is and how many notes
        // matched, so a reader can tell a query's output from a hand-written
        // table.
        ".query{border:1px solid %5;border-radius:6px;padding:8px 10px;"
        "margin:10px 0}"
        ".query .head{font-size:11px;font-weight:bold;color:%4;"
        "margin-bottom:6px}"
        ".query .head .count{font-weight:normal}"
        ".query .error{color:%7}"
        ".query table{margin:0}"
        // An embed card: a titled link to the page, with whatever the preview
        // cache already knew about it.
        ".embed{border:1px solid %5;border-radius:6px;padding:8px 12px;"
        "margin:10px 0}"
        ".embed .title{font-weight:bold}"
        ".embed .desc{color:%4;font-size:13px;margin-top:2px}"
        ".embed .host{color:%4;font-size:12px;margin-top:4px}"
        // The theme's own highlight tint, with the theme's text colour on it.
        // A fixed pale yellow was legible under the light themes only: the
        // dark ones export near-white body text, which on that yellow is
        // barely readable.
        "mark{background:%8;color:%1}"
        ".math-display{text-align:center;margin:1em 0}"
        // A code span in a table cell can hold a folded-in listing, whose
        // indentation HTML would otherwise collapse. The line breaks are
        // emitted as <br> rather than left to this, so a renderer that
        // ignores white-space still gets the lines.
        "td code,th code{white-space:pre-wrap}"
        "pre.text-diagram{line-height:1.2}"
        "pre.text-diagram code{white-space:pre;font-family:"
        "'Cascadia Code',Consolas,'DejaVu Sans Mono',monospace}"
        "pre.mermaid{background:none;padding:0;text-align:center}"
        ".diagram-source{margin:2px 0 10px;color:%4;font-size:13px}"
        ".diagram-source pre{margin-top:4px}"
        // ---- presentation attributes ----
        // An image is centred in the editor unless its block says otherwise,
        // and its caption sits under it; the figure default states both, so
        // only an explicit align= writes an inline rule.
        "figure{margin:1em 0;text-align:center}"
        "figcaption{color:%4;font-size:13px;font-style:italic;margin-top:4px}"
        // The enlarged initial of a paragraph carrying dropcap=<lines>. The
        // size, colour and family are inline, because they are per block.
        // float+line-height is what makes the following lines wrap around it.
        ".dropcap{float:left;line-height:0.82;font-weight:bold;"
        "padding:0.06em 0.08em 0 0}"
        // An image whose block asks for a border with no colour of its own.
        // The colour is named here, once, because this is where the theme is
        // in hand; a block kind cannot reach it.
        "img.bordered{border:1px solid %5}"
        // A decorative divider: a centred diamond flanked by two rules.
        ".hr-deco{display:flex;align-items:center;gap:10px;margin:1.5em 0}"
        ".hr-deco hr{flex:1;margin:0}"
        ).arg(fg, bg, accent, muted, border, codeBg).arg(danger).arg(highlight);
}

// ---- query blocks ----

QString DocumentExporter::queryHtml(const QString &spec) const
{
    const QueryData::ParseResult parsed = QueryData::parse(spec);
    if (!parsed.ok) {
        // The block refuses to guess at a spec it cannot parse and shows the
        // error; the export says the same thing, and keeps the spec beside it
        // so the reader can see what was wrong with it.
        return "<div class=\"query\"><div class=\"head\">" + esc(tr("Query"))
             + "</div><div class=\"error\">" + esc(parsed.error)
             + "</div><pre><code>" + esc(spec) + "</code></pre></div>";
    }
    if (!m_collection || !m_collection->isOpen()) {
        // No vault to ask. Writing an empty table would claim the query
        // matched nothing, which is a different statement from not having
        // run it, so the spec goes out as source.
        return "<pre><code>" + esc(spec) + "</code></pre>";
    }

    const QueryData::Result result =
        QueryData::evaluate(parsed.spec, *m_collection);
    const QString head = "<div class=\"head\">" + esc(tr("Query"))
        + " <span class=\"count\">"
        + esc(tr("%1 notes").arg(result.rows.size())) + "</span></div>";

    QString out = "<div class=\"query\">" + head;
    if (parsed.spec.view == QueryData::View::Board) {
        // The same columns-of-cards shape a task board exports as, so the two
        // read alike: a group heading with its count, then one card per note
        // with its first column as the card's title.
        out += QStringLiteral("<div class=\"kanban\">");
        for (const QueryData::Group &group : result.groups) {
            out += "<div class=\"col\"><strong>" + esc(group.name)
                 + "</strong> <span class=\"count\">"
                 + QString::number(group.rows.size()) + "</span>";
            for (const QueryData::Row &row : group.rows) {
                out += QStringLiteral("<div class=\"card\">");
                for (int i = 0; i < row.cells.size(); ++i) {
                    const QString &cell = row.cells.at(i);
                    if (cell.isEmpty())
                        continue;
                    out += (i == 0 ? "<div class=\"title\">"
                                   : "<div class=\"meta\">")
                         + esc(cell) + "</div>";
                }
                out += QStringLiteral("</div>");
            }
            out += QStringLiteral("</div>");
        }
        out += QStringLiteral("</div>");
    } else {
        out += QStringLiteral("<table><tr>");
        for (const QString &column : result.columns)
            out += "<th>" + esc(column) + "</th>";
        out += QStringLiteral("</tr>");
        for (const QueryData::Row &row : result.rows) {
            out += QStringLiteral("<tr>");
            // Every column gets a cell even when the note has nothing under
            // that key, so the rows stay square.
            for (int i = 0; i < result.columns.size(); ++i)
                out += "<td>"
                     + esc(i < row.cells.size() ? row.cells.at(i) : QString())
                     + "</td>";
            out += QStringLiteral("</tr>");
        }
        out += QStringLiteral("</table>");
    }
    out += QStringLiteral("</div>");
    return out;
}

QString DocumentExporter::queryPlainText(const QString &spec) const
{
    const QueryData::ParseResult parsed = QueryData::parse(spec);
    if (!parsed.ok) {
        return tr("Query") + QStringLiteral(": ") + parsed.error
             + QLatin1Char('\n') + BlockText::indent(spec, 2);
    }
    if (!m_collection || !m_collection->isOpen()) {
        // No vault to ask. An empty table would claim the query matched
        // nothing, which is a different statement from never having run it,
        // so the spec goes out as source — as the HTML export does.
        return spec;
    }

    const QueryData::Result result =
        QueryData::evaluate(parsed.spec, *m_collection);
    QStringList out;
    out << tr("Query") + QStringLiteral(" — ")
             + tr("%1 notes").arg(result.rows.size());
    if (parsed.spec.view == QueryData::View::Board) {
        for (const QueryData::Group &group : result.groups) {
            out << QString();
            out << group.name + QStringLiteral(" (")
                       + QString::number(group.rows.size()) + QLatin1Char(')');
            for (const QueryData::Row &row : group.rows) {
                for (int i = 0; i < row.cells.size(); ++i) {
                    const QString &cell = row.cells.at(i);
                    if (cell.isEmpty())
                        continue;
                    out << QString(i == 0 ? 2 : 4, QLatin1Char(' ')) + cell;
                }
            }
        }
    } else {
        QList<QStringList> rows;
        rows.reserve(result.rows.size());
        for (const QueryData::Row &row : result.rows)
            rows << row.cells;
        const QString table = BlockText::alignedTable(result.columns, rows);
        if (!table.isEmpty())
            out << table;
    }
    return out.join(QLatin1Char('\n'));
}

// ---- embed cards ----

QString DocumentExporter::embedCardHtml(const QString &url, const QString &alt,
                                        const QString &caption,
                                        const QString &attributes) const
{
    const BlockStyle::Attributes attrs = BlockStyle::parse(attributes);
    // Whatever the preview cache already holds for this URL. An export never
    // fetches: it runs on the reader's command over notes they may not have
    // opened, and reaching the network from here would send a list of the
    // sites a vault mentions to those sites.
    const QVariantMap meta = m_embedMetadata
        ? m_embedMetadata->cachedMetadata(url) : QVariantMap();
    const QString pageTitle = meta.value(QStringLiteral("title")).toString();
    const QString description =
        meta.value(QStringLiteral("description")).toString();
    // What the link is called: the alt text the note gave it, else the page
    // title from the cache, else the URL, which always says something.
    const QString label = !alt.isEmpty() ? alt
        : (!pageTitle.isEmpty() ? pageTitle : url);
    const QString host = QUrl(url).host();

    // The card's stored size, which the reader set by dragging its corner.
    // A width alone is enough to reproduce the shape of the card; the height
    // on screen is the player's, and forcing it on a static link would leave a
    // tall empty box.
    QStringList declarations;
    const int storedWidth = BlockStyle::num(attrs, QLatin1String("width"), 0);
    if (storedWidth > 0)
        declarations << QStringLiteral("max-width:%1px").arg(storedWidth);

    // The card's URL comes out of the note, so it is the note author's choice
    // and the export is read in a browser: a target that runs rather than
    // navigates keeps its title and loses its link.
    const QString href = HtmlInline::safeHref(url);
    const QString title = href.isEmpty()
        ? esc(label)
        : "<a href=\"" + esc(href) + "\">" + esc(label) + "</a>";
    QString out = "<div class=\"embed\"" + BlockStyle::styleAttr(declarations)
        + "><div class=\"title\">" + title + "</div>";
    if (!description.isEmpty())
        out += "<div class=\"desc\">" + esc(description) + "</div>";
    if (!caption.isEmpty())
        out += "<div class=\"desc\">" + escFlowing(caption) + "</div>";
    if (!host.isEmpty() && label != url)
        out += "<div class=\"host\">" + esc(host) + "</div>";
    return out + "</div>";
}

QString DocumentExporter::embedCardPlainText(const QString &url,
                                             const QString &alt,
                                             const QString &caption) const
{
    const QVariantMap meta = m_embedMetadata
        ? m_embedMetadata->cachedMetadata(url) : QVariantMap();
    const QString pageTitle = meta.value(QStringLiteral("title")).toString();
    const QString label = !alt.isEmpty() ? alt
        : (!pageTitle.isEmpty() ? pageTitle : QString());

    QString out = QStringLiteral("[embed");
    if (!label.isEmpty())
        out += QStringLiteral(": ") + label;
    out += QStringLiteral("] ") + url;
    if (!caption.isEmpty())
        out += QLatin1Char('\n')
             + BlockText::indent(BlockText::renderedFully(caption), 2);
    return out;
}

// ---- the HTML builder ----

// The services a block kind renders through: the exporter, answering the
// questions a kind cannot answer for itself.
//
// Built per render because two of its answers read the whole note. The rest
// forward to the exporter, which holds the theme, the open collection, the
// embed cache, the image context and the attachment budget.
class DocumentExporter::RenderServices : public BlockRenderServices
{
public:
    RenderServices(const DocumentExporter *exporter,
                   const QList<Block::State> *blocks,
                   const QStringList *slugs, bool mathJax, bool browserTarget)
        : m_exporter(exporter)
        , m_blocks(blocks)
        , m_slugs(slugs)
        , m_mathJax(mathJax)
        , m_browserTarget(browserTarget)
    {
        Q_UNUSED(m_mathJax);
        Q_UNUSED(m_browserTarget);
    }

    QString imageDataUri(const QString &storedPath) const override
    {
        return m_exporter->dataUriForImagePath(storedPath);
    }
    QString mathDataUri(const QString &tex) const override
    {
        return m_exporter->dataUriForMath(tex);
    }
    QString mermaidDataUri(const QString &source) const override
    {
        return m_exporter->dataUriForMermaid(source);
    }

    // The inner html of a <pre><code> block: the code fence's body with one
    // theme-coloured span per highlighted token. The wrapper is the code
    // kind's, because the kind is what decides it wants a <pre> at all.
    QString highlightedCodeHtml(const QString &language,
                                const QString &source) const override
    {
        QString code;
        const QList<CodeLanguages::Span> spans =
            CodeLanguages::highlightSpans(language, source);
        int pos = 0;
        for (const CodeLanguages::Span &span : spans) {
            if (span.start > pos)
                code += esc(source.mid(pos, span.start - pos));
            const QString color = tokenColor(span.token, m_exporter->m_theme);
            const QString piece = esc(source.mid(span.start, span.length));
            code += color.isEmpty()
                ? piece
                : ("<span style=\"color:" + color + "\">" + piece + "</span>");
            pos = span.start + span.length;
        }
        if (pos < source.length())
            code += esc(source.mid(pos));
        return code;
    }

    QString queryHtml(const QString &spec) const override
    {
        return m_exporter->queryHtml(spec);
    }
    QString queryPlainText(const QString &spec) const override
    {
        return m_exporter->queryPlainText(spec);
    }

    QString embedCardHtml(const QString &url, const QString &alt,
                          const QString &caption,
                          const QString &attributes) const override
    {
        return m_exporter->embedCardHtml(url, alt, caption, attributes);
    }
    QString embedCardPlainText(const QString &url, const QString &alt,
                               const QString &caption) const override
    {
        return m_exporter->embedCardPlainText(url, alt, caption);
    }

    QString tableOfContentsHtml() const override
    {
        return ::tableOfContentsHtml(*m_blocks, *m_slugs);
    }
    QString tableOfContentsPlainText() const override
    {
        return ::tableOfContentsPlainText(*m_blocks);
    }

private:
    const DocumentExporter *const m_exporter;
    const QList<Block::State> *const m_blocks;
    const QStringList *const m_slugs;
    const bool m_mathJax;
    const bool m_browserTarget;
};

QString DocumentExporter::buildHtmlBody(const QList<Block::State> &blocks,
                                        const QStringList &blockSlugs,
                                        const QList<Block::State> &documentBlocks,
                                        const QStringList &documentSlugs,
                                        bool browserTarget,
                                        bool *sawMathOut,
                                        bool *sawMermaidOut) const
{
    // Browser-targeted HTML leaves the TeX in the document for MathJax by
    // default; KVIT_MATH_RENDER=png forces PNG embeds (the escape hatch for
    // fully self-contained/offline exports). The PDF seam never uses
    // MathJax — QTextDocument runs no JavaScript.
    const QString mathMode =
        qEnvironmentVariable("KVIT_MATH_RENDER").trimmed().toLower();
    const bool mathJax = browserTarget && mathMode != QLatin1String("png");

    // RenderServices deliberately sees the complete note even when `blocks`
    // is only a selected subset. A TOC is a projection of the document, not
    // of the rows being emitted. The per-output slug list is separate so a
    // selected duplicate heading keeps its original collision suffix.
    const RenderServices services(this, &documentBlocks, &documentSlugs, mathJax,
                                  browserTarget);
    RenderContext ctx;
    ctx.target = browserTarget ? RenderContext::Browser : RenderContext::Pdf;
    ctx.mathJax = mathJax;
    ctx.services = &services;

    QString body;
    for (int i = 0; i < blocks.size(); ) {
        const Block::State &b = blocks.at(i);

        if (Block::isListFamily(b.type)) {
            // Collect the contiguous list run and emit it with its nesting:
            // an item deeper than the one before opens a sublist INSIDE the
            // still-open <li>, which is where HTML wants a nested list. The
            // closing </li> is deferred for exactly that reason, so a flat
            // run still emits <ul><li>a</li><li>b</li></ul> unchanged.
            //
            // This is the document renderer's work rather than any one
            // block's: an item cannot see its neighbours, and what opens a
            // list is the item before it not being in one.
            const bool ordered = b.type == Block::NumberedList;
            const QString openTag = ordered ? QStringLiteral("<ol>")
                                            : QStringLiteral("<ul>");
            const QString closeTag = ordered ? QStringLiteral("</ol>")
                                             : QStringLiteral("</ul>");
            int depth = 0;   // lists currently open in this run
            while (i < blocks.size() && Block::isListFamily(blocks.at(i).type)
                   && (blocks.at(i).type == Block::NumberedList) == ordered) {
                const Block::State &item = blocks.at(i);
                const int target = qMax(0, item.indentLevel) + 1;
                if (depth == 0) {
                    while (depth < target) { body += openTag; ++depth; }
                } else if (target > depth) {
                    while (depth < target) { body += openTag; ++depth; }
                } else {
                    body += QStringLiteral("</li>");
                    while (depth > target) {
                        body += closeTag + QStringLiteral("</li>");
                        --depth;
                    }
                }
                // The item's own markup, with no <li> around it: the wrapper
                // and its deferred closer belong to the run.
                body += "<li>" + kindFor(item)->toHtml(item, ctx);
                ++i;
            }
            if (depth > 0) {
                body += QStringLiteral("</li>");
                while (depth > 0) {
                    body += closeTag;
                    --depth;
                    if (depth > 0)
                        body += QStringLiteral("</li>");
                }
            }
            continue;
        }

        // Everything else is one block's markup, written by the kind. This
        // was a switch over the block type with a `default:` label, and
        // inside its code-fence case a chain of comparisons against fence
        // language strings. A kind that reached neither — a `query` fence,
        // the last one added — exported as a listing of its own source spec.
        ctx.headingSlug = blockSlugs.at(i);
        body += kindFor(b)->toHtml(b, ctx);
        ++i;
    }

    if (sawMathOut)
        *sawMathOut = ctx.sawMath;
    if (sawMermaidOut)
        *sawMermaidOut = ctx.sawMermaid;
    return body;
}

QString DocumentExporter::wrapHtmlDocument(const QString &body,
                                           const QString &title,
                                           bool browserTarget, bool sawMath,
                                           bool sawMermaid) const
{
    const QString mathMode =
        qEnvironmentVariable("KVIT_MATH_RENDER").trimmed().toLower();
    const bool mathJax = browserTarget && mathMode != QLatin1String("png");

    // One script tag each, only when the document actually contains that
    // content — a math/mermaid-free export carries no network dependency at all.
    const QString mathJaxTag = (mathJax && sawMath)
        ? QString::fromLatin1(kMathJaxScriptTag) : QString();
    // The browser renders Mermaid; the PDF path rasterizes natively and never
    // injects the module.
    const QString mermaidTag = (browserTarget && sawMermaid)
        ? QString::fromLatin1(kMermaidScriptTag) : QString();

    return QStringLiteral(
        "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n<title>%1</title>\n"
        "<style>%2</style>\n%3%5</head>\n<body>\n%4\n</body></html>\n")
        .arg(esc(title.isEmpty() ? QStringLiteral("Kvit Export") : title),
             cssBlock(), mathJaxTag, body, mermaidTag);
}

QString DocumentExporter::buildHtml(const QList<Block::State> &blocks,
                                    const QString &title,
                                    bool browserTarget) const
{
    const QStringList slugs = headingSlugs(blocks);
    return buildHtmlWithContext(blocks, slugs, blocks, slugs, title,
                                browserTarget);
}

QString DocumentExporter::buildHtmlWithContext(
    const QList<Block::State> &blocks, const QStringList &blockSlugs,
    const QList<Block::State> &documentBlocks,
    const QStringList &documentSlugs, const QString &title,
    bool browserTarget) const
{
    bool sawMath = false;
    bool sawMermaid = false;
    const QString body = buildHtmlBody(blocks, blockSlugs, documentBlocks,
                                       documentSlugs, browserTarget,
                                       &sawMath, &sawMermaid);
    return wrapHtmlDocument(body, title, browserTarget, sawMath, sawMermaid);
}

QString DocumentExporter::htmlForModel(BlockModel *model, const QString &title) const
{
    return buildHtml(withLiveAppendix(blocksFromModel(model)), title, true);
}

QString DocumentExporter::htmlForMarkdown(const QString &markdown,
                                          const QString &title) const
{
    return buildHtml(blocksFromMarkdown(markdown), title, true);
}

QString DocumentExporter::htmlForModelBlocks(
    BlockModel *model, const QVariantList &indexes, const QString &title) const
{
    const QList<Block::State> documentBlocks = blocksFromModel(model);
    const QList<int> selected = validIndexes(indexes, documentBlocks.size());
    const QStringList documentSlugs = headingSlugs(documentBlocks);
    return buildHtmlWithContext(blocksAtIndexes(documentBlocks, selected),
                                stringsAtIndexes(documentSlugs, selected),
                                documentBlocks, documentSlugs, title, true);
}

// ---- plain text ----

QString DocumentExporter::buildPlainText(
    const QList<Block::State> &blocks,
    const QList<Block::State> &documentBlocks) const
{
    // Plain text never rasterises and has no MathJax, so the context it
    // renders under is the PDF one; only the services and the ordinal are
    // read from it.
    const QStringList noSlugs;
    const RenderServices services(this, &documentBlocks, &noSlugs, false, false);
    RenderContext ctx;
    ctx.target = RenderContext::Pdf;
    ctx.mathJax = false;
    ctx.services = &services;

    QStringList lines;
    // One numbering counter per indent level, the same rule
    // BlockModel::ordinalAt keeps: a block at level L resets the deeper
    // levels, a non-numbered block at L resets L itself, and anything outside
    // the list family resets all of them. A single flat counter — what this
    // used to keep — numbers a two-level list 1,2,3,4 instead of 1,2 / 1,2.
    // It stays here rather than moving onto the list kind, because it is a
    // document-level count and an item cannot see the items above it.
    int counters[Block::MaxIndentLevel + 1] = {0};
    for (const Block::State &b : blocks) {
        if (!Block::isListFamily(b.type)) {
            for (int level = 0; level <= Block::MaxIndentLevel; ++level)
                counters[level] = 0;
            ctx.ordinal = 1;
        } else {
            const int level = qBound(0, b.indentLevel, Block::MaxIndentLevel);
            for (int deeper = level + 1; deeper <= Block::MaxIndentLevel; ++deeper)
                counters[deeper] = 0;
            if (b.type == Block::NumberedList) {
                ctx.ordinal = ++counters[level];
            } else {
                counters[level] = 0;
                ctx.ordinal = 1;
            }
        }

        // A block whose text is empty writes no line, but still gets the
        // blank line after it, which is what keeps an empty paragraph a
        // paragraph-shaped gap rather than nothing at all.
        const QString text = kindFor(b)->toPlainText(b, ctx);
        if (!text.isEmpty())
            lines << text;
        lines << QString();
    }
    return lines.join(QLatin1Char('\n'));
}

QString DocumentExporter::plainTextForModel(BlockModel *model) const
{
    const QList<Block::State> blocks =
        withLiveAppendix(blocksFromModel(model));
    return buildPlainText(blocks, blocks);
}

QString DocumentExporter::plainTextForMarkdown(const QString &markdown) const
{
    const QList<Block::State> blocks = blocksFromMarkdown(markdown);
    return buildPlainText(blocks, blocks);
}

QString DocumentExporter::plainTextForModelBlocks(
    BlockModel *model, const QVariantList &indexes) const
{
    const QList<Block::State> documentBlocks = blocksFromModel(model);
    const QList<int> selected = validIndexes(indexes, documentBlocks.size());
    return buildPlainText(blocksAtIndexes(documentBlocks, selected),
                          documentBlocks);
}

// ---- write to disk ----

namespace {
bool writeText(const QString &path, const QString &content)
{
    return NoteFileIo::writeTextFileAtomic(path, content);
}
}

bool DocumentExporter::htmlToPdf(const QString &html, const QString &path)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(96);
    QTextDocument doc;
    doc.setHtml(html);
    doc.setPageSize(QSizeF(writer.width(), writer.height()));
    doc.print(&writer);
    return QFileInfo(path).size() > 0;
}

bool DocumentExporter::writeMarkdownAs(const QString &markdown,
                                       const QString &title,
                                       const QString &format, const QString &path)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("export.run"),
        QVariantMap{
            {QStringLiteral("format"), format},
            {QStringLiteral("path"), path},
            {QStringLiteral("markdownChars"), markdown.size()},
        });
    if (format == QLatin1String("markdown"))
        return writeText(path, markdown);
    if (format == QLatin1String("html"))
        return writeText(path, htmlForMarkdown(markdown, title));
    if (format == QLatin1String("text"))
        return writeText(path, plainTextForMarkdown(markdown));
    if (format == QLatin1String("pdf"))
        return htmlToPdf(
            buildHtml(blocksFromMarkdown(markdown), title, false), path);
    return false;
}

bool DocumentExporter::writeModel(BlockModel *model, const QString &title,
                                  const QString &format, const QString &path)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("export.run"),
        QVariantMap{
            {QStringLiteral("format"), format},
            {QStringLiteral("path"), path},
            {QStringLiteral("blocks"), model ? model->count() : 0},
        });
    if (format == QLatin1String("markdown")) {
        DocumentSerializer serializer;
        return writeText(path, appendMarkdown(serializer.serialize(model),
                                              liveNoteAppendix()));
    }
    if (format == QLatin1String("html"))
        return writeText(path, htmlForModel(model, title));
    if (format == QLatin1String("text"))
        return writeText(path, plainTextForModel(model));
    if (format == QLatin1String("pdf"))
        return htmlToPdf(
            buildHtml(withLiveAppendix(blocksFromModel(model)), title, false),
            path);
    return false;
}

// A block-scope export carries no module contribution, and that is a decision
// rather than an omission. The reader picked particular blocks out of a note; a
// module's contribution is about the note, and it is not one of the blocks they
// picked. The three whole-note scopes carry it, and the export dialog names the
// contribution for those and not for this one.
bool DocumentExporter::writeModelBlocks(BlockModel *model,
                                        const QVariantList &indexes,
                                        const QString &title,
                                        const QString &format,
                                        const QString &path)
{
    const QList<Block::State> documentBlocks = blocksFromModel(model);
    const QList<int> selected = validIndexes(indexes, documentBlocks.size());
    const QList<Block::State> blocks = blocksAtIndexes(documentBlocks, selected);

    PerfLog::ScopedTimer perf(
        QStringLiteral("export.run"),
        QVariantMap{
            {QStringLiteral("format"), format},
            {QStringLiteral("path"), path},
            {QStringLiteral("blocks"), blocks.size()},
        });

    if (format == QLatin1String("markdown")) {
        DocumentSerializer serializer;
        return writeText(path, serializer.serializeBlocks(model, indexes));
    }
    if (format == QLatin1String("text"))
        return writeText(path, buildPlainText(blocks, documentBlocks));

    const QStringList documentSlugs = headingSlugs(documentBlocks);
    const QString html = buildHtmlWithContext(
        blocks, stringsAtIndexes(documentSlugs, selected), documentBlocks,
        documentSlugs, title, format != QLatin1String("pdf"));
    if (format == QLatin1String("html"))
        return writeText(path, html);
    if (format == QLatin1String("pdf"))
        return htmlToPdf(html, path);
    return false;
}

// ---- output plan ----

QString DocumentExporter::canonicalTarget(const QString &path)
{
    if (path.isEmpty())
        return QString();
    const QFileInfo info(path);
    const QString existing = info.canonicalFilePath();
    if (!existing.isEmpty())
        return existing;

    // Not on disk yet. Resolve the deepest ancestor that does exist and hang
    // the rest off it, so a destination under a symlinked directory still
    // compares equal to the note it aliases.
    QString tail = info.fileName();
    QDir dir = info.absoluteDir();
    while (true) {
        const QString canonicalDir =
            QFileInfo(dir.absolutePath()).canonicalFilePath();
        if (!canonicalDir.isEmpty()) {
            return QDir::cleanPath(canonicalDir + QLatin1Char('/') + tail);
        }
        const QString name = dir.dirName();
        if (name.isEmpty() || !dir.cdUp())
            return QDir::cleanPath(info.absoluteFilePath());
        tail = name + QLatin1Char('/') + tail;
    }
}

bool DocumentExporter::isInsideDirectory(const QString &canonicalPath,
                                         const QString &canonicalDir)
{
    if (canonicalPath.isEmpty() || canonicalDir.isEmpty())
        return false;
    if (canonicalPath == canonicalDir)
        return true;
    const QString prefix = canonicalDir.endsWith(QLatin1Char('/'))
        ? canonicalDir
        : canonicalDir + QLatin1Char('/');
    return canonicalPath.startsWith(prefix);
}

DocumentExporter::OutputPlan
DocumentExporter::buildOutputPlan(NoteCollection *collection,
                                  const QStringList &relPaths,
                                  const QString &destDir,
                                  const QString &format,
                                  bool singleFile) const
{
    OutputPlan plan;
    plan.destDir = destDir;
    plan.singleFile = singleFile;

    if (!collection || !collection->isOpen() || relPaths.isEmpty()) {
        plan.error = tr("There is nothing to export.");
        return plan;
    }
    if (destDir.isEmpty()) {
        plan.error = tr("No destination was chosen.");
        return plan;
    }

    // Every output path is built by joining the destination directory with a
    // note's vault-relative path, so a relative path that walks upward names a
    // file outside the destination the reader chose. The scan produces only
    // plain paths and the note list only passes those on, which is why this
    // has never mattered; it is checked here anyway, because a caller getting
    // it wrong writes over a file nobody in this conversation named.
    for (const QString &rel : relPaths) {
        if (!VaultPaths::isPlainRelativePath(rel)) {
            plan.error = tr("\"%1\" is not a note in this collection.").arg(rel);
            return plan;
        }
    }

    const QString ext = extensionFor(format);
    const QString canonicalDest = canonicalTarget(destDir);
    const QString canonicalRoot = canonicalTarget(collection->rootPath());

    // The sources an output could land on, resolved once, so an output can
    // be compared against all of them and not merely against the note it
    // came from.
    //
    // This used to canonicalise every note in the collection — one
    // filesystem call each, before any export work, in the asynchronous job
    // path as well as the synchronous one — so the prologue cost the size of
    // the vault however few notes were being exported. An output is written
    // inside the destination directory, so the only notes it can overwrite
    // are the ones that live there; the rest cannot collide with any path
    // this plan will produce. Deciding which those are is string work
    // against the vault-relative paths, and only the survivors are resolved.
    QStringList candidates = relPaths;   // always: these are being read
    if (isInsideDirectory(canonicalDest, canonicalRoot)) {
        // The destination is inside the vault, so notes under it are exactly
        // the ones an output can reach.
        QString prefix = canonicalDest.mid(canonicalRoot.size());
        while (prefix.startsWith(QLatin1Char('/')))
            prefix.remove(0, 1);
        for (const QString &rel : collection->noteRelPaths()) {
            if (prefix.isEmpty()
                || rel.startsWith(prefix + QLatin1Char('/'))) {
                candidates.append(rel);
            }
        }
    }

    QSet<QString> sources;
    sources.reserve(candidates.size());
    for (const QString &rel : candidates)
        sources.insert(canonicalTarget(collection->absolutePath(rel)));

    if (singleFile) {
        PlannedOutput out;
        out.outPath = QDir(destDir).filePath(QStringLiteral("collection.") + ext);
        if (sources.contains(canonicalTarget(out.outPath))) {
            plan.error = tr("Exporting there would overwrite one of your "
                            "notes. Choose a destination outside the "
                            "collection.");
            return plan;
        }
        plan.outputs.append(out);
        return plan;
    }

    // A per-note Markdown export writes only the body. Anywhere inside the
    // vault that is one collision away from replacing a note with a copy of
    // itself stripped of its metadata, so it is refused outright rather than
    // path by path.
    if (format == QLatin1String("markdown") && !canonicalRoot.isEmpty()
        && isInsideDirectory(canonicalDest, canonicalRoot)) {
        plan.error = tr("Markdown export writes note bodies without their "
                        "metadata, so it cannot write inside the collection "
                        "itself. Choose a destination outside it.");
        return plan;
    }

    QSet<QString> claimed;
    for (const QString &rel : relPaths) {
        QString outRel = rel;
        if (outRel.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
            outRel = outRel.left(outRel.size() - 3);
        PlannedOutput out;
        out.relPath = rel;
        out.outPath = QDir(destDir).filePath(outRel + QLatin1Char('.') + ext);

        const QString canonical = canonicalTarget(out.outPath);
        if (sources.contains(canonical)) {
            plan.error = tr("Exporting there would overwrite %1. Choose a "
                            "destination outside the collection.").arg(rel);
            return plan;
        }
        if (claimed.contains(canonical)) {
            plan.error = tr("Two notes in this export would be written to the "
                            "same file (%1).").arg(out.outPath);
            return plan;
        }
        claimed.insert(canonical);
        plan.outputs.append(out);
    }
    return plan;
}

void DocumentExporter::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void DocumentExporter::setMaxAttachmentBytes(double bytes)
{
    m_maxAttachmentBytes = bytes > 0 ? static_cast<qint64>(bytes) : 0;
}

void DocumentExporter::setMaxCombinedChars(double chars)
{
    m_maxCombinedChars = chars > 0 ? static_cast<qint64>(chars) : 0;
}

// ---- one note ----

QString DocumentExporter::writeFailureMessage(const QString &outPath)
{
    return tr("\"%1\" could not be written.").arg(outPath);
}

QString DocumentExporter::failedNotesMessage(int failed,
                                             const QString &firstFailure)
{
    if (failed == 1)
        return writeFailureMessage(firstFailure);
    return tr("%1 notes could not be written; the first was \"%2\".")
        .arg(failed).arg(firstFailure);
}

bool DocumentExporter::exportOneNote(NoteCollection *collection,
                                     const PlannedOutput &output,
                                     const QString &format)
{
    // Each note resolves relative media against its OWN folder; carrying
    // one context across the whole run made notes in other folders pick
    // up same-named files from wherever the export started.
    useImageContextFor(collection, output.relPath);
    const QVariantMap info = collection->noteInfo(output.relPath);
    QString body = bodyForExport(collection, output.relPath);
    const QString title = info.value(QStringLiteral("title")).toString();

    // A standalone Markdown export is meant to be the note, so it carries the
    // note's front matter: tags, favourite and pinned state, custom fields and
    // any foreign keys the app does not interpret. The other formats render
    // the metadata block as prose, so it stays out of them.
    if (format == QLatin1String("markdown"))
        body = collection->frontMatterFor(output.relPath) + body;

    QDir().mkpath(QFileInfo(output.outPath).absolutePath());
    return writeMarkdownAs(body, title, format, output.outPath);
}

bool DocumentExporter::appendCombinedNote(Job *job, const QString &relPath)
{
    // One combined file. For HTML and PDF that means ONE document: each
    // note contributes only its <body> contents, the wrapper closes over
    // all of them once, and each shared script tag is injected once
    // however many notes needed it. Concatenating whole documents instead
    // produced a file with several <html> elements and duplicated
    // document-level scripts and ids, which is invalid HTML and ambiguous
    // input to the PDF printer.
    const bool browserTarget = job->format != QLatin1String("pdf");

    useImageContextFor(job->collection, relPath);
    const QString body = bodyForExport(job->collection, relPath);
    const QString title = job->collection->noteInfo(relPath)
        .value(QStringLiteral("title")).toString();

    if (job->format == QLatin1String("markdown")) {
        job->combinedBody += "# " + title + "\n\n" + body + "\n\n";
    } else if (job->format == QLatin1String("text")) {
        job->combinedBody += plainTextForMarkdown(body) + "\n\n";
    } else {
        bool noteMath = false;
        bool noteMermaid = false;
        const QList<Block::State> noteBlocks = blocksFromMarkdown(body);
        const QStringList noteSlugs = headingSlugs(noteBlocks);
        const QString one = buildHtmlBody(noteBlocks, noteSlugs, noteBlocks,
                                          noteSlugs, browserTarget,
                                          &noteMath, &noteMermaid);
        job->sawMath = job->sawMath || noteMath;
        job->sawMermaid = job->sawMermaid || noteMermaid;
        // Each note after the first starts its own printed page, and
        // keeps the rule that used to separate them on screen.
        job->combinedBody += job->firstNote
            ? QStringLiteral("<section>\n")
            : QStringLiteral("<hr>\n<section style=\""
                             "page-break-before:always\">\n");
        job->combinedBody += one + QStringLiteral("\n</section>\n");
    }
    job->firstNote = false;

    if (m_maxCombinedChars > 0 && job->combinedBody.size() > m_maxCombinedChars) {
        job->error = tr("This selection is too large to combine into a single "
                        "file. Export it as one file per note instead.");
        return false;
    }
    return true;
}

bool DocumentExporter::writeCombined(Job *job)
{
    const bool htmlLike = job->format != QLatin1String("markdown")
        && job->format != QLatin1String("text");
    QString combined = htmlLike
        ? wrapHtmlDocument(job->combinedBody, QString(),
                           job->format != QLatin1String("pdf"),
                           job->sawMath, job->sawMermaid)
        : job->combinedBody;

    const QString out = job->outputs.first().outPath;
    QDir().mkpath(QFileInfo(out).absolutePath());
    if (job->format == QLatin1String("pdf"))
        return htmlToPdf(combined, out);
    return writeText(out, combined);
}

// ---- synchronous export ----

int DocumentExporter::exportCollection(QObject *collectionObj,
                                       const QString &destDir,
                                       const QString &format, bool singleFile)
{
    NoteCollection *collection = qobject_cast<NoteCollection *>(collectionObj);
    if (!collection || !collection->isOpen())
        return 0;
    return exportNotes(collection, collection->noteRelPaths(), destDir,
                       format, singleFile);
}

int DocumentExporter::exportNotes(QObject *collectionObj,
                                  const QStringList &relPaths,
                                  const QString &destDir,
                                  const QString &format, bool singleFile)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("export.run"),
        QVariantMap{
            {QStringLiteral("format"), format},
            {QStringLiteral("destDir"), destDir},
            {QStringLiteral("scopeSize"), relPaths.size()},
            {QStringLiteral("singleFile"), singleFile},
        });
    NoteCollection *collection = qobject_cast<NoteCollection *>(collectionObj);

    // Nothing is written until the whole plan is known to be safe.
    const OutputPlan plan =
        buildOutputPlan(collection, relPaths, destDir, format, singleFile);
    if (!plan.error.isEmpty()) {
        setLastError(plan.error);
        emit exportRefused(plan.error);
        perf.addContext(QStringLiteral("refused"), true);
        return 0;
    }
    setLastError(QString());
    QDir().mkpath(destDir);

    const QPair<QString, QString> savedContext{m_noteDir, m_collectionRoot};
    int written = 0;

    if (singleFile) {
        Job job;
        job.collection = collection;
        job.format = format;
        job.outputs = plan.outputs;
        job.singleFile = true;
        for (const QString &rel : relPaths) {
            if (!appendCombinedNote(&job, rel))
                break;
        }
        if (job.error.isEmpty() && writeCombined(&job))
            written = relPaths.size();
        else if (!job.error.isEmpty())
            setLastError(job.error);
        else
            setLastError(writeFailureMessage(job.outputs.first().outPath));
    } else {
        int failed = 0;
        QString firstFailure;
        for (const PlannedOutput &output : plan.outputs) {
            if (exportOneNote(collection, output, format)) {
                ++written;
            } else {
                if (failed == 0)
                    firstFailure = output.outPath;
                ++failed;
            }
        }
        // Same rule as the asynchronous path: the notes that did land are
        // worth keeping, and the ones that did not have to be named.
        if (failed > 0)
            setLastError(failedNotesMessage(failed, firstFailure));
    }

    m_noteDir = savedContext.first;
    m_collectionRoot = savedContext.second;
    perf.addContext(QStringLiteral("written"), written);
    return written;
}

// ---- job ----

int DocumentExporter::progress() const
{
    return m_job ? m_job->next : 0;
}

int DocumentExporter::total() const
{
    // One entry per note in both modes: a combined job's outputs are built
    // note by note even though they all name the same file, so this is the
    // amount of work, not the number of files.
    return m_job ? m_job->outputs.size() : 0;
}

bool DocumentExporter::startExportCollection(QObject *collectionObj,
                                             const QString &destDir,
                                             const QString &format,
                                             bool singleFile)
{
    NoteCollection *collection = qobject_cast<NoteCollection *>(collectionObj);
    if (!collection || !collection->isOpen())
        return false;
    return startJob(collection, collection->noteRelPaths(), destDir, format,
                    singleFile);
}

bool DocumentExporter::startExportNotes(QObject *collectionObj,
                                        const QStringList &relPaths,
                                        const QString &destDir,
                                        const QString &format, bool singleFile)
{
    return startJob(qobject_cast<NoteCollection *>(collectionObj), relPaths,
                    destDir, format, singleFile);
}

bool DocumentExporter::startJob(NoteCollection *collection,
                                const QStringList &relPaths,
                                const QString &destDir, const QString &format,
                                bool singleFile)
{
    if (m_job) {
        const QString busyError = tr("An export is already running.");
        setLastError(busyError);
        emit exportRefused(busyError);
        return false;
    }

    const OutputPlan plan =
        buildOutputPlan(collection, relPaths, destDir, format, singleFile);
    if (!plan.error.isEmpty()) {
        setLastError(plan.error);
        emit exportRefused(plan.error);
        return false;
    }
    setLastError(QString());
    QDir().mkpath(destDir);

    m_job = std::make_unique<Job>();
    m_job->collection = collection;
    m_job->rootPath = collection->rootPath();
    m_job->format = format;
    m_job->destDir = destDir;
    m_job->singleFile = singleFile;
    m_job->savedContext = QPair<QString, QString>{m_noteDir, m_collectionRoot};
    // A combined export renders every note in scope and writes one file, so
    // the plan's single output says nothing about how much work is left; the
    // note list does.
    if (singleFile) {
        m_job->outputs.clear();
        for (const QString &rel : relPaths) {
            PlannedOutput out;
            out.relPath = rel;
            out.outPath = plan.outputs.first().outPath;
            m_job->outputs.append(out);
        }
    } else {
        m_job->outputs = plan.outputs;
    }

    emit busyChanged();
    emit progressChanged();
    m_jobTimer.start();
    return true;
}

void DocumentExporter::cancelExport()
{
    if (m_job)
        m_job->cancelled = true;
}

void DocumentExporter::stepJob()
{
    if (!m_job)
        return;
    if (m_job->cancelled || m_job->next >= m_job->outputs.size()) {
        finishJob();
        return;
    }

    // The vault this export was planned against is not the one open now. Every
    // remaining note would be read from somewhere the reader never chose, so
    // stop here and say so rather than finish an export of two collections.
    if (!m_job->collection
        || m_job->collection->rootPath() != m_job->rootPath) {
        m_job->error = tr("The open collection changed while the export was "
                          "running, so it was stopped after %1 of %2 notes.")
                           .arg(m_job->next).arg(m_job->outputs.size());
        m_job->next = m_job->outputs.size();
        finishJob();
        return;
    }

    const PlannedOutput output = m_job->outputs.at(m_job->next);
    if (m_job->singleFile) {
        if (!appendCombinedNote(m_job.get(), output.relPath)) {
            m_job->next = m_job->outputs.size();
            finishJob();
            return;
        }
    } else if (exportOneNote(m_job->collection, output, m_job->format)) {
        ++m_job->written;
    } else {
        if (m_job->failed == 0)
            m_job->firstFailure = output.outPath;
        ++m_job->failed;
    }

    ++m_job->next;
    emit progressChanged();
    emit exportProgress(m_job->next, m_job->outputs.size(), output.relPath);

    // Back to the event loop: this is what lets the window repaint and the
    // reader press Cancel between notes.
    m_jobTimer.start();
}

void DocumentExporter::finishJob()
{
    if (!m_job)
        return;
    std::unique_ptr<Job> job = std::move(m_job);
    m_jobTimer.stop();

    if (job->singleFile && job->error.isEmpty() && !job->cancelled) {
        if (writeCombined(job.get()))
            job->written = job->outputs.size();
        else
            job->error = writeFailureMessage(job->outputs.first().outPath);
    }

    // A per-note write that failed left `written` short of the total and said
    // nothing else, so the shell reported "Exported N notes" for a run that
    // had silently lost some of them. The rest of the export is still worth
    // having, so the failures are carried to the end and reported there.
    if (job->error.isEmpty() && job->failed > 0)
        job->error = failedNotesMessage(job->failed, job->firstFailure);

    m_noteDir = job->savedContext.first;
    m_collectionRoot = job->savedContext.second;
    if (!job->error.isEmpty())
        setLastError(job->error);

    emit busyChanged();
    emit progressChanged();
    emit exportFinished(job->written, job->outputs.size(), job->cancelled,
                        job->error);
}
