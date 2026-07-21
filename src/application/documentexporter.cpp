// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentexporter.h"
#include "inlinemarkdown.h"
#include "blockmodel.h"
#include "documentserializer.h"
#include "markdownformatter.h"
#include "imageassets.h"
#include "tabledata.h"
#include "kanbandata.h"
#include "codelanguages.h"
#include "mathrenderer.h"
#include "diagrams/mermaidrenderer.h"
#include "diagrams/diagrampainter.h"
#include "documentoutline.h"
#include "notecollection.h"
#include "notefileio.h"
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

QString esc(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar &c : s) {
        if (c == '&') out += QStringLiteral("&amp;");
        else if (c == '<') out += QStringLiteral("&lt;");
        else if (c == '>') out += QStringLiteral("&gt;");
        else if (c == '"') out += QStringLiteral("&quot;");
        else out += c;
    }
    return out;
}

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
    }
    return previous;
}

QString DocumentExporter::bodyForExport(NoteCollection *collection,
                                        const QString &relPath) const
{
    if (!m_liveRelPath.isEmpty() && relPath == m_liveRelPath)
        return m_liveMarkdown;
    return collection
        ? collection->noteInfo(relPath).value(QStringLiteral("body")).toString()
        : QString();
}

QString DocumentExporter::extensionFor(const QString &format)
{
    if (format == QLatin1String("html")) return QStringLiteral("html");
    if (format == QLatin1String("pdf")) return QStringLiteral("pdf");
    if (format == QLatin1String("text")) return QStringLiteral("txt");
    return QStringLiteral("md");
}

// ---- block-list assembly ----

QList<DocumentExporter::Blk> DocumentExporter::blocksFromModel(BlockModel *model) const
{
    QList<Blk> out;
    if (!model)
        return out;
    for (int i = 0; i < model->count(); ++i) {
        Block *b = model->blockAt(i);
        if (!b)
            continue;
        out.append({b->blockType(), b->content(), b->indentLevel(),
                    b->checked(), b->language(), b->calloutTitle()});
    }
    return out;
}

QList<DocumentExporter::Blk> DocumentExporter::blocksFromMarkdown(const QString &markdown) const
{
    DocumentSerializer serializer;
    QList<Blk> out;
    const auto parsed = serializer.parse(markdown);
    for (const auto &d : parsed)
        out.append({d.type, d.content, d.indentLevel, d.checked,
                    d.language, d.calloutTitle});
    return out;
}

// ---- inline rendering ----

QString DocumentExporter::renderInline(const QString &md, bool mathJax,
                                       bool *sawMath) const
{
    MarkdownFormatter fmt;
    const QList<FormattedSpan> spans = fmt.parseSpans(md);

    // Recursive walk over the span tree (children are absolute coordinates).
    std::function<QString(const QList<FormattedSpan> &, int, int)> renderList;
    std::function<QString(const FormattedSpan &)> renderOne;

    renderOne = [&](const FormattedSpan &span) -> QString {
        const int cstart = span.start + span.openLen;
        const int cend = span.end - span.closeLen;
        QString inner;
        if (span.type == QLatin1String("code")
            || span.type == QLatin1String("autolink")
            || span.type == QLatin1String("math")
            || span.type == QLatin1String("escape"))
            // For an escape span the content is the bare character: the
            // export emits it without the backslash (fix 5).
            inner = esc(md.mid(cstart, cend - cstart));
        else
            inner = renderList(span.children, cstart, cend);

        const QString &t = span.type;
        if (t == QLatin1String("math")) {
            if (mathJax) {
                if (sawMath)
                    *sawMath = true;
                return "\\(" + inner + "\\)";
            }
            return inner; // image-embed modes keep the raw-TeX fallthrough
        }
        if (t == QLatin1String("bold")) return "<strong>" + inner + "</strong>";
        if (t == QLatin1String("italic")) return "<em>" + inner + "</em>";
        if (t == QLatin1String("bolditalic"))
            return "<strong><em>" + inner + "</em></strong>";
        if (t == QLatin1String("strike")) return "<s>" + inner + "</s>";
        if (t == QLatin1String("underline")) return "<u>" + inner + "</u>";
        if (t == QLatin1String("highlight")) return "<mark>" + inner + "</mark>";
        if (t == QLatin1String("code")) return "<code>" + inner + "</code>";
        if (t == QLatin1String("superscript")) return "<sup>" + inner + "</sup>";
        if (t == QLatin1String("subscript")) return "<sub>" + inner + "</sub>";
        if (t == QLatin1String("color"))
            return "<span style=\"color:" + esc(span.color) + "\">" + inner
                 + "</span>";
        if (t == QLatin1String("link") || t == QLatin1String("autolink"))
            return "<a href=\"" + esc(span.url) + "\">" + inner + "</a>";
        return inner;
    };

    renderList = [&](const QList<FormattedSpan> &list, int lo, int hi) -> QString {
        QString out;
        int pos = lo;
        for (const FormattedSpan &span : list) {
            if (span.start < lo || span.end > hi)
                continue;
            if (pos < span.start)
                out += esc(md.mid(pos, span.start - pos));
            out += renderOne(span);
            pos = span.end;
        }
        if (pos < hi)
            out += esc(md.mid(pos, hi - pos));
        return out;
    };

    return renderList(spans, 0, md.length());
}

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
    const QImage img = MathRenderer::render(tex, textSize, fg, 2.0, &error,
                                            verticalPadding);
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

QStringList DocumentExporter::headingSlugs(const QList<Blk> &blocks) const
{
    QStringList slugs;
    QHash<QString, int> counts;
    for (const Blk &b : blocks) {
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
    auto col = [&](QColor c, const char *fallback) {
        return m_theme ? c.name() : QString::fromLatin1(fallback);
    };
    const QString fg = m_theme ? m_theme->textPrimary().name() : QStringLiteral("#222222");
    const QString muted = m_theme ? m_theme->textMuted().name() : QStringLiteral("#666666");
    const QString bg = m_theme ? m_theme->windowBackground().name() : QStringLiteral("#ffffff");
    const QString accent = m_theme ? m_theme->accent().name() : QStringLiteral("#2970c8");
    const QString border = m_theme ? m_theme->border().name() : QStringLiteral("#dddddd");
    const QString codeBg = m_theme ? m_theme->codePanelBackground().name()
                                   : QStringLiteral("#f4f4f2");
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
        "img{max-width:100%%}"
        ".callout{border:1px solid %5;border-left:4px solid %3;border-radius:6px;"
        "padding:8px 12px;margin:10px 0}"
        ".callout .title{font-weight:bold;margin-bottom:4px}"
        ".kanban{display:flex;gap:12px}.kanban .col{border:1px solid %5;"
        "border-radius:6px;padding:8px;min-width:140px}"
        "mark{background:#fdf3a9}"
        ".math-display{text-align:center;margin:1em 0}"
        "pre.text-diagram{line-height:1.2}"
        "pre.text-diagram code{white-space:pre;font-family:"
        "'Cascadia Code',Consolas,'DejaVu Sans Mono',monospace}"
        "pre.mermaid{background:none;padding:0;text-align:center}"
        ".diagram-source{margin:2px 0 10px;color:%4;font-size:13px}"
        ".diagram-source pre{margin-top:4px}"
        ).arg(fg, bg, accent, muted, border, codeBg);
}

// ---- the HTML builder ----

QString DocumentExporter::buildHtmlBody(const QList<Blk> &blocks,
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
    bool sawMath = false;
    bool sawMermaid = false;

    const QStringList slugs = headingSlugs(blocks);
    QString body;

    // Grouped list rendering: consecutive bullet/numbered/todo items nest.
    auto isListType = [](Block::BlockType t) {
        return t == Block::BulletList || t == Block::NumberedList
            || t == Block::Todo;
    };

    for (int i = 0; i < blocks.size(); ) {
        const Blk &b = blocks.at(i);

        if (isListType(b.type)) {
            // Collect the contiguous list run and emit it with its nesting:
            // an item deeper than the one before opens a sublist INSIDE the
            // still-open <li>, which is where HTML wants a nested list. The
            // closing </li> is deferred for exactly that reason, so a flat
            // run still emits <ul><li>a</li><li>b</li></ul> unchanged.
            const bool ordered = b.type == Block::NumberedList;
            const QString openTag = ordered ? QStringLiteral("<ol>")
                                            : QStringLiteral("<ul>");
            const QString closeTag = ordered ? QStringLiteral("</ol>")
                                             : QStringLiteral("</ul>");
            int depth = 0;   // lists currently open in this run
            while (i < blocks.size() && isListType(blocks.at(i).type)
                   && (blocks.at(i).type == Block::NumberedList) == ordered) {
                const Blk &item = blocks.at(i);
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
                QString li = renderInline(item.content, mathJax, &sawMath);
                if (item.type == Block::Todo)
                    li = (item.checked ? QStringLiteral("&#9745; ")
                                       : QStringLiteral("&#9744; ")) + li;
                body += "<li>" + li;
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

        switch (b.type) {
        case Block::Heading1: case Block::Heading2:
        case Block::Heading3: case Block::Heading4: {
            const int level = b.type == Block::Heading1 ? 1
                            : b.type == Block::Heading2 ? 2
                            : b.type == Block::Heading3 ? 3 : 4;
            const QString tag = QStringLiteral("h%1").arg(level);
            body += "<" + tag + " id=\"" + esc(slugs.at(i)) + "\">"
                  + renderInline(b.content, mathJax, &sawMath) + "</" + tag + ">";
            break;
        }
        case Block::Quote:
            body += "<blockquote>" + renderInline(b.content, mathJax, &sawMath) + "</blockquote>";
            break;
        case Block::Divider:
            body += "<hr>";
            break;
        case Block::Callout: {
            const QString heading = b.calloutTitle.isEmpty()
                ? b.language : b.calloutTitle;
            body += "<div class=\"callout\"><div class=\"title\">"
                  + esc(heading) + "</div>" + renderInline(b.content, mathJax, &sawMath) + "</div>";
            break;
        }
        case Block::MathBlock: {
            if (mathJax) {
                // MathJax reads the parsed DOM text, so HTML-escaping the
                // TeX is transparent to it.
                body += "<p class=\"math-display\">\\[ " + esc(b.content)
                      + " \\]</p>";
                sawMath = true;
                break;
            }
            const QString uri = dataUriForMath(b.content);
            if (!uri.isEmpty())
                body += "<p style=\"text-align:center\"><img alt=\""
                      + esc(b.content) + "\" src=\"" + uri + "\"></p>";
            else
                body += "<pre><code>" + esc(b.content) + "</code></pre>";
            break;
        }
        case Block::Image: case Block::Media: {
            const ImageAssets::Parsed p = ImageAssets::parseLine(b.content);
            if (b.type == Block::Image) {
                const QString uri = dataUriForImagePath(p.path);
                QString img = uri.isEmpty()
                    ? ("<em>[image: " + esc(p.path) + "]</em>")
                    : ("<img alt=\"" + esc(p.alt) + "\" src=\"" + uri + "\""
                       + (p.width > 0 ? " width=\"" + QString::number(p.width) + "\"" : "")
                       + ">");
                body += "<figure>" + img
                      + (p.caption.isEmpty() ? QString()
                            : "<figcaption>" + esc(p.caption) + "</figcaption>")
                      + "</figure>";
            } else {
                // Media exports as a link to the source (no inline player).
                body += "<p>&#9654; <a href=\"" + esc(p.path) + "\">"
                      + esc(p.alt.isEmpty() ? p.path : p.alt) + "</a></p>";
            }
            break;
        }
        case Block::Table: {
            const TableData::Table tbl = TableData::parse(b.content);
            body += QStringLiteral("<table>");
            if (!tbl.headers.isEmpty()) {
                body += QStringLiteral("<tr>");
                for (const QString &h : tbl.headers)
                    body += "<th>" + renderInline(h, mathJax, &sawMath) + "</th>";
                body += QStringLiteral("</tr>");
            }
            for (const QStringList &row : tbl.rows) {
                body += QStringLiteral("<tr>");
                for (const QString &cell : row)
                    body += "<td>" + renderInline(cell, mathJax, &sawMath) + "</td>";
                body += QStringLiteral("</tr>");
            }
            body += QStringLiteral("</table>");
            break;
        }
        case Block::CodeBlock:
            if (b.language == QLatin1String("mermaid")) {
                if (browserTarget) {
                    // The browser renders the original source with Mermaid.js.
                    // A collapsed <details> keeps the escaped source available
                    // after Mermaid replaces the render target or errors.
                    body += "<pre class=\"mermaid\">" + esc(b.content) + "</pre>";
                    body += "<details class=\"diagram-source\"><summary>Diagram "
                            "source</summary><pre><code>" + esc(b.content)
                          + "</code></pre></details>";
                    sawMermaid = true;
                } else {
                    // PDF: rasterize a natively-supported diagram; fall back to
                    // escaped source for invalid/unsupported families.
                    const QString uri = dataUriForMermaid(b.content);
                    if (!uri.isEmpty())
                        body += "<p style=\"text-align:center\"><img alt=\"Mermaid "
                                "diagram\" src=\"" + uri + "\"></p>";
                    else
                        body += "<pre><code>" + esc(b.content) + "</code></pre>";
                }
            } else if (b.language == QLatin1String("diagram")
                || b.language == QLatin1String("text-diagram")
                || b.language == QLatin1String("ascii-diagram")) {
                // Character diagram: escaped preformatted text with whitespace
                // preserved and the configured monospace stack. Both browser
                // HTML and the PDF path share this branch.
                body += "<pre class=\"text-diagram\"><code>" + esc(b.content)
                      + "</code></pre>";
            } else if (b.language == QLatin1String("kanban")) {
                const KanbanData::Board board = KanbanData::parse(b.content);
                body += "<div class=\"kanban\">";
                for (const KanbanData::Column &col : board.columns) {
                    body += "<div class=\"col\"><strong>" + esc(col.name)
                          + "</strong><ul>";
                    for (const KanbanData::Card &card : col.cards)
                        body += "<li>" + (card.done ? QStringLiteral("&#9745; ")
                                                    : QStringLiteral("&#9744; "))
                              + esc(card.title) + "</li>";
                    body += "</ul></div>";
                }
                body += "</div>";
            } else if (b.language == QLatin1String("toc")) {
                // Regenerate the TOC from this document's headings as anchors.
                body += "<ul class=\"toc\">";
                int minLevel = 4;
                for (const Blk &h : blocks) {
                    if (h.type == Block::Heading1) minLevel = qMin(minLevel, 1);
                    else if (h.type == Block::Heading2) minLevel = qMin(minLevel, 2);
                    else if (h.type == Block::Heading3) minLevel = qMin(minLevel, 3);
                    else if (h.type == Block::Heading4) minLevel = qMin(minLevel, 4);
                }
                for (int j = 0; j < blocks.size(); ++j) {
                    const Blk &h = blocks.at(j);
                    int lvl = h.type == Block::Heading1 ? 1
                            : h.type == Block::Heading2 ? 2
                            : h.type == Block::Heading3 ? 3
                            : h.type == Block::Heading4 ? 4 : 0;
                    if (lvl == 0) continue;
                    body += "<li style=\"margin-left:"
                          + QString::number((lvl - minLevel) * 16) + "px\">"
                          + "<a href=\"#" + esc(slugs.at(j)) + "\">"
                          + esc(InlineMarkdown::displayText(h.content))
                          + "</a></li>";
                }
                body += "</ul>";
            } else {
                // Syntax-highlighted code as colored spans.
                QString code;
                const QList<CodeLanguages::Span> hs =
                    CodeLanguages::highlightSpans(b.language, b.content);
                int pos = 0;
                for (const CodeLanguages::Span &s : hs) {
                    if (s.start > pos)
                        code += esc(b.content.mid(pos, s.start - pos));
                    const QString colr = tokenColor(s.token, m_theme);
                    const QString piece = esc(b.content.mid(s.start, s.length));
                    code += colr.isEmpty() ? piece
                        : ("<span style=\"color:" + colr + "\">" + piece + "</span>");
                    pos = s.start + s.length;
                }
                if (pos < b.content.length())
                    code += esc(b.content.mid(pos));
                body += "<pre><code>" + code + "</code></pre>";
            }
            break;
        case Block::Paragraph:
        default:
            if (!b.content.isEmpty())
                body += "<p>" + renderInline(b.content, mathJax, &sawMath) + "</p>";
            break;
        }
        ++i;
    }

    if (sawMathOut)
        *sawMathOut = sawMath;
    if (sawMermaidOut)
        *sawMermaidOut = sawMermaid;
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

QString DocumentExporter::buildHtml(const QList<Blk> &blocks,
                                    const QString &title,
                                    bool browserTarget) const
{
    bool sawMath = false;
    bool sawMermaid = false;
    const QString body =
        buildHtmlBody(blocks, browserTarget, &sawMath, &sawMermaid);
    return wrapHtmlDocument(body, title, browserTarget, sawMath, sawMermaid);
}

QString DocumentExporter::htmlForModel(BlockModel *model, const QString &title) const
{
    return buildHtml(blocksFromModel(model), title, true);
}

QString DocumentExporter::htmlForMarkdown(const QString &markdown,
                                          const QString &title) const
{
    return buildHtml(blocksFromMarkdown(markdown), title, true);
}

// ---- plain text ----

QString DocumentExporter::buildPlainText(const QList<Blk> &blocks) const
{
    QStringList lines;
    int ordinal = 0;
    for (int i = 0; i < blocks.size(); ++i) {
        const Blk &b = blocks.at(i);
        const QString indent(2 * b.indentLevel, QLatin1Char(' '));
        const bool verbatim = b.type == Block::CodeBlock;
        const QString text = verbatim ? b.content
            : InlineMarkdown::displayText(b.content);
        if (b.type == Block::NumberedList) ++ordinal; else ordinal = 0;
        switch (b.type) {
        case Block::Heading1: lines << "# " + text; break;
        case Block::Heading2: lines << "## " + text; break;
        case Block::Heading3: lines << "### " + text; break;
        case Block::Heading4: lines << "#### " + text; break;
        case Block::BulletList: lines << indent + "- " + text; break;
        case Block::NumberedList:
            lines << indent + QString::number(ordinal) + ". " + text; break;
        case Block::Todo:
            lines << indent + (b.checked ? "[x] " : "[ ] ") + text; break;
        case Block::Quote: {
            const auto ql = text.split(QLatin1Char('\n'));
            for (const QString &q : ql) lines << "> " + q;
            break;
        }
        case Block::Divider: lines << "---"; break;
        case Block::CodeBlock: lines << text; break;
        default:
            if (!text.isEmpty()) lines << text;
            break;
        }
        lines << QString(); // blank line between blocks
    }
    return lines.join(QLatin1Char('\n'));
}

QString DocumentExporter::plainTextForModel(BlockModel *model) const
{
    return buildPlainText(blocksFromModel(model));
}

QString DocumentExporter::plainTextForMarkdown(const QString &markdown) const
{
    return buildPlainText(blocksFromMarkdown(markdown));
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
        return writeText(path, serializer.serialize(model));
    }
    if (format == QLatin1String("html"))
        return writeText(path, htmlForModel(model, title));
    if (format == QLatin1String("text"))
        return writeText(path, plainTextForModel(model));
    if (format == QLatin1String("pdf"))
        return htmlToPdf(buildHtml(blocksFromModel(model), title, false),
                         path);
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

    const QString ext = extensionFor(format);
    const QString canonicalDest = canonicalTarget(destDir);
    const QString canonicalRoot = canonicalTarget(collection->rootPath());

    // Every source, resolved once, so an output can be compared against all
    // of them and not merely against the note it came from.
    QSet<QString> sources;
    sources.reserve(relPaths.size());
    for (const QString &rel : collection->noteRelPaths())
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
        const QString one = buildHtmlBody(blocksFromMarkdown(body),
                                          browserTarget, &noteMath,
                                          &noteMermaid);
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
    } else {
        for (const PlannedOutput &output : plan.outputs) {
            if (exportOneNote(collection, output, format))
                ++written;
        }
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

    const PlannedOutput output = m_job->outputs.at(m_job->next);
    if (m_job->singleFile) {
        if (!appendCombinedNote(m_job.get(), output.relPath)) {
            m_job->next = m_job->outputs.size();
            finishJob();
            return;
        }
    } else if (exportOneNote(m_job->collection, output, m_job->format)) {
        ++m_job->written;
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
    }

    m_noteDir = job->savedContext.first;
    m_collectionRoot = job->savedContext.second;
    if (!job->error.isEmpty())
        setLastError(job->error);

    emit busyChanged();
    emit progressChanged();
    emit exportFinished(job->written, job->outputs.size(), job->cancelled,
                        job->error);
}
