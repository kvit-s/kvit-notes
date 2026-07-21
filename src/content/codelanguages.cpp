// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "codelanguages.h"

#include <QHash>
#include <QSet>

// Carry-state values (LineResult::endState). 0 is "normal". The nonzero
// meanings are scanner-internal and only threaded within one code block of
// one language, so a value can mean different things in different families.
namespace {
constexpr int kNormal = 0;
constexpr int kBlockComment = 1;   // inside /* */ or <!-- -->
constexpr int kTripleDouble = 2;   // inside """ (Python)
constexpr int kTripleSingle = 3;   // inside ''' (Python)

enum class Family { Generic, Markup, Css, Markdown };

struct Rules {
    Family family = Family::Generic;
    QSet<QString> keywords;
    QSet<QString> types;            // builtins/types → Type color
    bool caseInsensitiveKeywords = false;  // SQL
    QString lineComment;            // "//", "#", "--", or ""
    QString blockStart;             // "/*" or ""
    QString blockEnd;               // "*/"
    QString stringDelims;           // quote chars, e.g. "\"'"
    bool tripleQuotes = false;      // Python triple strings
    bool backtickString = false;    // JS template literals
    bool doubledQuoteEscape = false;// SQL '' inside '...'
    bool dollarVar = false;         // Bash $var
    bool hashPreproc = false;       // C/C++ # directives
    bool atDecorator = false;       // Python @decorator
    bool functionCalls = true;      // ident before '(' → Type
};

bool isIdentStart(QChar c) { return c.isLetter() || c == '_'; }
bool isIdentPart(QChar c) { return c.isLetterOrNumber() || c == '_'; }

void emitSpan(QList<CodeLanguages::Span> &out, int start, int len,
              CodeLanguages::Token tok)
{
    if (len > 0)
        out.append({start, len, tok});
}

// Scan a number literal starting at i (a digit, or a '.' followed by a digit).
// Handles 0x/0b hex/binary, underscores, decimals, exponents, and letter
// suffixes (C++ f/L/u, JS n). Returns the index just past the literal.
int scanNumber(const QString &s, int i)
{
    const int n = s.length();
    int j = i;
    if (s[j] == '0' && j + 1 < n
        && (s[j + 1] == 'x' || s[j + 1] == 'X'
            || s[j + 1] == 'b' || s[j + 1] == 'B'
            || s[j + 1] == 'o' || s[j + 1] == 'O')) {
        j += 2;
        while (j < n && (s[j].isLetterOrNumber() || s[j] == '_'))
            ++j;
        return j;
    }
    while (j < n && (s[j].isDigit() || s[j] == '_'))
        ++j;
    if (j < n && s[j] == '.') {
        ++j;
        while (j < n && (s[j].isDigit() || s[j] == '_'))
            ++j;
    }
    if (j < n && (s[j] == 'e' || s[j] == 'E')) {
        ++j;
        if (j < n && (s[j] == '+' || s[j] == '-'))
            ++j;
        while (j < n && s[j].isDigit())
            ++j;
    }
    // Trailing type suffix letters / units.
    while (j < n && s[j].isLetter())
        ++j;
    return j;
}

// Scan a single-line string opened by `delim` at position i. Honors backslash
// escapes; for SQL, a doubled delimiter ('') stays inside the string. Returns
// the index just past the closing delimiter, or EOL if unterminated.
int scanString(const QString &s, int i, QChar delim, bool doubledEscape)
{
    const int n = s.length();
    int j = i + 1;
    while (j < n) {
        const QChar c = s[j];
        if (c == '\\' && !doubledEscape) {
            j += 2;
            continue;
        }
        if (c == delim) {
            if (doubledEscape && j + 1 < n && s[j + 1] == delim) {
                j += 2;
                continue;
            }
            return j + 1;
        }
        ++j;
    }
    return n;
}

// The generic identifier/C-like scanner: Python, JavaScript, C++, Java, SQL,
// Bash, JSON, differentiated only by their rule tables.
CodeLanguages::LineResult scanGeneric(const Rules &r, const QString &line,
                                      int startState)
{
    using namespace CodeLanguages;
    LineResult res;
    res.endState = kNormal;
    const int n = line.length();
    int i = 0;

    // Continue a multi-line construct opened on a previous line.
    if (startState == kBlockComment && !r.blockEnd.isEmpty()) {
        const int close = line.indexOf(r.blockEnd);
        if (close < 0) {
            emitSpan(res.spans, 0, n, Token::Comment);
            res.endState = kBlockComment;
            return res;
        }
        const int end = close + r.blockEnd.length();
        emitSpan(res.spans, 0, end, Token::Comment);
        i = end;
    } else if (startState == kTripleDouble || startState == kTripleSingle) {
        const QString close = startState == kTripleDouble
                                  ? QStringLiteral("\"\"\"")
                                  : QStringLiteral("'''");
        const int at = line.indexOf(close);
        if (at < 0) {
            emitSpan(res.spans, 0, n, Token::String);
            res.endState = startState;
            return res;
        }
        emitSpan(res.spans, 0, at + 3, Token::String);
        i = at + 3;
    }

    while (i < n) {
        const QChar c = line[i];

        // Line comment: rest of the line.
        if (!r.lineComment.isEmpty()
            && line.mid(i, r.lineComment.length()) == r.lineComment) {
            emitSpan(res.spans, i, n - i, Token::Comment);
            return res;
        }

        // Block comment start.
        if (!r.blockStart.isEmpty()
            && line.mid(i, r.blockStart.length()) == r.blockStart) {
            const int close = line.indexOf(r.blockEnd, i + r.blockStart.length());
            if (close < 0) {
                emitSpan(res.spans, i, n - i, Token::Comment);
                res.endState = kBlockComment;
                return res;
            }
            const int end = close + r.blockEnd.length();
            emitSpan(res.spans, i, end - i, Token::Comment);
            i = end;
            continue;
        }

        // Python triple-quoted strings.
        if (r.tripleQuotes
            && (line.mid(i, 3) == QStringLiteral("\"\"\"")
                || line.mid(i, 3) == QStringLiteral("'''"))) {
            const bool dbl = line[i] == '"';
            const QString close = dbl ? QStringLiteral("\"\"\"")
                                      : QStringLiteral("'''");
            const int at = line.indexOf(close, i + 3);
            if (at < 0) {
                emitSpan(res.spans, i, n - i, Token::String);
                res.endState = dbl ? kTripleDouble : kTripleSingle;
                return res;
            }
            emitSpan(res.spans, i, at + 3 - i, Token::String);
            i = at + 3;
            continue;
        }

        // Strings.
        if (r.stringDelims.contains(c)) {
            const int end = scanString(line, i, c, r.doubledQuoteEscape);
            emitSpan(res.spans, i, end - i, Token::String);
            i = end;
            continue;
        }
        if (r.backtickString && c == '`') {
            const int end = scanString(line, i, '`', false);
            emitSpan(res.spans, i, end - i, Token::String);
            i = end;
            continue;
        }

        // C/C++ preprocessor directive (# as first non-space token).
        if (r.hashPreproc && c == '#') {
            bool atLineStart = true;
            for (int k = 0; k < i; ++k) {
                if (!line[k].isSpace()) { atLineStart = false; break; }
            }
            if (atLineStart) {
                int j = i + 1;
                while (j < n && isIdentPart(line[j]))
                    ++j;
                emitSpan(res.spans, i, j - i, Token::Keyword);
                i = j;
                continue;
            }
        }

        // Python decorator.
        if (r.atDecorator && c == '@' && i + 1 < n && isIdentStart(line[i + 1])) {
            int j = i + 1;
            while (j < n && (isIdentPart(line[j]) || line[j] == '.'))
                ++j;
            emitSpan(res.spans, i, j - i, Token::Type);
            i = j;
            continue;
        }

        // Bash / shell variable.
        if (r.dollarVar && c == '$' && i + 1 < n) {
            int j = i + 1;
            if (line[j] == '{') {
                const int close = line.indexOf('}', j);
                j = close < 0 ? n : close + 1;
            } else {
                while (j < n && isIdentPart(line[j]))
                    ++j;
            }
            emitSpan(res.spans, i, j - i, Token::Type);
            i = j;
            continue;
        }

        // Numbers.
        if (c.isDigit()
            || (c == '.' && i + 1 < n && line[i + 1].isDigit())) {
            const int end = scanNumber(line, i);
            emitSpan(res.spans, i, end - i, Token::Number);
            i = end;
            continue;
        }

        // Identifiers → keyword / type / function call.
        if (isIdentStart(c)) {
            int j = i + 1;
            while (j < n && isIdentPart(line[j]))
                ++j;
            const QString word = line.mid(i, j - i);
            const QString key = r.caseInsensitiveKeywords ? word.toLower() : word;
            if (r.keywords.contains(key)) {
                emitSpan(res.spans, i, j - i, Token::Keyword);
            } else if (r.types.contains(key)) {
                emitSpan(res.spans, i, j - i, Token::Type);
            } else if (r.functionCalls) {
                int k = j;
                while (k < n && line[k].isSpace())
                    ++k;
                if (k < n && line[k] == '(')
                    emitSpan(res.spans, i, j - i, Token::Type);
            }
            i = j;
            continue;
        }

        ++i;
    }

    return res;
}

// Markup scanner (HTML, XML): comments span lines; a tag's name is a Keyword,
// its attribute names are Types, its quoted values are Strings.
CodeLanguages::LineResult scanMarkup(const QString &line, int startState)
{
    using namespace CodeLanguages;
    LineResult res;
    res.endState = kNormal;
    const int n = line.length();
    int i = 0;

    if (startState == kBlockComment) {
        const int close = line.indexOf(QStringLiteral("-->"));
        if (close < 0) {
            emitSpan(res.spans, 0, n, Token::Comment);
            res.endState = kBlockComment;
            return res;
        }
        emitSpan(res.spans, 0, close + 3, Token::Comment);
        i = close + 3;
    }

    while (i < n) {
        if (line.mid(i, 4) == QStringLiteral("<!--")) {
            const int close = line.indexOf(QStringLiteral("-->"), i + 4);
            if (close < 0) {
                emitSpan(res.spans, i, n - i, Token::Comment);
                res.endState = kBlockComment;
                return res;
            }
            emitSpan(res.spans, i, close + 3 - i, Token::Comment);
            i = close + 3;
            continue;
        }
        if (line[i] == '<') {
            // Tag name (after optional '/', '!', '?').
            int j = i + 1;
            while (j < n && (line[j] == '/' || line[j] == '!' || line[j] == '?'))
                ++j;
            const int nameStart = j;
            while (j < n && (isIdentPart(line[j]) || line[j] == '-' || line[j] == ':'))
                ++j;
            if (j > nameStart)
                emitSpan(res.spans, nameStart, j - nameStart, Token::Keyword);
            // Attributes until '>'.
            while (j < n && line[j] != '>') {
                const QChar a = line[j];
                if (a == '"' || a == '\'') {
                    const int end = scanString(line, j, a, false);
                    emitSpan(res.spans, j, end - j, Token::String);
                    j = end;
                } else if (isIdentStart(a)) {
                    int k = j + 1;
                    while (k < n && (isIdentPart(line[k]) || line[k] == '-'
                                     || line[k] == ':'))
                        ++k;
                    emitSpan(res.spans, j, k - j, Token::Type);
                    j = k;
                } else {
                    ++j;
                }
            }
            i = j < n ? j + 1 : n;
            continue;
        }
        // Entities.
        if (line[i] == '&') {
            const int semi = line.indexOf(';', i);
            if (semi > i && semi - i <= 10) {
                emitSpan(res.spans, i, semi + 1 - i, Token::Number);
                i = semi + 1;
                continue;
            }
        }
        ++i;
    }
    return res;
}

// CSS scanner: /* */ comments (multi-line), strings, at-rules, hex colors,
// numbers with units, !important, property names before ':'.
CodeLanguages::LineResult scanCss(const QString &line, int startState)
{
    using namespace CodeLanguages;
    LineResult res;
    res.endState = kNormal;
    const int n = line.length();
    int i = 0;

    if (startState == kBlockComment) {
        const int close = line.indexOf(QStringLiteral("*/"));
        if (close < 0) {
            emitSpan(res.spans, 0, n, Token::Comment);
            res.endState = kBlockComment;
            return res;
        }
        emitSpan(res.spans, 0, close + 2, Token::Comment);
        i = close + 2;
    }

    while (i < n) {
        const QChar c = line[i];
        if (line.mid(i, 2) == QStringLiteral("/*")) {
            const int close = line.indexOf(QStringLiteral("*/"), i + 2);
            if (close < 0) {
                emitSpan(res.spans, i, n - i, Token::Comment);
                res.endState = kBlockComment;
                return res;
            }
            emitSpan(res.spans, i, close + 2 - i, Token::Comment);
            i = close + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            const int end = scanString(line, i, c, false);
            emitSpan(res.spans, i, end - i, Token::String);
            i = end;
            continue;
        }
        if (c == '@') {           // at-rule
            int j = i + 1;
            while (j < n && (isIdentPart(line[j]) || line[j] == '-'))
                ++j;
            emitSpan(res.spans, i, j - i, Token::Keyword);
            i = j;
            continue;
        }
        if (c == '!') {           // !important
            int j = i + 1;
            while (j < n && line[j].isLetter())
                ++j;
            emitSpan(res.spans, i, j - i, Token::Keyword);
            i = j;
            continue;
        }
        if (c == '#') {           // hex color or id selector
            int j = i + 1;
            while (j < n && (isIdentPart(line[j]) || line[j] == '-'))
                ++j;
            const QString body = line.mid(i + 1, j - i - 1);
            bool hex = (body.length() == 3 || body.length() == 4
                        || body.length() == 6 || body.length() == 8);
            for (QChar h : body) {
                if (!((h >= '0' && h <= '9') || (h.toLower() >= 'a' && h.toLower() <= 'f'))) {
                    hex = false;
                    break;
                }
            }
            emitSpan(res.spans, i, j - i, hex ? Token::Number : Token::Type);
            i = j;
            continue;
        }
        if (c == '.' && i + 1 < n && isIdentStart(line[i + 1])) { // class selector
            int j = i + 1;
            while (j < n && (isIdentPart(line[j]) || line[j] == '-'))
                ++j;
            emitSpan(res.spans, i, j - i, Token::Type);
            i = j;
            continue;
        }
        if (c.isDigit() || (c == '.' && i + 1 < n && line[i + 1].isDigit())) {
            int end = scanNumber(line, i);
            // Units / percent.
            if (end < n && line[end] == '%')
                ++end;
            emitSpan(res.spans, i, end - i, Token::Number);
            i = end;
            continue;
        }
        if (isIdentStart(c)) {
            int j = i + 1;
            while (j < n && (isIdentPart(line[j]) || line[j] == '-'))
                ++j;
            // A property name is an identifier immediately before a ':'.
            int k = j;
            while (k < n && line[k].isSpace())
                ++k;
            if (k < n && line[k] == ':')
                emitSpan(res.spans, i, j - i, Token::Type);
            i = j;
            continue;
        }
        ++i;
    }
    return res;
}

// Markdown scanner (highlighting markdown shown inside a code block): a light
// pass — headings, blockquotes, list markers, inline code / fences, link URLs.
CodeLanguages::LineResult scanMarkdown(const QString &line, int startState)
{
    using namespace CodeLanguages;
    LineResult res;
    res.endState = kNormal;
    const int n = line.length();

    QString trimmed = line;
    int lead = 0;
    while (lead < n && line[lead].isSpace())
        ++lead;

    // Fenced code block toggling (``` ...).
    if (line.mid(lead, 3) == QStringLiteral("```")) {
        emitSpan(res.spans, lead, n - lead, Token::String);
        res.endState = startState == kBlockComment ? kNormal : kBlockComment;
        return res;
    }
    if (startState == kBlockComment) {   // inside a fence
        emitSpan(res.spans, 0, n, Token::String);
        res.endState = kBlockComment;
        return res;
    }

    // Heading.
    if (lead < n && line[lead] == '#') {
        int j = lead;
        while (j < n && line[j] == '#')
            ++j;
        emitSpan(res.spans, lead, n - lead, Token::Keyword);
        return res;
    }
    // Blockquote.
    if (lead < n && line[lead] == '>') {
        emitSpan(res.spans, lead, n - lead, Token::Comment);
        return res;
    }

    int i = 0;
    // List marker.
    if (lead < n && (line[lead] == '-' || line[lead] == '*' || line[lead] == '+')
        && lead + 1 < n && line[lead + 1] == ' ') {
        emitSpan(res.spans, lead, 1, Token::Type);
        i = lead + 1;
    } else {
        // Ordered list marker.
        int j = lead;
        while (j < n && line[j].isDigit())
            ++j;
        if (j > lead && j < n && line[j] == '.') {
            emitSpan(res.spans, lead, j - lead + 1, Token::Type);
            i = j + 1;
        }
    }

    // Inline code and link URLs.
    while (i < n) {
        if (line[i] == '`') {
            const int close = line.indexOf('`', i + 1);
            const int end = close < 0 ? n : close + 1;
            emitSpan(res.spans, i, end - i, Token::String);
            i = end;
            continue;
        }
        if (line[i] == '(' && i > 0 && line[i - 1] == ']') {
            const int close = line.indexOf(')', i + 1);
            const int end = close < 0 ? n : close + 1;
            emitSpan(res.spans, i, end - i, Token::Type);
            i = end;
            continue;
        }
        ++i;
    }
    return res;
}

// ---- Rule-table registry ----

Rules makeGeneric(const QStringList &kw, const QStringList &ty)
{
    Rules r;
    r.family = Family::Generic;
    for (const QString &s : kw)
        r.keywords.insert(s);
    for (const QString &s : ty)
        r.types.insert(s);
    return r;
}

const QHash<QString, Rules> &registry()
{
    static const QHash<QString, Rules> table = [] {
        QHash<QString, Rules> t;

        // Python.
        {
            Rules r = makeGeneric(
                {"and","as","assert","async","await","break","class","continue",
                 "def","del","elif","else","except","finally","for","from",
                 "global","if","import","in","is","lambda","nonlocal","not","or",
                 "pass","raise","return","try","while","with","yield","match","case"},
                {"True","False","None","self","cls","int","str","float","bool",
                 "list","dict","set","tuple","bytes","object","print","len",
                 "range","super","type","isinstance","Exception"});
            r.lineComment = "#";
            r.stringDelims = "\"'";
            r.tripleQuotes = true;
            r.atDecorator = true;
            t.insert("python", r);
        }
        // JavaScript.
        {
            Rules r = makeGeneric(
                {"break","case","catch","class","const","continue","debugger",
                 "default","delete","do","else","export","extends","finally",
                 "for","function","if","import","in","instanceof","let","new",
                 "return","super","switch","this","throw","try","typeof","var",
                 "void","while","with","yield","async","await","of","static","get","set"},
                {"true","false","null","undefined","NaN","Infinity","console",
                 "document","window","Math","JSON","Object","Array","String",
                 "Number","Boolean","Promise","Map","Set","Symbol"});
            r.lineComment = "//";
            r.blockStart = "/*";
            r.blockEnd = "*/";
            r.stringDelims = "\"'";
            r.backtickString = true;
            t.insert("javascript", r);
        }
        // C++.
        {
            Rules r = makeGeneric(
                {"alignas","alignof","and","asm","auto","break","case","catch",
                 "class","const","constexpr","const_cast","continue","decltype",
                 "default","delete","do","dynamic_cast","else","enum","explicit",
                 "export","extern","for","friend","goto","if","inline","mutable",
                 "namespace","new","noexcept","operator","or","private","protected",
                 "public","register","reinterpret_cast","return","sizeof","static",
                 "static_assert","static_cast","struct","switch","template","this",
                 "throw","try","typedef","typename","union","using","virtual",
                 "volatile","while","not","nullptr","override","final"},
                {"bool","char","char8_t","char16_t","char32_t","double","float",
                 "int","long","short","signed","unsigned","void","wchar_t","true",
                 "false","size_t","string","vector","map","set","std","uint8_t",
                 "int32_t","int64_t","uint32_t","uint64_t"});
            r.lineComment = "//";
            r.blockStart = "/*";
            r.blockEnd = "*/";
            r.stringDelims = "\"'";
            r.hashPreproc = true;
            t.insert("cpp", r);
        }
        // Java.
        {
            Rules r = makeGeneric(
                {"abstract","assert","break","case","catch","class","const",
                 "continue","default","do","else","enum","extends","final",
                 "finally","for","goto","if","implements","import","instanceof",
                 "interface","native","new","package","private","protected",
                 "public","return","static","strictfp","super","switch",
                 "synchronized","this","throw","throws","transient","try","void",
                 "volatile","while","var","record","yield","sealed","permits"},
                {"boolean","byte","char","double","float","int","long","short",
                 "String","Object","Integer","Boolean","Double","List","Map",
                 "Set","true","false","null","System","Math","Exception"});
            r.lineComment = "//";
            r.blockStart = "/*";
            r.blockEnd = "*/";
            r.stringDelims = "\"'";
            r.atDecorator = true;  // Java annotations @Override read like decorators
            t.insert("java", r);
        }
        // SQL (case-insensitive keywords).
        {
            Rules r = makeGeneric(
                {"select","from","where","insert","into","values","update","set",
                 "delete","create","table","drop","alter","add","column","index",
                 "view","join","inner","left","right","outer","full","on","group",
                 "by","order","having","limit","offset","distinct","as","and","or",
                 "not","null","is","in","like","between","exists","union","all",
                 "primary","key","foreign","references","default","unique","check",
                 "constraint","begin","commit","rollback","transaction","case",
                 "when","then","else","end","asc","desc","count","sum","avg",
                 "min","max","with"},
                {"int","integer","varchar","char","text","boolean","date",
                 "datetime","timestamp","decimal","numeric","float","double",
                 "serial","bigint","smallint","real","blob"});
            r.caseInsensitiveKeywords = true;
            r.lineComment = "--";
            r.blockStart = "/*";
            r.blockEnd = "*/";
            r.stringDelims = "'";
            r.doubledQuoteEscape = true;
            t.insert("sql", r);
        }
        // Bash.
        {
            Rules r = makeGeneric(
                {"if","then","else","elif","fi","case","esac","for","while",
                 "until","do","done","in","function","select","time","return",
                 "break","continue","local","export","readonly","declare","shift",
                 "exit","source"},
                {"echo","printf","cd","ls","cp","mv","rm","mkdir","cat","grep",
                 "sed","awk","find","test","read","set","unset","true","false"});
            r.lineComment = "#";
            r.stringDelims = "\"'";
            r.dollarVar = true;
            r.functionCalls = false;
            t.insert("bash", r);
        }
        // JSON.
        {
            Rules r = makeGeneric({"true","false","null"}, {});
            r.stringDelims = "\"";
            r.functionCalls = false;
            t.insert("json", r);
        }
        // Markup and CSS and Markdown families (no keyword tables).
        { Rules r; r.family = Family::Markup;   t.insert("html", r); }
        { Rules r; r.family = Family::Markup;   t.insert("xml", r); }
        { Rules r; r.family = Family::Css;      t.insert("css", r); }
        { Rules r; r.family = Family::Markdown; t.insert("markdown", r); }

        return t;
    }();
    return table;
}

const QHash<QString, QString> &aliasMap()
{
    static const QHash<QString, QString> m = {
        {"python", "python"}, {"py", "python"}, {"python3", "python"},
        {"javascript", "javascript"}, {"js", "javascript"}, {"node", "javascript"},
        {"jsx", "javascript"}, {"mjs", "javascript"},
        {"cpp", "cpp"}, {"c++", "cpp"}, {"cxx", "cpp"}, {"cc", "cpp"},
        {"c", "cpp"}, {"h", "cpp"}, {"hpp", "cpp"},
        {"java", "java"},
        {"html", "html"}, {"htm", "html"}, {"xhtml", "html"},
        {"css", "css"},
        {"sql", "sql"}, {"mysql", "sql"}, {"postgres", "sql"}, {"postgresql", "sql"},
        {"bash", "bash"}, {"sh", "bash"}, {"shell", "bash"}, {"zsh", "bash"},
        {"json", "json"},
        {"xml", "xml"}, {"svg", "xml"},
        {"markdown", "markdown"}, {"md", "markdown"},
    };
    return m;
}

} // namespace

namespace CodeLanguages {

QStringList supportedLanguages()
{
    return {"python", "javascript", "cpp", "java", "html", "css",
            "sql", "bash", "json", "xml", "markdown"};
}

QString canonicalLanguage(const QString &nameOrAlias)
{
    return aliasMap().value(nameOrAlias.trimmed().toLower());
}

bool isSupported(const QString &language)
{
    return !canonicalLanguage(language).isEmpty();
}

LineResult highlightLine(const QString &language, const QString &line,
                         int startState)
{
    const QString canon = canonicalLanguage(language);
    if (canon.isEmpty())
        return LineResult{};
    const Rules &r = registry()[canon];
    switch (r.family) {
    case Family::Markup:   return scanMarkup(line, startState);
    case Family::Css:      return scanCss(line, startState);
    case Family::Markdown: return scanMarkdown(line, startState);
    case Family::Generic:  break;
    }
    return scanGeneric(r, line, startState);
}

QList<Span> highlightSpans(const QString &language, const QString &text)
{
    QList<Span> out;
    if (canonicalLanguage(language).isEmpty())
        return out;
    int state = kNormal;
    int offset = 0;
    const QStringList lines = text.split('\n');
    for (int li = 0; li < lines.size(); ++li) {
        const QString &line = lines[li];
        const LineResult r = highlightLine(language, line, state);
        for (const Span &s : r.spans)
            out.append({s.start + offset, s.length, s.token});
        state = r.endState;
        offset += line.length() + 1; // + '\n'
    }
    return out;
}

} // namespace CodeLanguages
