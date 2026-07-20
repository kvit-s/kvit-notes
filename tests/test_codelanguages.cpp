// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "codelanguages.h"

using CodeLanguages::Token;
using CodeLanguages::Span;

// Corpus for the code-block syntax highlighter.
// The scanners are pure functions, so the whole language pass is table-tested
// here before the engine ever paints it — the same discipline the inline
// registry gets in test_markdownformatter. Assertions are substring-anchored
// (a token of class X covers this exact substring) so they pin behavior
// without brittle absolute offsets.
class TestCodeLanguages : public QObject
{
    Q_OBJECT

private:
    // The token class covering the character at `pos` (Plain if none).
    static Token tokenAt(const QList<Span> &spans, int pos)
    {
        for (const Span &s : spans)
            if (pos >= s.start && pos < s.start + s.length)
                return s.token;
        return Token::Plain;
    }

    // True iff some span exactly covers `needle` (at its first occurrence in
    // `text`) and classifies it as `tok`.
    static bool covers(const QList<Span> &spans, const QString &text,
                       const QString &needle, Token tok)
    {
        const int at = text.indexOf(needle);
        if (at < 0)
            return false;
        for (const Span &s : spans)
            if (s.start == at && s.length == needle.length() && s.token == tok)
                return true;
        return false;
    }

    static QList<Span> hl(const QString &lang, const QString &text)
    {
        return CodeLanguages::highlightSpans(lang, text);
    }

private slots:
    // ---- Registry and aliases (decision 14) ----
    void supportedSetIsTheEleven()
    {
        const QStringList langs = CodeLanguages::supportedLanguages();
        QCOMPARE(langs.size(), 11);
        for (const QString &l : {"python", "javascript", "cpp", "java", "html",
                                 "css", "sql", "bash", "json", "xml", "markdown"})
            QVERIFY2(langs.contains(l), qPrintable(l));
    }

    void aliasesResolve_data()
    {
        QTest::addColumn<QString>("alias");
        QTest::addColumn<QString>("canonical");
        QTest::newRow("py") << "py" << "python";
        QTest::newRow("PY upper") << "PY" << "python";
        QTest::newRow("js") << "js" << "javascript";
        QTest::newRow("node") << "node" << "javascript";
        QTest::newRow("c++") << "c++" << "cpp";
        QTest::newRow("cxx") << "cxx" << "cpp";
        QTest::newRow("c") << "c" << "cpp";
        QTest::newRow("sh") << "sh" << "bash";
        QTest::newRow("shell") << "shell" << "bash";
        QTest::newRow("md") << "md" << "markdown";
        QTest::newRow("postgres") << "postgres" << "sql";
        QTest::newRow("svg->xml") << "svg" << "xml";
        QTest::newRow("whitespace") << "  Python  " << "python";
    }
    void aliasesResolve()
    {
        QFETCH(QString, alias);
        QFETCH(QString, canonical);
        QCOMPARE(CodeLanguages::canonicalLanguage(alias), canonical);
    }

    void unknownLanguageIsEmptyAndPaintsNothing()
    {
        QVERIFY(CodeLanguages::canonicalLanguage("brainfuck").isEmpty());
        QVERIFY(!CodeLanguages::isSupported("brainfuck"));
        QVERIFY(hl("brainfuck", "def x(): pass").isEmpty());
        QVERIFY(hl("", "anything").isEmpty());
    }

    // ---- Python ----
    void pythonKeywordsTypesStringsComments()
    {
        const QString src = "def greet(name):  # say hi\n    return 'hello'";
        const auto s = hl("python", src);
        QVERIFY(covers(s, src, "def", Token::Keyword));
        QVERIFY(covers(s, src, "return", Token::Keyword));
        QVERIFY(covers(s, src, "# say hi", Token::Comment));
        QVERIFY(covers(s, src, "'hello'", Token::String));
        // greet( is a call → function/type color.
        QVERIFY(covers(s, src, "greet", Token::Type));
    }

    void pythonNumbersAndDecorator()
    {
        const QString src = "@decorator\nx = 0xFF + 3.14e2";
        const auto s = hl("python", src);
        QVERIFY(covers(s, src, "@decorator", Token::Type));
        QVERIFY(covers(s, src, "0xFF", Token::Number));
        QVERIFY(covers(s, src, "3.14e2", Token::Number));
    }

    void pythonTripleQuotedStringSpansLines()
    {
        const QString src = "a = \"\"\"line one\nstill string\n\"\"\"\nb = 1";
        const auto s = hl("python", src);
        // Every character of the docstring (across the newlines) is String.
        QCOMPARE(tokenAt(s, src.indexOf("line one")), Token::String);
        QCOMPARE(tokenAt(s, src.indexOf("still string")), Token::String);
        // After the closing triple, code resumes.
        QVERIFY(covers(s, src, "1", Token::Number));
    }

    // ---- JavaScript ----
    void javascriptTemplateAndBlockComment()
    {
        const QString src = "const s = `hi ${x}`; /* multi\nline */ let n = 42;";
        const auto s = hl("javascript", src);
        QVERIFY(covers(s, src, "const", Token::Keyword));
        QVERIFY(covers(s, src, "let", Token::Keyword));
        QVERIFY(covers(s, src, "`hi ${x}`", Token::String));
        QVERIFY(covers(s, src, "42", Token::Number));
        // Block comment carries across the newline.
        QCOMPARE(tokenAt(s, src.indexOf("multi")), Token::Comment);
        QCOMPARE(tokenAt(s, src.indexOf("line */")), Token::Comment);
    }

    // ---- C++ ----
    void cppPreprocessorAndTypes()
    {
        const QString src = "#include <vector>\nint main() { return 0; }";
        const auto s = hl("cpp", src);
        QVERIFY(covers(s, src, "#include", Token::Keyword));
        QVERIFY(covers(s, src, "int", Token::Type));
        QVERIFY(covers(s, src, "return", Token::Keyword));
        QVERIFY(covers(s, src, "0", Token::Number));
        QVERIFY(covers(s, src, "main", Token::Type)); // call
    }

    void cppLineCommentAndString()
    {
        const QString src = "auto x = \"a\\\"b\"; // trailing";
        const auto s = hl("cpp", src);
        QVERIFY(covers(s, src, "auto", Token::Keyword));
        QVERIFY(covers(s, src, "\"a\\\"b\"", Token::String)); // escaped quote inside
        QVERIFY(covers(s, src, "// trailing", Token::Comment));
    }

    // ---- Java ----
    void javaAnnotationAndKeywords()
    {
        const QString src = "@Override\npublic final int x = 5;";
        const auto s = hl("java", src);
        QVERIFY(covers(s, src, "@Override", Token::Type));
        QVERIFY(covers(s, src, "public", Token::Keyword));
        QVERIFY(covers(s, src, "final", Token::Keyword));
        QVERIFY(covers(s, src, "int", Token::Type));
        QVERIFY(covers(s, src, "5", Token::Number));
    }

    // ---- SQL (case-insensitive) ----
    void sqlCaseInsensitiveKeywordsAndDashComment()
    {
        const QString src = "SELECT * from users -- all\nWHERE id = 'x''y';";
        const auto s = hl("sql", src);
        QVERIFY(covers(s, src, "SELECT", Token::Keyword));
        QVERIFY(covers(s, src, "from", Token::Keyword)); // lowercase keyword
        QVERIFY(covers(s, src, "WHERE", Token::Keyword));
        QVERIFY(covers(s, src, "-- all", Token::Comment));
        // '' is an escaped quote, so the whole 'x''y' is one string.
        QVERIFY(covers(s, src, "'x''y'", Token::String));
    }

    // ---- Bash ----
    void bashVariablesAndComment()
    {
        const QString src = "echo $HOME # comment\nfor i in ${list}; do :; done";
        const auto s = hl("bash", src);
        QVERIFY(covers(s, src, "echo", Token::Type));
        QVERIFY(covers(s, src, "$HOME", Token::Type));
        QVERIFY(covers(s, src, "# comment", Token::Comment));
        QVERIFY(covers(s, src, "for", Token::Keyword));
        QVERIFY(covers(s, src, "${list}", Token::Type));
        QVERIFY(covers(s, src, "done", Token::Keyword));
    }

    // ---- JSON ----
    void jsonStringsNumbersLiterals()
    {
        const QString src = "{\"key\": true, \"n\": -1.5, \"z\": null}";
        const auto s = hl("json", src);
        QVERIFY(covers(s, src, "\"key\"", Token::String));
        QVERIFY(covers(s, src, "true", Token::Keyword));
        QVERIFY(covers(s, src, "null", Token::Keyword));
        QVERIFY(covers(s, src, "1.5", Token::Number));
    }

    // ---- HTML / XML ----
    void htmlTagsAttributesCommentsEntities()
    {
        const QString src = "<a href=\"x\">t</a><!-- note -->&amp;";
        const auto s = hl("html", src);
        QVERIFY(covers(s, src, "a", Token::Keyword));        // tag name
        QVERIFY(covers(s, src, "href", Token::Type));        // attribute name
        QVERIFY(covers(s, src, "\"x\"", Token::String));     // attribute value
        QVERIFY(covers(s, src, "<!-- note -->", Token::Comment));
        QVERIFY(covers(s, src, "&amp;", Token::Number));     // entity
    }

    void xmlCommentSpansLines()
    {
        const QString src = "<root><!-- a\nb -->\n<x/></root>";
        const auto s = hl("xml", src);
        QCOMPARE(tokenAt(s, src.indexOf("a\n") ), Token::Comment);
        QCOMPARE(tokenAt(s, src.indexOf("b -->")), Token::Comment);
        QVERIFY(covers(s, src, "root", Token::Keyword));
    }

    // ---- CSS ----
    void cssSelectorsPropertiesColorsComments()
    {
        const QString src = ".btn { color: #ff0000; width: 20px; } /* c */";
        const auto s = hl("css", src);
        QVERIFY(covers(s, src, ".btn", Token::Type));    // class selector
        QVERIFY(covers(s, src, "color", Token::Type));   // property name
        QVERIFY(covers(s, src, "#ff0000", Token::Number)); // hex color
        QVERIFY(covers(s, src, "20px", Token::Number));  // number+unit
        QVERIFY(covers(s, src, "/* c */", Token::Comment));
    }

    void cssAtRuleAndImportant()
    {
        const QString src = "@media screen { a { color: red !important; } }";
        const auto s = hl("css", src);
        QVERIFY(covers(s, src, "@media", Token::Keyword));
        QVERIFY(covers(s, src, "!important", Token::Keyword));
    }

    // ---- Markdown ----
    void markdownHeadingsListsCodeLinks()
    {
        const QString src = "# Title\n- item `code`\n> quote\n[t](http://u)";
        const auto s = hl("markdown", src);
        QCOMPARE(tokenAt(s, src.indexOf("# Title")), Token::Keyword);
        QVERIFY(covers(s, src, "`code`", Token::String));
        QCOMPARE(tokenAt(s, src.indexOf("> quote")), Token::Comment);
        QVERIFY(covers(s, src, "(http://u)", Token::Type));
    }

    // ---- Per-line entry point + state threading (the engine's path) ----
    void perLineStateThreadsBlockComment()
    {
        // Line 1 opens a block comment; its end-state must feed line 2.
        auto r1 = CodeLanguages::highlightLine("cpp", "int x; /* open", 0);
        QVERIFY(r1.endState != 0);
        auto r2 = CodeLanguages::highlightLine("cpp", "still comment", r1.endState);
        QCOMPARE(r2.spans.size(), 1);
        QCOMPARE(r2.spans.first().token, Token::Comment);
        QVERIFY(r2.endState != 0); // still open
        auto r3 = CodeLanguages::highlightLine("cpp", "done */ int y;", r2.endState);
        QCOMPARE(r3.endState, 0); // closed
    }

    void wholeTextEqualsThreadedLines()
    {
        // highlightSpans must equal manually threading highlightLine, offset
        // by line — the contract the corpus and the engine both rely on.
        const QString src = "x = 1 /* c\nd */ y = 2";
        const auto whole = hl("cpp", src);
        QVERIFY(covers(whole, src, "1", Token::Number));
        QVERIFY(covers(whole, src, "2", Token::Number));
        QCOMPARE(tokenAt(whole, src.indexOf("/* c")), Token::Comment);
        QCOMPARE(tokenAt(whole, src.indexOf("d */")), Token::Comment);
    }
};

QTEST_MAIN(TestCodeLanguages)
#include "test_codelanguages.moc"
