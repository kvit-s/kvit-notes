// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentexporter.h"
#include "blockmodel.h"
#include "block.h"
#include "notecollection.h"
#include "documentserializer.h"

#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>

// Document export: the pure HTML builder asserted per
// block type, plain-text and markdown export, the PDF print seam producing a
// non-empty file, and collection-scope writing.
class TestDocumentExporter : public QObject
{
    Q_OBJECT

private slots:
    void testHtmlWrapper();
    void testHeadingCarriesSlugAnchor();
    void testInlineBoldItalicLink();
    void testEscapedPunctuationExportsBare();
    void testBulletAndNumberedLists();
    void testTodoCheckboxes();
    void testQuote();
    void testDivider();
    void testCodeBlockHighlighted();
    void testCharacterDiagramExports();
    void testMermaidHtmlExport();
    void testMermaidScriptOnlyWithMermaid();
    void testTable();
    void testCallout();
    void testTocFenceBecomesAnchorList();
    void testInternalLinkAnchor();
    void testDisplayMathEmitsMathJaxDelimiters();
    void testInlineMathEmitsMathJaxDelimiters();
    void testMathJaxScriptTagInjectedOnlyWithMath();
    void testLiteralDollarsStayLiteral();
    void testMathJaxReferenceCorpusArtifact();
    void testMathExportPngModeArtifacts();
    void testPlainTextStructuralPrefixes();
    void testWriteHtmlFile();
    void testWritePdfNonEmpty();
    void testExportCollectionPerNote();
    void testExportCollectionSingleFile();

    // M8: one combined document, and lists that keep their nesting.
    void testSingleFileHtmlIsOneDocument();
    void testSingleFileHtmlInjectsSharedAssetsOnce();
    void testSingleFileHtmlSeparatesNotesWithPageBreaks();
    void testNestedListsNestInHtml();
    void testNestedNumberedAndTodoListsNest();

    // M7: each note resolves its images against its own folder, and the note
    // being edited exports at its current state.
    void testPerNoteImageBaseInCollectionExport();
    void testLiveNoteSnapshotOverridesSavedBody();
    void testLiveNoteSnapshotIsIgnoredForOtherNotes();

private:
    DocumentExporter m_exporter;

    // A note written straight to disk so its body is under test control.
    static void writeNote(NoteCollection *coll, const QString &relPath,
                          const QString &body)
    {
        const QString abs = coll->absolutePath(relPath);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(body.toUtf8());
        f.close();
    }
};

void TestDocumentExporter::testHtmlWrapper()
{
    const QString html = m_exporter.htmlForMarkdown("Hello", "My Title");
    QVERIFY(html.contains("<!DOCTYPE html>"));
    QVERIFY(html.contains("<style>"));
    QVERIFY(html.contains("<title>My Title</title>"));
    QVERIFY(html.contains("<p>Hello</p>"));
}

void TestDocumentExporter::testHeadingCarriesSlugAnchor()
{
    const QString html = m_exporter.htmlForMarkdown("# Getting Started");
    QVERIFY(html.contains("<h1 id=\"getting-started\">Getting Started</h1>"));
}

void TestDocumentExporter::testInlineBoldItalicLink()
{
    const QString html = m_exporter.htmlForMarkdown(
        "A **bold** and *italic* and [link](http://x).");
    QVERIFY(html.contains("<strong>bold</strong>"));
    QVERIFY(html.contains("<em>italic</em>"));
    QVERIFY(html.contains("<a href=\"http://x\">link</a>"));
}

void TestDocumentExporter::testEscapedPunctuationExportsBare()
{
    // Escaped punctuation: the markdown source keeps the backslash;
    // HTML export emits the escaped character without it.
    const QString html = m_exporter.htmlForMarkdown("2 \\* 3 \\* 4");
    QVERIFY(html.contains("2 * 3 * 4"));
    QVERIFY(!html.contains("\\*"));
}

void TestDocumentExporter::testBulletAndNumberedLists()
{
    const QString bullets = m_exporter.htmlForMarkdown("- one\n- two");
    QVERIFY(bullets.contains("<ul><li>one</li><li>two</li></ul>"));
    const QString numbered = m_exporter.htmlForMarkdown("1. a\n2. b");
    QVERIFY(numbered.contains("<ol><li>a</li><li>b</li></ol>"));
}

void TestDocumentExporter::testTodoCheckboxes()
{
    const QString html = m_exporter.htmlForMarkdown("- [ ] open\n- [x] done");
    QVERIFY(html.contains("&#9744; open"));  // unchecked box
    QVERIFY(html.contains("&#9745; done"));  // checked box
}

void TestDocumentExporter::testQuote()
{
    const QString html = m_exporter.htmlForMarkdown("> quoted");
    QVERIFY(html.contains("<blockquote>quoted</blockquote>"));
}

void TestDocumentExporter::testDivider()
{
    QVERIFY(m_exporter.htmlForMarkdown("---").contains("<hr>"));
}

void TestDocumentExporter::testCodeBlockHighlighted()
{
    const QString html = m_exporter.htmlForMarkdown("```python\nreturn 1\n```");
    QVERIFY(html.contains("<pre><code>"));
    // "return" is a keyword → wrapped in a colored span.
    QVERIFY(html.contains("<span style=\"color:"));
    QVERIFY(html.contains("return"));
}

void TestDocumentExporter::testCharacterDiagramExports()
{
    // A `diagram` fence exports as an escaped, whitespace-preserving
    // <pre class="text-diagram">; markup in the body is escaped, not
    // interpreted.
    const QString md = "```diagram\n"
                       "┌────┐\n"
                       "│ <a>│\n"
                       "└────┘\n"
                       "```";
    const QString html = m_exporter.htmlForMarkdown(md);
    QVERIFY(html.contains("<pre class=\"text-diagram\">"));
    QVERIFY(html.contains("&lt;a&gt;"));          // escaped, not a tag
    QVERIFY(!html.contains("<a>"));

    // Plain text emits the verbatim source body.
    const QString text = m_exporter.plainTextForMarkdown(md);
    QVERIFY(text.contains(QString::fromUtf8("┌────┐")));
    QVERIFY(text.contains(QString::fromUtf8("│ <a>│")));
}

void TestDocumentExporter::testMermaidHtmlExport()
{
    const QString md = "```mermaid\nflowchart LR\n  A[<x>] --> B\n```";
    const QString html = m_exporter.htmlForMarkdown(md);
    // The render target and the collapsed source disclosure are both present.
    QVERIFY(html.contains("<pre class=\"mermaid\">"));
    QVERIFY(html.contains("<details class=\"diagram-source\">"));
    // Source is HTML-escaped, not interpreted.
    QVERIFY(html.contains("&lt;x&gt;"));
    QVERIFY(!html.contains("<x>"));
    // Exactly one pinned module import, at the exact reviewed version.
    QVERIFY(html.contains("mermaid@11.16.0/dist/mermaid.esm.min.mjs"));
    QCOMPARE(html.count("cdn.jsdelivr.net/npm/mermaid@11.16.0"), 1);
    QVERIFY(html.contains("securityLevel: 'strict'"));
    QVERIFY(html.contains("htmlLabels: false"));
}

void TestDocumentExporter::testMermaidScriptOnlyWithMermaid()
{
    // Two Mermaid blocks still inject the module exactly once.
    const QString two = "```mermaid\nflowchart LR\nA-->B\n```\n\n"
                        "```mermaid\ngraph TD\nC-->D\n```";
    QCOMPARE(m_exporter.htmlForMarkdown(two)
                 .count("mermaid.esm.min.mjs"), 1);
    // A Mermaid-free export carries no Mermaid network dependency (the CSS
    // still defines the .mermaid class, like .kanban; only the module matters).
    const QString none = m_exporter.htmlForMarkdown("# Title\n\nJust prose.");
    QVERIFY(!none.contains("mermaid.esm.min.mjs"));
    QVERIFY(!none.contains("<pre class=\"mermaid\">"));
    // Plain text of a Mermaid block is the verbatim source.
    const QString text = m_exporter.plainTextForMarkdown(
        "```mermaid\nflowchart LR\nA-->B\n```");
    QVERIFY(text.contains("flowchart LR"));
    QVERIFY(text.contains("A-->B"));
}

void TestDocumentExporter::testTable()
{
    const QString html = m_exporter.htmlForMarkdown(
        "| A | B |\n| --- | --- |\n| 1 | 2 |");
    QVERIFY(html.contains("<table>"));
    QVERIFY(html.contains("<th>A</th>"));
    QVERIFY(html.contains("<td>1</td>"));
}

void TestDocumentExporter::testCallout()
{
    const QString html = m_exporter.htmlForMarkdown("> [!info] Heads up\n> body");
    QVERIFY(html.contains("class=\"callout\""));
    QVERIFY(html.contains("Heads up"));
    QVERIFY(html.contains("body"));
}

void TestDocumentExporter::testTocFenceBecomesAnchorList()
{
    const QString html = m_exporter.htmlForMarkdown(
        "# Intro\n\n```toc\n```\n\n## Details");
    QVERIFY(html.contains("class=\"toc\""));
    QVERIFY(html.contains("<a href=\"#intro\">Intro</a>"));
    QVERIFY(html.contains("<a href=\"#details\">Details</a>"));
}

void TestDocumentExporter::testInternalLinkAnchor()
{
    const QString html = m_exporter.htmlForMarkdown("See [risks](#risks).");
    QVERIFY(html.contains("<a href=\"#risks\">risks</a>"));
}

void TestDocumentExporter::testDisplayMathEmitsMathJaxDelimiters()
{
    // Display math exports as \[ … \] inside p.math-display:
    // the TeX stays in the document and MathJax typesets it in the browser.
    // &, <, > in the TeX are HTML-escaped — MathJax reads the parsed DOM
    // text, so escaping is transparent to it.
    const QString html =
        m_exporter.htmlForMarkdown("$$\na & b < c > d\n$$");
    QVERIFY(html.contains(
        "<p class=\"math-display\">\\[ a &amp; b &lt; c &gt; d \\]</p>"));
    QVERIFY(!html.contains("data:image/"));
}

void TestDocumentExporter::testInlineMathEmitsMathJaxDelimiters()
{
    const QString html =
        m_exporter.htmlForMarkdown("The square $x^2$ grows fast.");
    QVERIFY(html.contains("\\(x^2\\)"));
    // The $ markers themselves are not exported — \( \) are the only
    // delimiters MathJax is left to find.
    QVERIFY(!html.contains("$x^2$"));
}

void TestDocumentExporter::testMathJaxScriptTagInjectedOnlyWithMath()
{
    // Exactly one pinned script tag when the document contains math…
    const QString withMath = m_exporter.htmlForMarkdown(
        "Inline $a+b$ math.\n\n$$\nE = mc^2\n$$");
    QCOMPARE(withMath.count("MathJax-script"), 1);
    QVERIFY(withMath.contains(
        "https://cdn.jsdelivr.net/npm/mathjax@3.2.2/es5/tex-svg.min.js"));

    // …and none (no network dependency) when it does not.
    const QString withoutMath = m_exporter.htmlForMarkdown("Just prose.");
    QVERIFY(!withoutMath.contains("MathJax"));
    QVERIFY(!withoutMath.contains("<script"));
}

void TestDocumentExporter::testLiteralDollarsStayLiteral()
{
    const QString html =
        m_exporter.htmlForMarkdown("It costs $5 and $6 total.");
    QVERIFY(html.contains("It costs $5 and $6 total."));
    QVERIFY(!html.contains("\\("));
    QVERIFY(!html.contains("MathJax"));
}

void TestDocumentExporter::testMathJaxReferenceCorpusArtifact()
{
    // The browser-review artifact for the MathJax export: the NewTX
    // reference corpus as display math, inline usage in
    // prose, and two NewTX-specific commands MathJax does not know — those
    // are expected to show MathJax's inline error rendering with the source
    // visible, never a broken image. The assertions cover what a headless
    // run can check; the visual review happens in a browser against the
    // written file.
    const QStringList corpus{
        QStringLiteral("x^2"),
        QStringLiteral("E = mc^2"),
        QStringLiteral("A_i^2 + B_i^2 = C_i^2"),
        QStringLiteral("\\frac{a+b}{c+d}"),
        QStringLiteral("\\frac{1}{1+\\frac{x}{2}}"),
        QStringLiteral("\\sqrt{1 + x^2}"),
        QStringLiteral("\\int_0^\\infty e^{-x^2}\\,dx"),
        QStringLiteral("\\sum_{n=1}^{\\infty} \\frac{1}{n^2}"),
        QStringLiteral("\\left(\\frac{a}{b}\\right)"),
        QStringLiteral("\\alpha\\beta\\Gamma\\Delta\\theta\\vartheta\\phi\\varphi"),
        QStringLiteral("\\leq \\geq \\neq \\approx \\in \\notin \\subseteq \\cup \\cap"),
        QStringLiteral("\\partial\\quad\\nabla\\quad\\infty"),
        QStringLiteral("\\mathcal{F}\\quad\\mathscr{L}\\quad\\mathbb{R}"
                       "\\quad\\mathfrak{g}"),
        QStringLiteral("x^{\\in A}\\quad y_{\\oplus}\\quad e^{x^{\\leq 2}}"),
        QStringLiteral("\\mathrm{x}x\\quad\\mathrm{d}x\\quad\\sin(x) + \\log(y)"),
    };

    QString markdown = QStringLiteral(
        "# MathJax export review\n\n"
        "Inline baseline check: the square $x^2$ and the fraction "
        "$\\frac{a}{b}$ sit inside prose.\n");
    for (const QString &tex : corpus)
        markdown += QStringLiteral("\n$$\n") + tex + QStringLiteral("\n$$\n");
    markdown += QStringLiteral(
        "\n## NewTX-specific commands (expected MathJax errors)\n\n"
        "$$\n\\vv{AB}\n$$\n\n$$\n\\widering{ABC}\n$$\n");

    QString dir = qEnvironmentVariable("KVIT_SHOT_DIR");
    if (dir.isEmpty())
        dir = QDir::currentPath() + QStringLiteral("/screenshots");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/math_export_mathjax_corpus.html");
    QVERIFY(m_exporter.writeMarkdownAs(markdown, "MathJax corpus", "html",
                                       path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(f.readAll());
    QCOMPARE(html.count("class=\"math-display\""), corpus.size() + 2);
    QCOMPARE(html.count("MathJax-script"), 1);
    QVERIFY(html.contains("\\(x^2\\)"));
    QVERIFY(html.contains("\\[ \\vv{AB} \\]"));
    QVERIFY(!html.contains("data:image/"));
}

void TestDocumentExporter::testMathExportPngModeArtifacts()
{
    // Format-aware math export: HTML leaves MathJax-delimited TeX in the
    // document, the PDF seam embeds PNG math, and KVIT_MATH_RENDER=png
    // force-overrides the HTML export into PNG embeds (the offline escape
    // hatch).
    const QString markdown = QStringLiteral(
        "# Equations\n\n$$\nE = mc^2\n$$\n\n"
        "$$\n\\int_0^1 x^2\\, dx = \\frac{1}{3}\n$$");

    QString dir = qEnvironmentVariable("KVIT_SHOT_DIR");
    if (dir.isEmpty())
        dir = QDir::currentPath() + QStringLiteral("/screenshots");
    QDir().mkpath(dir);

    struct RenderModeGuard {
        ~RenderModeGuard() { qunsetenv("KVIT_MATH_RENDER"); }
    } guard;

    auto readAll = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                           : QString();
    };

    // Defaults: HTML → MathJax TeX, PDF → PNG math.
    qunsetenv("KVIT_MATH_RENDER");
    const QString mathJaxHtml = dir + QStringLiteral("/math_export_mathjax.html");
    const QString pngPdf = dir + QStringLiteral("/math_export_png.pdf");
    QVERIFY(m_exporter.writeMarkdownAs(markdown, "Math", "html", mathJaxHtml));
    QVERIFY(m_exporter.writeMarkdownAs(markdown, "Math", "pdf", pngPdf));
    const QString mathJaxContent = readAll(mathJaxHtml);
    QVERIFY(mathJaxContent.contains("\\[ E = mc^2 \\]"));
    QVERIFY(mathJaxContent.contains("MathJax-script"));
    QVERIFY(!mathJaxContent.contains("data:image/"));
    QVERIFY(QFileInfo(pngPdf).size() > 0);

    // Forced PNG applies to HTML — image embeds, no CDN dependency; the
    // PDF default already embeds PNG math, asserted through the
    // equal-output check below.
    qputenv("KVIT_MATH_RENDER", "png");
    const QString pngHtml = dir + QStringLiteral("/math_export_png.html");
    QVERIFY(m_exporter.writeMarkdownAs(markdown, "Math", "html", pngHtml));
    const QString pngContent = readAll(pngHtml);
    QVERIFY(pngContent.contains("data:image/png;base64,"));
    QVERIFY(!pngContent.contains("MathJax"));
    const QString forcedPngPdf =
        dir + QStringLiteral("/math_export_png_forced.pdf");
    QVERIFY(m_exporter.writeMarkdownAs(markdown, "Math", "pdf", forcedPngPdf));

    // The default PDF matches the forced-PNG PDF size, so the default PDF
    // path embeds PNG math.
    QCOMPARE(QFileInfo(forcedPngPdf).size(), QFileInfo(pngPdf).size());
}

void TestDocumentExporter::testPlainTextStructuralPrefixes()
{
    const QString text = m_exporter.plainTextForMarkdown(
        "# Title\n\n- item\n\n> quote");
    QVERIFY(text.contains("# Title"));
    QVERIFY(text.contains("- item"));
    QVERIFY(text.contains("> quote"));
    // Markers are stripped from the display text.
    const QString t2 = m_exporter.plainTextForMarkdown("**bold** word");
    QVERIFY(t2.contains("bold word"));
    QVERIFY(!t2.contains("**"));
}

void TestDocumentExporter::testWriteHtmlFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("out.html");
    QVERIFY(m_exporter.writeMarkdownAs("# Hi", "T", "html", path));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY(content.contains("<h1 id=\"hi\">Hi</h1>"));
}

void TestDocumentExporter::testWritePdfNonEmpty()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("out.pdf");
    QString body = "# Title\n\n";
    for (int i = 0; i < 80; ++i)
        body += "Paragraph " + QString::number(i) + " with prose.\n\n";
    QVERIFY(m_exporter.writeMarkdownAs(body, "T", "pdf", path));
    QVERIFY(QFileInfo(path).size() > 1000);

    // Every natively supported Mermaid family rasterizes into the PDF: the
    // export with the diagrams is substantially larger than the same prose
    // alone (each block embeds a rendered PNG).
    const QString diagrams = QStringLiteral(
        "```mermaid\nflowchart LR\n  A[Start] --> B{Choice}\n```\n\n"
        "```mermaid\nsequenceDiagram\n  Alice->>Bob: Hello\n```\n\n"
        "```mermaid\nclassDiagram\n  Animal <|-- Duck\n```\n\n"
        "```mermaid\nstateDiagram-v2\n  [*] --> Working\n```\n\n"
        "```mermaid\nerDiagram\n  CUSTOMER ||--o{ ORDER : places\n```\n");
    const QString diagPath = dir.filePath("diagrams.pdf");
    QVERIFY(m_exporter.writeMarkdownAs(QStringLiteral("# D\n\n") + diagrams,
                                       "D", "pdf", diagPath));
    const QString basePath = dir.filePath("base.pdf");
    QVERIFY(m_exporter.writeMarkdownAs(QStringLiteral("# D\n"), "D", "pdf",
                                       basePath));
    QVERIFY(QFileInfo(diagPath).size() > QFileInfo(basePath).size() + 5000);
}

void TestDocumentExporter::testExportCollectionPerNote()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    coll.createFolder("", "Sub");
    coll.createNote("", "Alpha");
    coll.createNote("Sub", "Beta");

    QTemporaryDir dest;
    const int n = m_exporter.exportCollection(&coll, dest.path(), "html", false);
    QCOMPARE(n, 2);
    // The folder tree is mirrored.
    QVERIFY(QFile::exists(QDir(dest.path()).filePath("Alpha.html")));
    QVERIFY(QFile::exists(QDir(dest.path()).filePath("Sub/Beta.html")));
}

void TestDocumentExporter::testExportCollectionSingleFile()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    coll.createNote("", "One");
    coll.createNote("", "Two");

    QTemporaryDir dest;
    const int n = m_exporter.exportCollection(&coll, dest.path(), "html", true);
    QCOMPARE(n, 2);
    QVERIFY(QFile::exists(QDir(dest.path()).filePath("collection.html")));
}

// ---------- M8: one combined document ----------

namespace {

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

int occurrences(const QString &haystack, const QString &needle)
{
    int n = 0;
    for (int at = haystack.indexOf(needle); at >= 0;
         at = haystack.indexOf(needle, at + needle.size()))
        ++n;
    return n;
}

} // namespace

void TestDocumentExporter::testSingleFileHtmlIsOneDocument()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "One.md", "# One\n\nAlpha body.\n");
    writeNote(&coll, "Two.md", "# Two\n\nBeta body.\n");
    coll.refresh();

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", true), 2);
    const QString html = readAll(QDir(dest.path()).filePath("collection.html"));
    QVERIFY(!html.isEmpty());

    // A combined export is ONE document, not several concatenated.
    QCOMPARE(occurrences(html, "<!DOCTYPE html>"), 1);
    QCOMPARE(occurrences(html, "<html"), 1);
    QCOMPARE(occurrences(html, "</html>"), 1);
    QCOMPARE(occurrences(html, "<head>"), 1);
    QCOMPARE(occurrences(html, "<body>"), 1);
    QCOMPARE(occurrences(html, "</body>"), 1);
    // Nothing may follow the closing tag.
    QVERIFY(html.trimmed().endsWith(QLatin1String("</html>")));
    // Both notes are in it.
    QVERIFY(html.contains("Alpha body."));
    QVERIFY(html.contains("Beta body."));
}

void TestDocumentExporter::testSingleFileHtmlInjectsSharedAssetsOnce()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    // Both notes carry math, so a per-note wrapper would inject MathJax twice.
    writeNote(&coll, "One.md", "# One\n\n$$\nE = mc^2\n$$\n");
    writeNote(&coll, "Two.md", "# Two\n\n$$\na^2 + b^2\n$$\n");
    coll.refresh();

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", true), 2);
    const QString html = readAll(QDir(dest.path()).filePath("collection.html"));

    QCOMPARE(occurrences(html, "<style>"), 1);
    QCOMPARE(occurrences(html, "MathJax"), 1);
}

void TestDocumentExporter::testSingleFileHtmlSeparatesNotesWithPageBreaks()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "One.md", "# One\n\nAlpha.\n");
    writeNote(&coll, "Two.md", "# Two\n\nBeta.\n");
    writeNote(&coll, "Three.md", "# Three\n\nGamma.\n");
    coll.refresh();

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", true), 3);
    const QString html = readAll(QDir(dest.path()).filePath("collection.html"));

    // Three notes means two breaks: one before each note after the first.
    QCOMPARE(occurrences(html, "page-break-before"), 2);
    // The old shape put a bare rule between whole documents.
    QVERIFY(!html.contains("</html>\n<hr>"));
}

void TestDocumentExporter::testNestedListsNestInHtml()
{
    const QString html =
        m_exporter.htmlForMarkdown("- one\n  - nested\n  - also nested\n- two");
    QVERIFY(html.contains("<ul><li>one<ul><li>nested</li>"
                          "<li>also nested</li></ul></li><li>two</li></ul>"));
}

void TestDocumentExporter::testNestedNumberedAndTodoListsNest()
{
    const QString ordered = m_exporter.htmlForMarkdown("1. a\n  1. deep\n2. b");
    QVERIFY(ordered.contains("<ol><li>a<ol><li>deep</li></ol></li><li>b</li></ol>"));

    const QString todo =
        m_exporter.htmlForMarkdown("- [ ] top\n  - [x] child");
    QVERIFY(todo.contains("&#9744; top<ul><li>&#9745; child</li></ul>"));
}

// ---------- M7: per-note image base ----------

void TestDocumentExporter::testPerNoteImageBaseInCollectionExport()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    coll.createFolder("", "A");
    coll.createFolder("", "B");

    // Same file name in each folder, different bytes: whichever one the
    // export inlines identifies the base directory it resolved against.
    const QByteArray aBytes("AAAAAAAAAAAAAAAA");
    const QByteArray bBytes("BBBBBBBBBBBBBBBB");
    auto writeBinary = [&](const QString &rel, const QByteArray &bytes) {
        QFile f(coll.absolutePath(rel));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
        f.close();
    };
    writeBinary("A/pic.png", aBytes);
    writeBinary("B/pic.png", bBytes);
    writeNote(&coll, "A/one.md", "# One\n\n![](pic.png)\n");
    writeNote(&coll, "B/two.md", "# Two\n\n![](pic.png)\n");
    coll.refresh();

    // The dialog primes the context from whichever note is open; here that is
    // the one in A. The export must not carry it into the note in B.
    m_exporter.setImageContext(coll.absolutePath("A"), root.path());

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", false), 2);

    const QString aHtml = readAll(QDir(dest.path()).filePath("A/one.html"));
    const QString bHtml = readAll(QDir(dest.path()).filePath("B/two.html"));
    QVERIFY(aHtml.contains(QString::fromLatin1(aBytes.toBase64())));
    QVERIFY(bHtml.contains(QString::fromLatin1(bBytes.toBase64())));
}

void TestDocumentExporter::testLiveNoteSnapshotOverridesSavedBody()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "Open.md", "# Open\n\nSaved state.\n");
    coll.refresh();

    QTemporaryDir dest;

    // Without a snapshot the export reads what last reached disk. That is
    // correct for a note nobody is editing, and wrong for the open one.
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", false), 1);
    QVERIFY(readAll(QDir(dest.path()).filePath("Open.html"))
                .contains("Saved state."));

    // The editor holds newer content than the file does.
    BlockModel model;
    DocumentSerializer serializer;
    serializer.loadIntoModel(&model, "# Open\n\nUnsaved state.");
    m_exporter.setLiveNote("Open.md", &model);

    QTemporaryDir dest2;
    QCOMPARE(m_exporter.exportCollection(&coll, dest2.path(), "html", false), 1);
    const QString html = readAll(QDir(dest2.path()).filePath("Open.html"));
    QVERIFY(html.contains("Unsaved state."));
    QVERIFY(!html.contains("Saved state."));

    // The file itself is untouched: exporting is not a save.
    QFile f(coll.absolutePath("Open.md"));
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(f.readAll()).contains("Saved state."));

    m_exporter.clearLiveNote();
}

void TestDocumentExporter::testLiveNoteSnapshotIsIgnoredForOtherNotes()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "Open.md", "# Open\n\nSaved open.\n");
    writeNote(&coll, "Other.md", "# Other\n\nSaved other.\n");
    coll.refresh();

    BlockModel model;
    DocumentSerializer serializer;
    serializer.loadIntoModel(&model, "# Open\n\nUnsaved open.");
    m_exporter.setLiveNote("Open.md", &model);

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", false), 2);
    QVERIFY(readAll(QDir(dest.path()).filePath("Open.html"))
                .contains("Unsaved open."));
    // Every other note still exports from disk.
    QVERIFY(readAll(QDir(dest.path()).filePath("Other.html"))
                .contains("Saved other."));

    m_exporter.clearLiveNote();
}

QTEST_MAIN(TestDocumentExporter)
#include "test_documentexporter.moc"
