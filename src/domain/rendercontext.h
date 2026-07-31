// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef RENDERCONTEXT_H
#define RENDERCONTEXT_H

#include <QString>

// The services a block kind needs to render itself but cannot reach from
// `domain`.
//
// Every one of these is a call back into the document renderer
// (DocumentExporter, in `application`), which holds the theme, the open
// collection, the embed-preview cache, the per-note image context and the
// attachment budget, and which owns every scan that reads more than one
// block. A kind calls through this interface, so it compiles against
// `content` alone and the layering guard stays satisfied.
//
// The rule the interface encodes: rasterisation, anything shaped like network
// state, and anything that reads a block other than this one stays above
// `domain`.
class BlockRenderServices
{
public:
    virtual ~BlockRenderServices() = default;

    // ---- embedded assets ----
    // All three rasterise, so all three are GUI-thread-only, which is one
    // reason they are not in `domain`.

    // A stored image path as a base64 data: URI, empty when the path does not
    // resolve or the file is over the attachment budget. The two are
    // deliberately indistinguishable here: the caller draws one placeholder
    // for both, because "the picture is not in this file" is the same fact to
    // a reader either way.
    virtual QString imageDataUri(const QString &storedPath) const = 0;

    // Display TeX as a PNG data: URI in the theme's text colour, empty when
    // the formula did not typeset. The caller falls back to the source.
    virtual QString mathDataUri(const QString &tex) const = 0;

    // A natively-supported Mermaid diagram as a 2x PNG data: URI, empty when
    // the source is invalid or the family is one the native renderer does not
    // draw.
    virtual QString mermaidDataUri(const QString &source) const = 0;

    // A code fence's body with theme-coloured spans. Needs the theme, which
    // lives in `platform` and which `domain` may not include.
    virtual QString highlightedCodeHtml(const QString &language,
                                        const QString &source) const = 0;

    // ---- kinds whose answer is not in the block ----

    // A query fence's answer, evaluated once against the open collection.
    // Three-way: the spec did not parse, there is no vault to ask, or the
    // answer itself.
    virtual QString queryHtml(const QString &spec) const = 0;
    virtual QString queryPlainText(const QString &spec) const = 0;

    // An image expression whose URL names a web page: the preview card.
    // Reads the metadata CACHE only and never fetches — an export runs over
    // notes the reader may never have opened, and reaching the network from
    // there would tell every site named in a vault that it had been read.
    virtual QString embedCardHtml(const QString &url, const QString &alt,
                                  const QString &caption,
                                  const QString &attributes) const = 0;
    virtual QString embedCardPlainText(const QString &url, const QString &alt,
                                       const QString &caption) const = 0;

    // The table of contents for the document being rendered. This reads every
    // heading in the note and the whole collision-suffixed slug table, so it
    // stays with the document renderer; the toc kind's own rendering is one
    // line delegating here. That keeps the whole-document scan out of the
    // kinds without putting a "if this is the toc block" branch back into the
    // exporter.
    virtual QString tableOfContentsHtml() const = 0;
    virtual QString tableOfContentsPlainText() const = 0;
};

// Everything one call to toHtml() or toPlainText() needs beyond the block.
//
// Deliberately NOT here: a list cursor, the block list, a slug table, an
// index. Anything that looks at neighbours stays in the document renderer. A
// RenderContext that grew those would be the exporter again, wearing a
// different name.
struct RenderContext {
    enum Target {
        // A browser: MathJax delimiters for display math and a
        // <pre class="mermaid"> the page's own script renders.
        Browser,
        // The PDF print seam, which loads the markup into a QTextDocument.
        // That runs no JavaScript, so both maths and diagrams arrive as PNG
        // data URIs instead.
        Pdf,
    };
    Target target = Browser;

    // Whether display and inline maths go out as MathJax delimiters.
    // Computed once by the document renderer from the target and the
    // KVIT_MATH_RENDER environment variable, and read by every kind that
    // renders prose, so that no two of them can disagree about it.
    bool mathJax = true;

    // Never null during a render; owned by the document renderer.
    const BlockRenderServices *services = nullptr;

    // The anchor the document renderer resolved for THIS block, empty for
    // anything that is not a heading. Slugs are collision-suffixed across the
    // whole document, so a heading cannot compute its own without breaking
    // internal links and the table of contents. A per-block input rather than
    // cross-block state: the renderer sets it before each call.
    QString headingSlug;

    // The numbering the document gave this block, for the one kind that
    // reads it. One counter per indent level, so a nested numbered list
    // restarts rather than continuing.
    int ordinal = 1;

    // Which shared assets the document turned out to need: one MathJax script
    // tag and one Mermaid module tag are emitted for the whole file however
    // many blocks asked for them. A combined export ORs each note's flags
    // into the job before the single wrapper is written. Mutable because
    // rendering is const and returns only a string; this is document-level
    // accumulation rather than sequencing.
    mutable bool sawMath = false;
    mutable bool sawMermaid = false;
};

#endif // RENDERCONTEXT_H
