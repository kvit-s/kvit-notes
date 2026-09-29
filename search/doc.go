// Package search is the searching Kvit Notes does, without any drawing:
// finding and replacing inside one note (the find bar, features.md 7.1 and
// 7.2), and searching every note of a vault (features.md 8.4). It follows
// the app's rules: src/domain/documentsearch.cpp for the find bar, and
// src/search/searchindexdb.cpp with src/application/collectionsearch.cpp for
// the search across notes.
//
// Text is always the text as the reader sees it: a block's Markdown with the
// markers of its inline spans removed (a code block's text is its source).
// Positions are rune offsets into that text. The app counts UTF-16 code
// units, so the numbers differ after a character outside the Basic
// Multilingual Plane (most emoji), but they name the same characters.
//
// The package knows nothing of the editor or the toolkit. Where a rule needs
// a block's inline spans, the caller passes them in (Span), so the editor's
// Markdown parser stays the only one.
package search
