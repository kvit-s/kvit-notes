// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef CODELANGUAGES_H
#define CODELANGUAGES_H

#include <QList>
#include <QString>
#include <QStringList>

// Code-block syntax highlighting. An in-house, data-driven rule-table pass —
// NOT a third-party framework: KSyntaxHighlighting was rejected because it
// drags in a framework dependency, bypasses the theme token system, and is not
// table-testable the way this project tests everything.
//
// The design mirrors the inline span-type registry that made inline formatting
// data instead of control flow: one rule table per language, interpreted by one
// small set of shared matchers (a generic identifier/C-like scanner covering
// seven languages, plus dedicated markup, CSS, and Markdown scanners). A pure
// `highlightSpans(language, text)` returns classified ranges over the whole
// text for the unit corpus; the engine's QSyntaxHighlighter calls the per-line
// `highlightLine` with the previous block's carry-state so multi-line
// constructs (block comments, triple-quoted strings) survive across lines
// through QSyntaxHighlighter's block-state mechanism.
//
// The five token classes map one-to-one onto the five new theme tokens
// (codeKeyword, codeType, codeString, codeComment, codeNumber — keyword,
// string, comment, number, function/type). Per-language extras (C
// preprocessor, Python decorators, markup tags/attributes) are folded onto
// these five, so no sixth color is ever needed.
namespace CodeLanguages {

// Token classes. The names map to theme tokens; `Type` is the "function/type"
// color, carrying types, builtins, function-like names, decorators, and markup
// tag names. `Plain` ranges are never emitted (the default text color already
// applies) — the scanners only emit non-plain runs.
enum class Token : quint8 {
    Plain = 0,
    Keyword,
    Type,
    String,
    Comment,
    Number,
};

// One classified run, in coordinates of the text passed to the scanner
// (whole-text for highlightSpans, line-local for highlightLine).
struct Span {
    int start = 0;
    int length = 0;
    Token token = Token::Plain;

    bool operator==(const Span &o) const
    {
        return start == o.start && length == o.length && token == o.token;
    }
};

// Per-line result: the line's spans plus the carry-state to hand the next
// line. State 0 is "normal"; nonzero values are scanner-internal and only
// meaningful threaded within one code block of one language.
struct LineResult {
    QList<Span> spans;
    int endState = 0;
};

// Canonical language ids the highlighter recognizes (the eleven of §1.2.7).
QStringList supportedLanguages();

// Resolve a user-typed name or alias (e.g. "py", "c++", "js", "sh") to a
// canonical id, or "" if unrecognized. Used by highlighting and by the
// `/code <language>` menu aliases. Case-insensitive.
QString canonicalLanguage(const QString &nameOrAlias);

// True when `language` (already canonical or an alias) has a rule table.
bool isSupported(const QString &language);

// The corpus entry point: classify the whole (possibly multi-line) text,
// threading carry-state across '\n'. Ranges are in whole-text coordinates.
QList<Span> highlightSpans(const QString &language, const QString &text);

// The engine entry point: classify one line given the previous line's
// carry-state (previousBlockState()), returning line-local spans and the
// state to set on this block (setCurrentBlockState()).
LineResult highlightLine(const QString &language, const QString &line, int startState);

} // namespace CodeLanguages

#endif // CODELANGUAGES_H
