// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentexporter.h"
#include "blockmodel.h"
#include "block.h"
#include "notecollection.h"
#include "documentserializer.h"
#include "extensionregistry.h"
#include "faultinjection.h"

#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>

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
    void testExportedHrefsCarryOnlyNavigationalSchemes();
    void testEscapedPunctuationExportsBare();
    void testBulletAndNumberedLists();
    void testTodoCheckboxes();
    void testQuote();
    void testLineBreaksExportAsBreaks();
    void testDivider();
    void testCodeBlockHighlighted();
    void testCharacterDiagramExports();
    void testMermaidHtmlExport();
    void testMermaidScriptOnlyWithMermaid();
    void testTable();
    void testCallout();
    void testTocFenceBecomesAnchorList();
    void testBlockSubsetKeepsDocumentContext();
    void testInternalLinkAnchor();
    void testDisplayMathEmitsMathJaxDelimiters();
    void testInlineMathEmitsMathJaxDelimiters();
    void testMathJaxScriptTagInjectedOnlyWithMath();
    void testLiteralDollarsStayLiteral();
    void testMathJaxReferenceCorpusArtifact();
    void testMathExportPngModeArtifacts();
    void testPlainTextStructuralPrefixes();
    void testWriteHtmlFile();
    void testShortTextWritePreservesExistingExport();
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

    // APP-1: an export may never write over a note.
    void testMarkdownExportIntoTheVaultLeavesSourcesByteIdentical();
    void testMarkdownExportIntoASubfolderOfTheVaultIsRefused();
    void testCombinedExportOntoASourceIsRefused();
    void testExportOutsideTheVaultStillWorks();
    void testCollidingOutputsAreRefused();

    // APP-6: the collection export runs as a cancellable job with progress.
    void testJobExportReportsProgressAndWritesEveryNote();
    void testJobExportCanBeCancelledPartWay();
    void testOversizedAttachmentIsSkippedNotInlined();
    void testCombinedExportOverTheDocumentBudgetIsRefused();

    // Blocks the editor draws from something other than their own text: a
    // query's answer, an embed's preview card, a board card's chips. Each was
    // exporting as the source the reader never sees on screen, or as markup
    // that cannot render at all.
    void testQueryFenceRendersItsTable();
    void testQueryFenceBoardViewRendersColumnsOfCards();
    void testQueryFenceWithABadSpecReportsTheError();
    void testQueryFenceWithoutACollectionKeepsItsSource();
    void testEmbedUrlExportsAsALinkNotAnImage();
    void testImageUrlStillExportsAsAnImage();
    void testKanbanCardsCarryLabelsDueDatesAndDescriptions();
    void testImagesAreCappedToThePageWidth();
    void testRichBlockReviewArtifact();

    // The block's <!--kvit …--> presentation attributes, which an export
    // dropped entirely while it rendered a snapshot struct that had no
    // attributes field.
    void testParagraphAlignmentReachesTheExport();
    void testHeadingAlignmentReachesTheExport();
    void testDropCapCapsTheFirstRenderedCharacter();
    void testDividerStyleReachesTheExport();
    void testUnstyledBlocksExportWithoutStyleAttributes();
    void testImageEffectsAndAlignmentReachTheExport();
    void testCalloutColourOverrideReachesTheExport();
    void testTableColumnWidthsReachTheExport();
    void testAttributeColoursThatAreNotColoursAreDropped();

    // Plain-text export of the blocks that used to write their own source.
    void testKanbanFenceExportsAsABoardNotItsMarkdown();
    void testQueryFenceExportsItsAnswerAsText();
    void testTocFenceExportsTheDocumentsHeadings();
    void testTableExportsAsAnAlignedTextTable();
    void testCalloutAndMediaCarryWhatTheyAreInText();
    void testMermaidTextIsLabelledSource();
    void testDisplayMathKeepsItsTeXInText();
    void testTodoMetadataSurvivesTheText();
    void testNestedNumberedListsRestartTheirNumbering();

    // C7: what a linked module adds to a note's export. A module draws content
    // beside a note rather than in it, so it is in the note's block model
    // nowhere and was in no export of the note at all.
    void testNoModuleLeavesEveryExportUnchanged();
    void testTheContributionReachesEveryFormatOfTheNoteExport();
    void testTheContributionFollowsEachNoteThroughACollectionExport();
    void testTheContributionFollowsEachNoteIntoACombinedFile();
    void testARelativePictureResolvesAgainstTheModulesOwnBase();
    void testTwoModulesContributeInInstallationOrder();
    void testABlockScopeExportCarriesNoContribution();
    void testExportingDoesNotTouchTheNoteOrItsModel();

private:
    DocumentExporter m_exporter;

    // A note written straight to disk so its body is under test control.
    static void writeNote(NoteCollection *coll, const QString &relPath,
                          const QString &body)
    {
        const QString abs = coll->absolutePath(relPath);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly));
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

// An exported HTML file is opened by a browser, where an href is executable
// surface rather than an address. A note is untrusted input — whoever wrote it
// chose what its links say — so only schemes that navigate are written
// through, and a refused link keeps its text and loses its anchor.
void TestDocumentExporter::testExportedHrefsCarryOnlyNavigationalSchemes()
{
    const auto html = [this](const QString &md) {
        return m_exporter.htmlForMarkdown(md);
    };

    // The link parser refuses a URL containing parentheses, so these say what
    // they mean without them.
    const QString blocked = html("[click me](javascript:danger)");
    QVERIFY2(!blocked.contains(QStringLiteral("href=\"javascript:")),
             "an active scheme reached the exported document's href");
    QVERIFY2(blocked.contains(QStringLiteral("click me")),
             "the link's own text was dropped along with its target");

    // A whole document of the author's choosing, and the spellings a browser
    // reads as the same scheme but a text comparison does not.
    QVERIFY(!html("[x](data:text/html,<script>danger</script>)")
                 .contains(QStringLiteral("href=\"data:")));
    QVERIFY(!html("[x](JaVaScRiPt:danger)")
                 .contains(QStringLiteral("href=\"JaVaScRiPt:")));
    QVERIFY(!html("[x](vbscript:danger)")
                 .contains(QStringLiteral("href=\"vbscript:")));

    // What a note legitimately links to is untouched, relative paths and
    // fragments included.
    QVERIFY(html("[a](https://example.com/p)")
                .contains(QStringLiteral("<a href=\"https://example.com/p\">")));
    QVERIFY(html("[a](mailto:someone@example.com)")
                .contains(QStringLiteral("<a href=\"mailto:someone@example.com\">")));
    QVERIFY(html("[a](images/diagram.png)")
                .contains(QStringLiteral("<a href=\"images/diagram.png\">")));
    QVERIFY(html("[a](#a-heading)")
                .contains(QStringLiteral("<a href=\"#a-heading\">")));
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

// A line break inside a block is a break the reader sees in the editor, so
// the export has to carry it: a bare newline inside <p> or <li> collapses to
// a space in a browser and in QTextDocument, which is what prints the PDF.
void TestDocumentExporter::testLineBreaksExportAsBreaks()
{
    const QString para = m_exporter.htmlForMarkdown("wrapped\nline");
    QVERIFY(para.contains("<p>wrapped<br>line</p>"));

    const QString item = m_exporter.htmlForMarkdown("- one\n  two");
    QVERIFY(item.contains("<li>one<br>two</li>"));

    const QString quote = m_exporter.htmlForMarkdown("> a\n> b");
    QVERIFY(quote.contains("<blockquote>a<br>b</blockquote>"));

    // Formatting around a break still renders as formatting.
    const QString bold = m_exporter.htmlForMarkdown("**bold**\nafter");
    QVERIFY(bold.contains("<strong>bold</strong><br>after"));

    // A code block's newlines are the code: they stay literal in its <pre>
    // rather than turning into markup.
    const QString code = m_exporter.htmlForMarkdown("```\nfirst\nsecond\n```");
    QVERIFY(code.contains("first\nsecond"));
    QVERIFY(!code.contains("first<br>second"));
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

void TestDocumentExporter::testBlockSubsetKeepsDocumentContext()
{
    BlockModel model;
    DocumentSerializer serializer;
    serializer.loadIntoModel(
        &model, "# Intro\n\n```toc\n```\n\n## Details\n\n# Intro");
    QCOMPARE(model.count(), 4);

    // Only the TOC is emitted, but it remains a projection of the complete
    // note rather than an empty projection of itself.
    const QString html = m_exporter.htmlForModelBlocks(&model, {1});
    QVERIFY(html.contains("class=\"toc\""));
    QVERIFY(html.contains("<a href=\"#intro\">Intro</a>"));
    QVERIFY(html.contains("<a href=\"#details\">Details</a>"));
    QVERIFY(html.contains("<a href=\"#intro-1\">Intro</a>"));
    QVERIFY(!html.contains("<h1"));
    QVERIFY(!html.contains("<h2"));

    const QString text = m_exporter.plainTextForModelBlocks(&model, {1});
    QVERIFY(text.contains("Intro\n  Details\nIntro"));

    // A selected duplicate heading keeps the slug it owns in the original
    // document, not the unsuffixed slug it would get if rendered in isolation.
    const QString duplicate = m_exporter.htmlForModelBlocks(&model, {3});
    QVERIFY(duplicate.contains("id=\"intro-1\""));
    QVERIFY(!duplicate.contains("id=\"intro\""));

    QTemporaryDir out;
    const QString path = out.filePath("toc.html");
    QVERIFY(m_exporter.writeModelBlocks(&model, {1}, "Subset", "html", path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString written = QString::fromUtf8(file.readAll());
    QVERIFY(written.contains("href=\"#details\""));
    QVERIFY(!written.contains("<h2"));
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

void TestDocumentExporter::testShortTextWritePreservesExistingExport()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("out.txt");
    {
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("original export\n"), qint64(16));
    }
    {
        FaultInjection::FileSizeLimit limit(4096);
        if (!limit.supported())
            QSKIP(qPrintable(limit.skipReason()));
        QVERIFY(!m_exporter.writeMarkdownAs(QString(64 * 1024, 'x'), "T",
                                            "text", path));
    }
    QFile retained(path);
    QVERIFY(retained.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(retained.readAll(), QByteArray("original export\n"));
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

// ---------- APP-1: an export must never overwrite a note ----------

namespace {

QByteArray readBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

} // namespace

// Destination = the collection root, format = Markdown, one file per note.
// Every output path then lands exactly on the note it came from, and the
// export writes only the body — so tags, favourite/pinned state, custom fields
// and any foreign front matter were erased from the reader's own notes by
// exporting them. The whole plan is now built before anything is written, and
// a plan that touches a source refuses the export outright.
void TestDocumentExporter::testMarkdownExportIntoTheVaultLeavesSourcesByteIdentical()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "Kept.md",
              "---\ntags: [work, urgent]\nfavorite: true\nreviewer: ada\n---\n"
              "# Kept\n\nThe body.\n");
    writeNote(&coll, "Folder/Nested.md",
              "---\ntags: [nested]\n---\nNested body.\n");
    coll.refresh();

    const QByteArray keptBefore = readBytes(coll.absolutePath("Kept.md"));
    const QByteArray nestedBefore = readBytes(coll.absolutePath("Folder/Nested.md"));
    QVERIFY(!keptBefore.isEmpty());

    QSignalSpy refused(&m_exporter, &DocumentExporter::exportRefused);
    QCOMPARE(m_exporter.exportCollection(&coll, root.path(), "markdown", false), 0);
    QCOMPARE(refused.count(), 1);
    QVERIFY(!m_exporter.lastError().isEmpty());

    // Byte-identical: not merely "still has front matter".
    QCOMPARE(readBytes(coll.absolutePath("Kept.md")), keptBefore);
    QCOMPARE(readBytes(coll.absolutePath("Folder/Nested.md")), nestedBefore);
}

// The same hazard one level down: a subfolder of the vault is still the
// vault, and one collision there replaces a note with a metadata-free copy.
void TestDocumentExporter::testMarkdownExportIntoASubfolderOfTheVaultIsRefused()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "One.md", "---\ntags: [a]\n---\nBody one.\n");
    coll.refresh();
    const QByteArray before = readBytes(coll.absolutePath("One.md"));

    const QString inside = QDir(root.path()).filePath("Exports");
    QVERIFY(QDir().mkpath(inside));
    QCOMPARE(m_exporter.exportCollection(&coll, inside, "markdown", false), 0);
    QCOMPARE(readBytes(coll.absolutePath("One.md")), before);

    // HTML into the same place is fine: it cannot collide with a .md source.
    QCOMPARE(m_exporter.exportCollection(&coll, inside, "html", false), 1);
    QVERIFY(QFileInfo::exists(QDir(inside).filePath("One.html")));
    QCOMPARE(readBytes(coll.absolutePath("One.md")), before);
}

// A combined export writes one file named collection.<ext>. If the vault
// happens to hold a note by that name, the export would eat it.
void TestDocumentExporter::testCombinedExportOntoASourceIsRefused()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "collection.md", "---\ntags: [meta]\n---\nIndex of notes.\n");
    writeNote(&coll, "Other.md", "Another note.\n");
    coll.refresh();
    const QByteArray before = readBytes(coll.absolutePath("collection.md"));

    QSignalSpy refused(&m_exporter, &DocumentExporter::exportRefused);
    QCOMPARE(m_exporter.exportCollection(&coll, root.path(), "markdown", true), 0);
    QCOMPARE(refused.count(), 1);
    QCOMPARE(readBytes(coll.absolutePath("collection.md")), before);
}

void TestDocumentExporter::testExportOutsideTheVaultStillWorks()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "One.md", "---\ntags: [a]\n---\nBody one.\n");
    writeNote(&coll, "Folder/Two.md", "Body two.\n");
    coll.refresh();

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "markdown", false), 2);
    QVERIFY(m_exporter.lastError().isEmpty());
    // A standalone Markdown export is the note, metadata included: the
    // exported file is what you would copy back into a vault.
    QCOMPARE(readAll(QDir(dest.path()).filePath("One.md")),
             QString("---\ntags: [a]\n---\nBody one.\n"));
    QCOMPARE(readAll(QDir(dest.path()).filePath("Folder/Two.md")),
             QString("Body two.\n"));
}

// Two notes whose names differ only in the extension the export strips would
// be written to the same file; the second silently replaced the first.
void TestDocumentExporter::testCollidingOutputsAreRefused()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, "Report.md", "Markdown report.\n");
    coll.refresh();

    QTemporaryDir dest;
    QSignalSpy refused(&m_exporter, &DocumentExporter::exportRefused);
    QCOMPARE(m_exporter.exportNotes(
                 &coll, QStringList{"Report.md", "Report.md"}, dest.path(),
                 "html", false),
             0);
    QCOMPARE(refused.count(), 1);
    QVERIFY(!QFileInfo::exists(QDir(dest.path()).filePath("Report.html")));
}

// ---------- APP-6: the export is a job, not a blocking loop ----------

void TestDocumentExporter::testJobExportReportsProgressAndWritesEveryNote()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    for (int i = 0; i < 5; ++i)
        writeNote(&coll, QStringLiteral("Note%1.md").arg(i),
                  QStringLiteral("# Note %1\n\nBody %1\n").arg(i));
    coll.refresh();

    QTemporaryDir dest;
    QSignalSpy progress(&m_exporter, &DocumentExporter::exportProgress);
    QSignalSpy finished(&m_exporter, &DocumentExporter::exportFinished);

    QVERIFY(m_exporter.startExportCollection(&coll, dest.path(), "html", false));
    QVERIFY2(m_exporter.busy(), "the job must report itself running");
    // Nothing is written before control returns to the event loop.
    QCOMPARE(progress.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(!m_exporter.busy());
    QCOMPARE(progress.count(), 5);
    QCOMPARE(finished.at(0).at(0).toInt(), 5);   // written
    QVERIFY(!finished.at(0).at(2).toBool());     // not cancelled
    QCOMPARE(finished.at(0).at(3).toString(), QString());
    for (int i = 0; i < 5; ++i) {
        QVERIFY(QFileInfo::exists(
            QDir(dest.path()).filePath(QStringLiteral("Note%1.html").arg(i))));
    }
}

// The point of yielding between notes: a Cancel that arrives part way is
// acted on, and the notes already written stay written.
void TestDocumentExporter::testJobExportCanBeCancelledPartWay()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    for (int i = 0; i < 8; ++i)
        writeNote(&coll, QStringLiteral("N%1.md").arg(i),
                  QStringLiteral("Body %1\n").arg(i));
    coll.refresh();

    QTemporaryDir dest;
    QSignalSpy finished(&m_exporter, &DocumentExporter::exportFinished);
    connect(&m_exporter, &DocumentExporter::exportProgress, &m_exporter,
            [this](int done, int, const QString &) {
                if (done == 3)
                    m_exporter.cancelExport();
            });

    QVERIFY(m_exporter.startExportCollection(&coll, dest.path(), "html", false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

    QVERIFY(!m_exporter.busy());
    QCOMPARE(finished.at(0).at(0).toInt(), 3);  // written
    QVERIFY(finished.at(0).at(2).toBool());     // cancelled
    QVERIFY(QFileInfo::exists(QDir(dest.path()).filePath("N0.html")));
    QVERIFY(!QFileInfo::exists(QDir(dest.path()).filePath("N7.html")));
}

// Budgets. An attachment over the cap is left out of the output instead of
// being read whole and Base64-expanded into it.
void TestDocumentExporter::testOversizedAttachmentIsSkippedNotInlined()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    const QByteArray big(200 * 1024, 'Z');
    {
        QFile f(coll.absolutePath("big.png"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(big);
    }
    writeNote(&coll, "WithImage.md", "# With image\n\n![](big.png)\n");
    coll.refresh();

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", false), 1);
    QVERIFY2(readAll(QDir(dest.path()).filePath("WithImage.html"))
                 .contains(QString::fromLatin1(big.toBase64())),
             "an attachment under the budget is inlined as before");

    m_exporter.setMaxAttachmentBytes(1024);
    QTemporaryDir dest2;
    QCOMPARE(m_exporter.exportCollection(&coll, dest2.path(), "html", false), 1);
    const QString html = readAll(QDir(dest2.path()).filePath("WithImage.html"));
    QVERIFY2(!html.contains(QString::fromLatin1(big.toBase64())),
             "an attachment over the budget must not be inlined");
    QVERIFY(html.contains(QStringLiteral("With image")));

    m_exporter.setMaxAttachmentBytes(64LL * 1024 * 1024);
}

// A combined export holds the whole document in memory because the PDF
// printer and the HTML wrapper both need it whole. Over the budget it stops
// rather than growing until the process dies.
void TestDocumentExporter::testCombinedExportOverTheDocumentBudgetIsRefused()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    for (int i = 0; i < 6; ++i)
        writeNote(&coll, QStringLiteral("Big%1.md").arg(i),
                  QString(4000, QLatin1Char('w')) + QStringLiteral("\n"));
    coll.refresh();

    QTemporaryDir dest;
    m_exporter.setMaxCombinedChars(5000);
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", true), 0);
    QVERIFY(!m_exporter.lastError().isEmpty());

    m_exporter.setMaxCombinedChars(128LL * 1024 * 1024);
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", true), 6);
}

// ---------- blocks whose rendering is not their own text ----------

namespace {

// Four project notes, enough for a query to filter, sort and group over.
void seedProjects(NoteCollection *coll, const QString &root)
{
    const auto write = [&](const QString &rel, const QString &text) {
        const QString abs = QDir(root).filePath(rel);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    write("Projects/Alpha.md",
          "---\nstatus: active\nowner: Dana\ndue: 2026-08-01\n---\nAlpha\n");
    write("Projects/Beta.md",
          "---\nstatus: active\nowner: Rio\ndue: 2026-07-15\n---\nBeta\n");
    write("Projects/Gamma.md",
          "---\nstatus: done\nowner: Dana\n---\nGamma\n");
    write("Elsewhere.md", "Not a project\n");
    QVERIFY(coll->openRoot(root));
}

} // namespace

void TestDocumentExporter::testQueryFenceRendersItsTable()
{
    QTemporaryDir root;
    NoteCollection coll;
    seedProjects(&coll, root.path());
    m_exporter.setCollection(&coll);

    const QString html = m_exporter.htmlForMarkdown(
        "```query\nfrom: Projects/\nwhere: status = active\n"
        "columns: title, owner, due\nsort: due asc\n```");

    // The answer, not the question: a table of the matching notes.
    QVERIFY(html.contains("class=\"query\""));
    QVERIFY(html.contains("<th>title</th>"));
    QVERIFY(html.contains("<th>owner</th>"));
    QVERIFY(html.contains("<td>Beta</td>"));
    QVERIFY(html.contains("<td>Dana</td>"));
    QVERIFY(html.contains("2 notes"));
    // The two notes the spec excludes stay out, and the spec itself is not
    // printed as source.
    QVERIFY(!html.contains("Gamma"));
    QVERIFY(!html.contains("Elsewhere"));
    QVERIFY(!html.contains("where: status = active"));
    // Beta is due first, so it sorts above Alpha.
    QVERIFY(html.indexOf("Beta") < html.indexOf("Alpha"));
    m_exporter.setCollection(nullptr);
}

void TestDocumentExporter::testQueryFenceBoardViewRendersColumnsOfCards()
{
    QTemporaryDir root;
    NoteCollection coll;
    seedProjects(&coll, root.path());
    m_exporter.setCollection(&coll);

    const QString html = m_exporter.htmlForMarkdown(
        "```query\nfrom: Projects/\nview: board\ngroup-by: status\n"
        "columns: title, owner\n```");

    // The same columns-of-cards shape a task board exports as.
    QVERIFY(html.contains("class=\"kanban\""));
    QVERIFY(html.contains("<strong>active</strong>"));
    QVERIFY(html.contains("<strong>done</strong>"));
    QVERIFY(html.contains("class=\"card\""));
    QVERIFY(html.contains("Gamma"));
    QVERIFY(!html.contains("group-by: status"));
    m_exporter.setCollection(nullptr);
}

void TestDocumentExporter::testQueryFenceWithABadSpecReportsTheError()
{
    QTemporaryDir root;
    NoteCollection coll;
    seedProjects(&coll, root.path());
    m_exporter.setCollection(&coll);

    // "sortby" is not a key; the block shows the parse error rather than
    // guessing, and the export says the same.
    const QString html = m_exporter.htmlForMarkdown(
        "```query\nfrom: Projects/\nsortby: due\n```");
    QVERIFY(html.contains("class=\"error\""));
    QVERIFY(html.contains("sortby"));
    m_exporter.setCollection(nullptr);
}

void TestDocumentExporter::testQueryFenceWithoutACollectionKeepsItsSource()
{
    // Single-file mode: there is no vault to ask, and an empty table would
    // claim the query matched nothing.
    m_exporter.setCollection(nullptr);
    const QString html = m_exporter.htmlForMarkdown(
        "```query\nfrom: Projects/\nwhere: status = active\n```");
    QVERIFY(html.contains("where: status = active"));
    QVERIFY(!html.contains("class=\"query\""));
}

void TestDocumentExporter::testEmbedUrlExportsAsALinkNotAnImage()
{
    // An image expression whose URL is a web page renders as a preview card
    // in the editor. An <img> pointed at a page is a broken image everywhere.
    const QString html = m_exporter.htmlForMarkdown("![](https://example.com/wiki)");
    QVERIFY(html.contains("class=\"embed\""));
    QVERIFY(html.contains("<a href=\"https://example.com/wiki\">"));
    QVERIFY(html.contains("example.com"));
    QVERIFY(!html.contains("<img"));
}

void TestDocumentExporter::testImageUrlStillExportsAsAnImage()
{
    // The classifier is by extension, so a remote *image* is unaffected by
    // the embed branch and still exports as a picture.
    const QString html = m_exporter.htmlForMarkdown("![cat](https://example.com/cat.png)");
    QVERIFY(html.contains("<img"));
    QVERIFY(html.contains("https://example.com/cat.png"));
    QVERIFY(!html.contains("class=\"embed\""));
}

void TestDocumentExporter::testKanbanCardsCarryLabelsDueDatesAndDescriptions()
{
    const QString html = m_exporter.htmlForMarkdown(
        "```kanban\n## To do\n- [ ] Ship the beta #release \xF0\x9F\x93\x85 2026-08-01\n"
        "  Needs the installer signed first\n## Done\n- [x] Write the notes\n```");
    QVERIFY(html.contains("Ship the beta"));
    // The chips the board shows under the title, and the description below it.
    QVERIFY(html.contains("class=\"chip\">release</span>"));
    // The due date is a chip of its own, taken OFF the title — the board
    // shows it that way, and leaving the marker in the title would print a
    // calendar emoji mid-sentence.
    QVERIFY(html.contains("class=\"chip\">&#128197; 2026-08-01</span>"));
    QVERIFY(!html.contains("Ship the beta \xF0\x9F\x93\x85"));
    QVERIFY(html.contains("Needs the installer signed first"));
    // The column heading still carries its card count.
    QVERIFY(html.contains("<strong>To do</strong>"));
    QVERIFY(html.contains("&#9745;"));   // the finished card stays ticked
}

void TestDocumentExporter::testImagesAreCappedToThePageWidth()
{
    // QString::arg has no escape for a percent sign, so a doubled one used to
    // reach the stylesheet verbatim and the browser discarded the rule.
    const QString html = m_exporter.htmlForMarkdown("text");
    QVERIFY(html.contains("img{max-width:100%}"));
    QVERIFY(!html.contains("100%%"));
}

void TestDocumentExporter::testRichBlockReviewArtifact()
{
    // The browser-review artifact for everything the editor draws from
    // something other than its own text, in one file: a query as a table and
    // as a board, a task board with chips, an embed card, a remote image, a
    // Mermaid diagram and a character diagram. A headless run can only check
    // that each arrived; whether it LOOKS right is the manual QA step, which
    // opens this file.
    QTemporaryDir root;
    NoteCollection coll;
    seedProjects(&coll, root.path());
    m_exporter.setCollection(&coll);

    const QString markdown = QStringLiteral(
        "# Export review: blocks that render from something else\n\n"
        "## Query, table view\n\n"
        "```query\nfrom: Projects/\nwhere: status = active\n"
        "columns: title, owner, due\nsort: due asc\n```\n\n"
        "## Query, board view\n\n"
        "```query\nfrom: Projects/\nview: board\ngroup-by: status\n"
        "columns: title, owner\n```\n\n"
        "## Query with a bad spec\n\n"
        "```query\nfrom: Projects/\nsortby: due\n```\n\n"
        "## Task board\n\n"
        "```kanban\n## To do\n"
        "- [ ] Ship the beta #release #urgent 📅 2026-08-01\n"
        "  Needs the installer signed first\n"
        "## Done\n- [x] Write the release notes\n```\n\n"
        "## Web embed\n\n"
        "![](https://example.com/wiki/Kvit)\n\n"
        "## Remote image\n\n"
        "![A remote picture](https://example.com/cat.png)\n\n"
        "## Mermaid\n\n"
        "```mermaid\nflowchart LR\n  A([Start]) --> B{Vault set?}\n"
        "  B -- yes --> C[Open collection]\n```\n\n"
        "## Character diagram\n\n"
        "```diagram\n┌────┐\n│ A  │\n└────┘\n```\n\n"
        "## Prose, links and to-dos\n\n"
        "A ==highlight==, a [[Projects/Alpha]] wiki-link, and a link to "
        "[the web](https://example.com).\n\n"
        "- [ ] Ship the beta 📅 2026-08-01 ⏫\n");

    QString dir = qEnvironmentVariable("KVIT_SHOT_DIR");
    if (dir.isEmpty())
        dir = QDir::currentPath() + QStringLiteral("/screenshots");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/rich_blocks_export.html");
    QVERIFY(m_exporter.writeMarkdownAs(markdown, "Rich block export review",
                                       "html", path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString html = QString::fromUtf8(f.readAll());
    QCOMPARE(html.count("class=\"query\""), 3);
    QCOMPARE(html.count("class=\"embed\""), 1);
    QCOMPARE(html.count("class=\"kanban\""), 2);   // the board, and the query's
    QVERIFY(html.contains("<pre class=\"mermaid\">"));
    QVERIFY(html.contains("<pre class=\"text-diagram\">"));
    QVERIFY(html.contains("<img"));
    m_exporter.setCollection(nullptr);
}

// ---- presentation attributes ----
//
// Everything below covers the same defect: a block's <!--kvit …--> payload
// never reached the HTML builder, because the builder rendered a private copy
// of the block struct with `attributes` left out and both loops that filled it
// dropped the field. So alignment, drop caps, divider styles, image effects,
// callout colours and table column widths were absent from every HTML and PDF
// export, and no test noticed because no test had an attribute in it.

void TestDocumentExporter::testParagraphAlignmentReachesTheExport()
{
    const QString html =
        m_exporter.htmlForMarkdown("centred  <!--kvit align=center-->");
    QVERIFY(html.contains("<p style=\"text-align:center\">centred</p>"));

    const QString right =
        m_exporter.htmlForMarkdown("right  <!--kvit align=right-->");
    QVERIFY(right.contains("<p style=\"text-align:right\">right</p>"));
}

void TestDocumentExporter::testHeadingAlignmentReachesTheExport()
{
    const QString html =
        m_exporter.htmlForMarkdown("# Title  <!--kvit align=center-->");
    QVERIFY(html.contains(
        "<h1 id=\"title\" style=\"text-align:center\">Title</h1>"));
}

// The initial is the first character the reader sees, not the first character
// of the markdown: a paragraph opening in bold caps the letter, not the
// asterisk, and stays bold. That is what the editor's overlay does, and
// walking the rendered HTML gets the same answer.
void TestDocumentExporter::testDropCapCapsTheFirstRenderedCharacter()
{
    const QString plain =
        m_exporter.htmlForMarkdown("Once upon a time  <!--kvit dropcap=3-->");
    QVERIFY(plain.contains("<span class=\"dropcap\" style=\"font-size:3.45em\">O"
                           "</span>nce upon a time"));

    const QString bold = m_exporter.htmlForMarkdown(
        "**Bold** opening  <!--kvit dropcap=3-->");
    QVERIFY(bold.contains("<strong><span class=\"dropcap\""));
    QVERIFY(bold.contains(">B</span>old</strong>"));

    // Under two lines is not a drop cap, which is the delegate's rule too.
    const QString off =
        m_exporter.htmlForMarkdown("Small  <!--kvit dropcap=1-->");
    QVERIFY(!off.contains("<span class=\"dropcap\""));

    // The stored colour and family ride the span.
    const QString styled = m_exporter.htmlForMarkdown(
        "Fancy  <!--kvit dropcap=4 dropcapcolor=#c1121f dropcapfont=Georgia-->");
    QVERIFY(styled.contains("color:#c1121f"));
    QVERIFY(styled.contains("font-family:'Georgia'"));
}

void TestDocumentExporter::testDividerStyleReachesTheExport()
{
    const QString dashed =
        m_exporter.htmlForMarkdown("---  <!--kvit style=dashed thickness=4-->");
    QVERIFY(dashed.contains("border-top-width:4px"));
    QVERIFY(dashed.contains("border-top-style:dashed"));

    const QString half =
        m_exporter.htmlForMarkdown("---  <!--kvit width=50%-->");
    QVERIFY(half.contains("width:50%"));
    QVERIFY(half.contains("margin-left:auto"));

    // The decorative rule is a diamond between two segments, which is the
    // motif the canvas paints.
    const QString deco =
        m_exporter.htmlForMarkdown("---  <!--kvit style=decorative-->");
    QVERIFY(deco.contains("class=\"hr-deco\""));
    QVERIFY(deco.contains("&#9670;"));
}

// The common case is a block with no attributes at all, and it must export
// byte-identically to what it exported before any of this existed.
void TestDocumentExporter::testUnstyledBlocksExportWithoutStyleAttributes()
{
    const QString html = m_exporter.htmlForMarkdown(
        "Plain paragraph\n\n# Plain heading\n\n---\n\n| A | B |\n| --- | --- |\n| 1 | 2 |");
    QVERIFY(html.contains("<p>Plain paragraph</p>"));
    QVERIFY(html.contains("<h1 id=\"plain-heading\">Plain heading</h1>"));
    QVERIFY(html.contains("<hr>"));
    QVERIFY(html.contains("<table><tr><th>A</th>"));
    QVERIFY(!html.contains("<colgroup>"));
}

void TestDocumentExporter::testImageEffectsAndAlignmentReachTheExport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.path() + QStringLiteral("/pic.png");
    QImage(4, 4, QImage::Format_RGB32).save(png);
    m_exporter.setImageContext(dir.path(), dir.path());

    const QString html = m_exporter.htmlForMarkdown(
        "![alt](pic.png)  <!--kvit align=left rounded=8 shadow border-->");
    QVERIFY(html.contains("border-radius:8px"));
    QVERIFY(html.contains("box-shadow:"));
    QVERIFY(html.contains("<figure style=\"text-align:left\">"));
    // A bare `border` flag means the theme's own border colour, which the
    // block cannot reach: the stylesheet names it once for this class.
    QVERIFY(html.contains("<img alt=\"alt\""));
    QVERIFY(html.contains("class=\"bordered\""));
    QVERIFY(html.contains("img.bordered{border:1px solid"));

    // A colour of its own is written on the image, not through the class.
    const QString custom = m_exporter.htmlForMarkdown(
        "![alt](pic.png)  <!--kvit border=#c1121f-->");
    QVERIFY(custom.contains("border:1px solid #c1121f"));
    QVERIFY(!custom.contains("class=\"bordered\""));

    // A bare `rounded` flag means the delegate's default radius.
    const QString bare = m_exporter.htmlForMarkdown(
        "![alt](pic.png)  <!--kvit rounded-->");
    QVERIFY(bare.contains("border-radius:12px"));

    // Centred is the image default on screen, so it writes no rule.
    const QString plain = m_exporter.htmlForMarkdown("![alt](pic.png)");
    QVERIFY(plain.contains("<figure>"));
    m_exporter.setImageContext(QString(), QString());
}

void TestDocumentExporter::testCalloutColourOverrideReachesTheExport()
{
    const QString html = m_exporter.htmlForMarkdown(
        "> [!info] Heads up  <!--kvit color=#2970c8-->\n> body");
    QVERIFY(html.contains("border-left-color:#2970c8"));
    QVERIFY(html.contains("<div class=\"title\" style=\"color:#2970c8\">"));
}

void TestDocumentExporter::testTableColumnWidthsReachTheExport()
{
    const QString html = m_exporter.htmlForMarkdown(
        "| A | B |  <!--kvit cols=120,0-->\n| --- | --- |\n| 1 | 2 |");
    QVERIFY(html.contains("<colgroup><col style=\"width:120px\"><col></colgroup>"));
}

// A note is untrusted input: it can arrive by import or by sync, and its
// bytes end up inside a style attribute of a document the reader may pass on.
// A payload that is not a colour writes no declaration rather than escaping
// into one of its own.
void TestDocumentExporter::testAttributeColoursThatAreNotColoursAreDropped()
{
    const QString html = m_exporter.htmlForMarkdown(
        "---  <!--kvit color=red;background:url(http://x)-->");
    QVERIFY(!html.contains("background:url"));
    QVERIFY(!html.contains("border-top-color"));

    const QString quoted = m_exporter.htmlForMarkdown(
        "> [!info] T  <!--kvit color=\"#fff\"-->\n> body");
    QVERIFY(!quoted.contains("border-left-color"));

    // A bare colour word and a hex triplet are both real colours and survive.
    const QString word =
        m_exporter.htmlForMarkdown("---  <!--kvit color=crimson-->");
    QVERIFY(word.contains("border-top-color:crimson"));
}

// ---- plain-text export ----
//
// A `.txt` export wrote a fenced block's source, which for a query is its
// `from:`/`where:` spec, for a board is the kanban markdown and for a table is
// the pipe syntax — the one part of each block a reader never sees on screen.
// Each of these asserts that what the editor draws is what the text file says.

void TestDocumentExporter::testKanbanFenceExportsAsABoardNotItsMarkdown()
{
    const QString text = m_exporter.plainTextForMarkdown(
        "```kanban\n## To do\n- [ ] Ship it #release 📅 2026-07-15\n"
        "  Needs a changelog\n- [x] Draft the notes\n## Done\n```");

    QVERIFY(text.contains("To do (2)"));
    QVERIFY(text.contains("Done (0)"));
    QVERIFY(text.contains("[ ] Ship it"));
    QVERIFY(text.contains("[x] Draft the notes"));
    QVERIFY(text.contains("#release"));
    QVERIFY(text.contains("(due 2026-07-15)"));
    QVERIFY(text.contains("Needs a changelog"));
    // Not the fence's own markdown.
    QVERIFY(!text.contains("## To do"));
    QVERIFY(!text.contains("- [ ] Ship it"));
}

void TestDocumentExporter::testQueryFenceExportsItsAnswerAsText()
{
    QTemporaryDir root;
    NoteCollection coll;
    seedProjects(&coll, root.path());
    m_exporter.setCollection(&coll);

    const QString text = m_exporter.plainTextForMarkdown(
        "```query\nfrom: Projects/\nwhere: status = active\n"
        "columns: title, owner, due\nsort: due asc\n```");

    QVERIFY(text.contains("2 notes"));
    QVERIFY(text.contains("Beta"));
    QVERIFY(text.contains("Dana"));
    QVERIFY(!text.contains("where: status = active"));
    QVERIFY(text.indexOf("Beta") < text.indexOf("Alpha"));

    m_exporter.setCollection(nullptr);
    // With no vault there is nothing to ask, so the spec goes out as source
    // rather than as a table claiming the query matched nothing.
    const QString orphan = m_exporter.plainTextForMarkdown(
        "```query\nfrom: Projects/\n```");
    QVERIFY(orphan.contains("from: Projects/"));
}

void TestDocumentExporter::testTocFenceExportsTheDocumentsHeadings()
{
    const QString text = m_exporter.plainTextForMarkdown(
        "# One\n\n```toc\nstale\n```\n\n## Under one\n\n# Two");
    QVERIFY(text.contains("One\n  Under one\nTwo"));
    // The fence's own body is written by the editor as the reader types and
    // is stale in a note nobody has opened; the export reads the document.
    QVERIFY(!text.contains("stale"));
}

void TestDocumentExporter::testTableExportsAsAnAlignedTextTable()
{
    const QString text = m_exporter.plainTextForMarkdown(
        "| Name | Owner |\n| --- | --- |\n| Alpha | Dana |\n| B | R |");
    QVERIFY(text.contains("Name  | Owner"));
    QVERIFY(text.contains("------+------"));
    QVERIFY(text.contains("Alpha | Dana"));
    QVERIFY(text.contains("B     | R"));
    QVERIFY(!text.contains("| --- |"));
}

void TestDocumentExporter::testCalloutAndMediaCarryWhatTheyAreInText()
{
    const QString callout = m_exporter.plainTextForMarkdown(
        "> [!warning] Careful\n> Mind the gap");
    QVERIFY(callout.contains("[WARNING] Careful"));
    QVERIFY(callout.contains("  Mind the gap"));

    const QString image =
        m_exporter.plainTextForMarkdown("![A diagram](pic.png \"the caption\")");
    QVERIFY(image.contains("[image: A diagram] pic.png"));
    QVERIFY(image.contains("the caption"));

    const QString embed =
        m_exporter.plainTextForMarkdown("![](https://example.com/page)");
    QVERIFY(embed.contains("[embed] https://example.com/page"));
}

void TestDocumentExporter::testMermaidTextIsLabelledSource()
{
    const QString text = m_exporter.plainTextForMarkdown(
        "```mermaid\nflowchart LR\nA-->B\n```");
    // There is no text rendering of a diagram, so the source stays — but
    // labelled, so `A-->B` does not read as a line of prose.
    QVERIFY(text.contains("[mermaid diagram]"));
    QVERIFY(text.contains("flowchart LR"));
}

// Display math is verbatim: running its TeX through the inline-markdown pass
// eats the `_`, `^` and `*` that carry the formula.
void TestDocumentExporter::testDisplayMathKeepsItsTeXInText()
{
    const QString text = m_exporter.plainTextForMarkdown("$$\na_1 * b^2\n$$");
    QVERIFY(text.contains("a_1 * b^2"));
}

// A to-do's due date and priority are drawn as chips beside its text, so a
// reader sees them and an export carries them. The three text projections
// strip them, because a word count and a search index should not carry an
// emoji tail; an export is neither of those.
void TestDocumentExporter::testTodoMetadataSurvivesTheText()
{
    const QString text =
        m_exporter.plainTextForMarkdown("- [ ] Ship it 📅 2026-07-15 ⏫");
    QVERIFY(text.contains("[ ] Ship it"));
    QVERIFY(text.contains("2026-07-15"));
    QVERIFY(text.contains("⏫"));
}

// One counter per indent level, as the editor numbers them. A flat counter
// numbers a two-level list 1, 2, 3, 4.
void TestDocumentExporter::testNestedNumberedListsRestartTheirNumbering()
{
    const QString text = m_exporter.plainTextForMarkdown(
        "1. one\n   1. sub one\n   2. sub two\n2. two");
    QVERIFY(text.contains("1. one"));
    QVERIFY(text.contains("  1. sub one"));
    QVERIFY(text.contains("  2. sub two"));
    QVERIFY(text.contains("2. two"));
}

// ---- what a linked module adds to a note's export (C7) ----
//
// A module contributes MARKDOWN for one note, given that note's vault-relative
// path, and the exporter renders it exactly as it renders the note's own — so
// it reaches all four formats and every scope that exports whole notes without
// the module knowing about any of them. The cases below are that claim, plus
// the one that has to hold for the open build: with nothing installed, every
// export is byte-identical to what it was.

namespace {

// A stand-in for a linked module. It contributes a paragraph to one named
// note and nothing to any other, which is what lets a case assert both that
// the contribution arrives and that a note the module has nothing to say about
// is untouched.
class ContributingModule : public KvitExtension
{
public:
    ContributingModule(QString name, QString relPath, QString markdown,
                       QString baseDir = QString())
        : m_name(std::move(name)), m_relPath(std::move(relPath)),
          m_markdown(std::move(markdown)), m_baseDir(std::move(baseDir))
    {
    }

    QString name() const override { return m_name; }
    QString qmlNamespace() const override { return m_name; }

    QString exportAppendix(const QString &noteRelPath) const override
    {
        return noteRelPath == m_relPath ? m_markdown : QString();
    }
    QString exportAppendixBaseDir() const override { return m_baseDir; }
    QString exportAppendixLabel() const override
    {
        return QStringLiteral("Notes from ") + m_name;
    }

private:
    QString m_name;
    QString m_relPath;
    QString m_markdown;
    QString m_baseDir;
};

// Anything a module might add for any note.
class SilentModule : public KvitExtension
{
public:
    QString name() const override { return QStringLiteral("silent"); }
    QString qmlNamespace() const override { return QStringLiteral("silent"); }
};

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

} // namespace

// The open build's guarantee: no module installed, and no export changes by a
// byte. A registry that is wired but empty is the same case, since a module
// that answers nothing has to be indistinguishable from no module at all.
void TestDocumentExporter::testNoModuleLeavesEveryExportUnchanged()
{
    BlockModel model;
    model.insertBlock(0, Block::Heading1, QStringLiteral("Report"));
    model.insertBlock(1, Block::Paragraph, QStringLiteral("The note's own text."));

    DocumentSerializer serializer;
    DocumentExporter bare;
    m_exporter.setLiveNote(QStringLiteral("Report.md"), &model);

    ExtensionRegistry empty;
    empty.install(std::make_unique<SilentModule>());
    m_exporter.setExtensions(&empty);

    QCOMPARE(m_exporter.htmlForModel(&model, "Report"),
             bare.htmlForModel(&model, "Report"));
    QCOMPARE(m_exporter.plainTextForModel(&model), bare.plainTextForModel(&model));

    QTemporaryDir dir;
    const QString withRegistry = dir.filePath("with.md");
    const QString withoutRegistry = dir.filePath("without.md");
    QVERIFY(m_exporter.writeModel(&model, "Report", "markdown", withRegistry));
    QVERIFY(bare.writeModel(&model, "Report", "markdown", withoutRegistry));
    QCOMPARE(readFile(withRegistry), readFile(withoutRegistry));
    QCOMPARE(readFile(withRegistry), serializer.serialize(&model));

    m_exporter.setExtensions(nullptr);
    m_exporter.clearLiveNote();
}

void TestDocumentExporter::testTheContributionReachesEveryFormatOfTheNoteExport()
{
    BlockModel model;
    model.insertBlock(0, Block::Heading1, QStringLiteral("Report"));
    model.insertBlock(1, Block::Paragraph, QStringLiteral("The note's own text."));

    ExtensionRegistry registry;
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("demo"), QStringLiteral("Report.md"),
        QStringLiteral("## Added beside the note\n\n"
                       "A paragraph the module drew.\n")));
    m_exporter.setExtensions(&registry);
    // The single-note scope renders the live model, so the note being exported
    // is named the one way the exporter has of knowing it.
    m_exporter.setLiveNote(QStringLiteral("Report.md"), &model);

    const QString html = m_exporter.htmlForModel(&model, "Report");
    QVERIFY2(html.contains("A paragraph the module drew"), qPrintable(html));
    QVERIFY(html.contains("Added beside the note"));
    // Rendered as markdown of the same document rather than pasted in: the
    // heading became a heading, and the whole thing is one HTML document.
    QVERIFY(html.contains("<h2"));
    QCOMPARE(html.count(QStringLiteral("<html")), 1);

    const QString text = m_exporter.plainTextForModel(&model);
    QVERIFY2(text.contains("A paragraph the module drew"), qPrintable(text));
    // After the note's own text, not before it.
    QVERIFY(text.indexOf("The note's own text")
            < text.indexOf("A paragraph the module drew"));

    QTemporaryDir dir;
    const QString mdPath = dir.filePath("out.md");
    QVERIFY(m_exporter.writeModel(&model, "Report", "markdown", mdPath));
    const QString markdown = readFile(mdPath);
    QVERIFY2(markdown.contains("## Added beside the note"), qPrintable(markdown));
    QVERIFY(markdown.startsWith("# Report"));

    // PDF has no text to read back, so it is measured against the same note
    // exported without the contribution: the extra page content makes a
    // larger file.
    const QString pdfPath = dir.filePath("out.pdf");
    QVERIFY(m_exporter.writeModel(&model, "Report", "pdf", pdfPath));
    m_exporter.setExtensions(nullptr);
    const QString barePdfPath = dir.filePath("bare.pdf");
    QVERIFY(m_exporter.writeModel(&model, "Report", "pdf", barePdfPath));
    QVERIFY2(QFileInfo(pdfPath).size() > QFileInfo(barePdfPath).size(),
             "the PDF with a contribution is no larger than the one without");

    m_exporter.clearLiveNote();
}

void TestDocumentExporter::testTheContributionFollowsEachNoteThroughACollectionExport()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, QStringLiteral("Alpha.md"), QStringLiteral("Alpha body.\n"));
    writeNote(&coll, QStringLiteral("Beta.md"), QStringLiteral("Beta body.\n"));
    coll.refresh();

    ExtensionRegistry registry;
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("demo"), QStringLiteral("Alpha.md"),
        QStringLiteral("Only Alpha carries this.\n")));
    m_exporter.setExtensions(&registry);

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", false), 2);
    const QString alpha = readFile(QDir(dest.path()).filePath("Alpha.html"));
    const QString beta = readFile(QDir(dest.path()).filePath("Beta.html"));
    QVERIFY2(alpha.contains("Only Alpha carries this"), qPrintable(alpha));
    // The registry is asked per note, so a note the module has nothing to add
    // to is exported exactly as it would have been.
    QVERIFY2(!beta.contains("Only Alpha carries this"), qPrintable(beta));

    // And the markdown scope keeps the note's front matter in front of the
    // body, with the contribution after it.
    QTemporaryDir mdDest;
    QCOMPARE(m_exporter.exportCollection(&coll, mdDest.path(), "markdown", false),
             2);
    const QString alphaMd = readFile(QDir(mdDest.path()).filePath("Alpha.md"));
    QVERIFY(alphaMd.indexOf("Alpha body")
            < alphaMd.indexOf("Only Alpha carries this"));

    m_exporter.setExtensions(nullptr);
}

void TestDocumentExporter::testTheContributionFollowsEachNoteIntoACombinedFile()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, QStringLiteral("Alpha.md"), QStringLiteral("Alpha body.\n"));
    writeNote(&coll, QStringLiteral("Beta.md"), QStringLiteral("Beta body.\n"));
    coll.refresh();

    ExtensionRegistry registry;
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("demo"), QStringLiteral("Alpha.md"),
        QStringLiteral("Alpha's appendix.\n")));
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("other"), QStringLiteral("Beta.md"),
        QStringLiteral("Beta's appendix.\n")));
    m_exporter.setExtensions(&registry);

    QTemporaryDir dest;
    QVERIFY(m_exporter.exportCollection(&coll, dest.path(), "html", true) > 0);
    QDir out(dest.path());
    const QStringList produced = out.entryList(QStringList{"*.html"}, QDir::Files);
    QCOMPARE(produced.size(), 1);
    const QString combined = readFile(out.filePath(produced.first()));
    QVERIFY2(combined.contains("Alpha's appendix"), qPrintable(combined));
    QVERIFY2(combined.contains("Beta's appendix"), qPrintable(combined));
    // Still one document: each note's contribution goes into that note's
    // section rather than producing a second file to join afterwards.
    QCOMPARE(combined.count(QStringLiteral("<html")), 1);
    QVERIFY(combined.indexOf("Alpha's appendix")
            < combined.indexOf("Beta body"));

    m_exporter.setExtensions(nullptr);
}

// A contribution may name pictures that live nowhere near the note, and the
// exporter's image context is the note's own folder.
void TestDocumentExporter::testARelativePictureResolvesAgainstTheModulesOwnBase()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, QStringLiteral("Alpha.md"), QStringLiteral("Alpha body.\n"));
    coll.refresh();

    // The module's picture, in a directory of its own outside the vault.
    QTemporaryDir moduleDir;
    QVERIFY(QDir().mkpath(QDir(moduleDir.path()).filePath("pictures")));
    QImage picture(8, 8, QImage::Format_RGB32);
    picture.fill(Qt::darkCyan);
    QVERIFY(picture.save(QDir(moduleDir.path()).filePath("pictures/chart.png")));

    ExtensionRegistry registry;
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("demo"), QStringLiteral("Alpha.md"),
        QStringLiteral("![Chart](pictures/chart.png)\n"), moduleDir.path()));
    m_exporter.setExtensions(&registry);

    QTemporaryDir dest;
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "html", false), 1);
    const QString alpha = readFile(QDir(dest.path()).filePath("Alpha.html"));
    // Embedded rather than left as a broken path: the picture resolved, which
    // it could not have done against the note's folder.
    QVERIFY2(alpha.contains("data:image/png;base64,"), qPrintable(alpha));

    m_exporter.setExtensions(nullptr);
}

void TestDocumentExporter::testTwoModulesContributeInInstallationOrder()
{
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, QStringLiteral("The note."));

    ExtensionRegistry registry;
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("first"), QStringLiteral("Note.md"),
        QStringLiteral("From the first module.\n")));
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("second"), QStringLiteral("Note.md"),
        QStringLiteral("From the second module.\n")));
    m_exporter.setExtensions(&registry);
    m_exporter.setLiveNote(QStringLiteral("Note.md"), &model);

    const QString text = m_exporter.plainTextForModel(&model);
    QVERIFY(text.indexOf("From the first module")
            < text.indexOf("From the second module"));
    QVERIFY(text.indexOf("The note.") < text.indexOf("From the first module"));

    // Both are named where the reader is told, once each.
    QCOMPARE(registry.exportAppendixLabels(),
             (QStringList{QStringLiteral("Notes from first"),
                          QStringLiteral("Notes from second")}));

    m_exporter.setExtensions(nullptr);
    m_exporter.clearLiveNote();
}

// The reader picked particular blocks out of a note; a module's contribution
// is about the note and is not one of the blocks they picked.
void TestDocumentExporter::testABlockScopeExportCarriesNoContribution()
{
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, QStringLiteral("First block."));
    model.insertBlock(1, Block::Paragraph, QStringLiteral("Second block."));

    ExtensionRegistry registry;
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("demo"), QStringLiteral("Note.md"),
        QStringLiteral("The module's paragraph.\n")));
    m_exporter.setExtensions(&registry);
    m_exporter.setLiveNote(QStringLiteral("Note.md"), &model);

    const QString html = m_exporter.htmlForModelBlocks(&model, {0}, "Note");
    QVERIFY(html.contains("First block"));
    QVERIFY2(!html.contains("The module's paragraph"), qPrintable(html));
    const QString text = m_exporter.plainTextForModelBlocks(&model, {0});
    QVERIFY(!text.contains("The module's paragraph"));

    // The whole-note scope of the same model does carry it, which is what
    // makes this an exclusion rather than a wiring failure.
    QVERIFY(m_exporter.htmlForModel(&model, "Note")
                .contains("The module's paragraph"));

    m_exporter.setExtensions(nullptr);
    m_exporter.clearLiveNote();
}

// The contribution is a string the exporter renders. Nothing about it reaches
// the document.
void TestDocumentExporter::testExportingDoesNotTouchTheNoteOrItsModel()
{
    QTemporaryDir root;
    NoteCollection coll;
    QVERIFY(coll.openRoot(root.path()));
    writeNote(&coll, QStringLiteral("Alpha.md"), QStringLiteral("Alpha body.\n"));
    coll.refresh();

    BlockModel model;
    model.insertBlock(0, Block::Paragraph, QStringLiteral("Alpha body."));
    const int countBefore = model.count();

    ExtensionRegistry registry;
    registry.install(std::make_unique<ContributingModule>(
        QStringLiteral("demo"), QStringLiteral("Alpha.md"),
        QStringLiteral("An appendix paragraph.\n")));
    m_exporter.setExtensions(&registry);
    m_exporter.setLiveNote(QStringLiteral("Alpha.md"), &model);

    const QString onDiskBefore = readFile(coll.absolutePath("Alpha.md"));
    QTemporaryDir dest;
    QVERIFY(m_exporter.writeModel(&model, "Alpha", "html",
                                  QDir(dest.path()).filePath("out.html")));
    QCOMPARE(m_exporter.exportCollection(&coll, dest.path(), "markdown", false),
             1);

    QCOMPARE(model.count(), countBefore);
    QCOMPARE(readFile(coll.absolutePath("Alpha.md")), onDiskBefore);
    QCOMPARE(coll.noteInfo("Alpha.md").value("body").toString(),
             QStringLiteral("Alpha body.\n"));

    m_exporter.setExtensions(nullptr);
    m_exporter.clearLiveNote();
}

QTEST_MAIN(TestDocumentExporter)
#include "test_documentexporter.moc"
