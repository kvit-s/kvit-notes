// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef HTMLINLINE_H
#define HTMLINLINE_H

#include <QString>

// HTML escaping and inline-markdown rendering: the part of building a block's
// markup that is the same for every block that holds prose.
//
// This lived inside DocumentExporter, where it was reachable only by the
// exporter itself. Every block kind now writes its own markup, and a kind
// lives in `domain`, so the shared piece moves down to `content` — where it
// belongs anyway, since it reads no state at all and is a pure function of
// its markdown. Nothing about its behaviour changed in the move: the
// exporter's tests compare exact strings, and they still pass.
namespace HtmlInline {

// & < > and " only. An apostrophe is deliberately left alone: nothing here
// writes single-quoted attributes, and escaping it makes ordinary prose
// unreadable in the source of the exported file.
QString esc(const QString &text);

// esc(), then each newline as a <br>. A break the editor shows has to survive
// into the export: a bare newline inside a <p> or an <li> collapses to a
// space in a browser and in QTextDocument alike, which is what prints the
// PDF. Only prose comes through here — a code block's newlines stay literal
// inside its <pre>.
QString escFlowing(const QString &text);

// A link target the exported document may carry in an `href`, or "" when it
// may not. A refused link is written as its own text, with no anchor around
// it.
//
// An exported HTML file is opened by a browser, where an href is executable
// surface rather than an address: `javascript:` runs, and `data:` carries a
// whole document of the author's choosing. A note is untrusted input — anyone
// who can hand a reader a `.md` file chooses what the links in it say, and the
// reader opens the export expecting a document — so only schemes that
// navigate are written through.
//
// An allowlist, not a list of the dangerous ones. `JaVaScRiPt:` and
// `java<tab>script:` are one scheme to the browser and three strings to a
// filter, and the next spelling of that trick is not knowable here.
QString safeHref(const QString &url);

// Inline markdown as HTML, walking the span tree so nesting survives.
//
// With `mathJax`, a `$x$` span becomes `\( … \)` and *sawMath is set, so the
// document wrapper knows to emit the one script tag; without it the TeX falls
// through as literal text, which is what the image-embed and PDF paths want.
QString renderInline(const QString &markdown, bool mathJax = false,
                     bool *sawMath = nullptr);

} // namespace HtmlInline

#endif // HTMLINLINE_H
