// Package search is the searching Kvit Notes does, without any drawing:
// finding and replacing inside one note (the find bar), and searching every
// note of a vault.
//
// Text is always the text as the reader sees it: a block's Markdown with the
// markers of its inline spans removed (a code block's text is its source).
// Positions are rune offsets into that text.
//
// The package knows nothing of the editor or the toolkit. Where a rule needs
// a block's inline spans, the caller passes them in (Span), so the editor's
// Markdown parser stays the only one.
package search
