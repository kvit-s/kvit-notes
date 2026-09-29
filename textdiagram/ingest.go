package textdiagram

import "strings"

// fenceID is a fence's language as the app compares it:
// string::trimmed and then string::toLower. toLower differs from
// strings.ToLower only for U+0130 (capital I with a dot), which  lowers
// to "i" and a combining dot, so a language holding it never equals one of
// the ASCII names below, as in .
func fenceID(lang string) string {
	return strings.ToLower(strings.ReplaceAll(trimSpace(lang), "İ", "i̇"))
}

// IsDiagramLanguage reports whether a fence's language marks a character
// diagram: `diagram`, `text-diagram` or `ascii-diagram`, in any case, with
// spaces around it ignored.
func IsDiagramLanguage(lang string) bool {
	switch fenceID(lang) {
	case "diagram", "text-diagram", "ascii-diagram":
		return true
	}
	return false
}

// Ingest is what the app does to a code fence at every point where
// Markdown enters a note from outside it: a note opened, Markdown pasted,
// text pasted into a code block, and a code block given a new language
// (classifyFenceLanguage and ingestFence in
// src/domain/documentserializer.cpp). lang is the fence's language, the text
// after the opening backticks; body is the text between the fence lines.
//
// A fence whose language says nothing about its contents (none, `text`,
// `plaintext` or `ascii`) is given the language `diagram` when Classify
// finds a character diagram in it. Every other language is kept, which is
// why `plain` is the way to keep a fence from ever being tagged. Then a
// fence whose language is `diagram`, `text-diagram` or `ascii-diagram` is
// straightened by Repair.
//
// It returns the fence's language and body, unchanged when there was
// nothing to do. Running it again keeps the language it returned, and
// usually the body too (see Repair).
func Ingest(lang, body string) (string, string) {
	switch fenceID(lang) {
	case "", "text", "plaintext", "ascii":
		if LooksLikeDiagram(body) {
			lang = "diagram"
		}
	}
	if IsDiagramLanguage(lang) {
		body = Repair(body)
	}
	return lang, body
}
