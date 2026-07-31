// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKSTYLE_H
#define BLOCKSTYLE_H

#include <QMap>
#include <QString>
#include <QStringList>

// A block's presentation attributes as CSS.
//
// A block's trailing <!--kvit …--> tag carries what the editor draws but
// markdown cannot say: text alignment, a drop cap, a styled divider, image
// effects, an embed's stored size, a table's column widths, a callout's
// colour override. Several kinds read it, so the reading is here rather than
// repeated in each.
//
// The functions that produce a colour or a family are strict on purpose. A
// note is untrusted input — it arrives by import, by sync, or from whoever
// wrote it — and its bytes end up inside a style attribute of a document the
// reader may pass on. A payload of `color=red;background:url(http://x)` must
// become nothing rather than two declarations, and a quote in it must not
// close the attribute.
namespace BlockStyle {

using Attributes = QMap<QString, QString>;

// The block's payload parsed into keys. A bare flag maps to an empty value.
Attributes parse(const QString &payload);

// Whether the key is present at all, as a flag or as a key=value.
bool has(const Attributes &attrs, QLatin1String key);

// The value of the key, or `fallback` when it is absent or is a bare flag.
QString str(const Attributes &attrs, QLatin1String key,
            const QString &fallback = QString());

// The value of the key as a number, or `fallback` when it is absent or does
// not parse.
int num(const Attributes &attrs, QLatin1String key, int fallback);

// A colour attribute as a CSS colour, empty when it is not one: a hex
// triplet, quad, six or eight digits, or a bare colour word.
QString cssColor(const QString &value);

// A font attribute as a quoted CSS family name, empty when it holds anything
// but letters, digits, spaces and hyphens.
QString cssFontFamily(const QString &value);

// ` style="…"` for a list of declarations, or nothing at all when the list is
// empty — so a block with no attributes writes no attribute, and the common
// case exports exactly as it did before any of this reached the renderer.
QString styleAttr(const QStringList &declarations);

// A `text-align` declaration, or nothing when the block is aligned the way
// its kind already aligns by default. Text defaults to left and an image to
// centre, which is why the default is a parameter.
QString textAlign(const Attributes &attrs, const QString &kindDefault);

// `html` with its first rendered character wrapped in a drop-cap span, or
// `html` unchanged when the block has no drop cap or asks for fewer than two
// lines — the same threshold the delegate applies.
//
// The initial is the first character the READER sees, not the first character
// of the markdown, so a paragraph opening in bold caps the letter rather than
// the asterisk. Walking the rendered HTML gets that for free: tags are
// skipped, an entity counts as one character, and wrapping in place keeps
// whatever markup surrounds it, so a bold opening stays bold.
QString withDropCap(const QString &html, const Attributes &attrs);

} // namespace BlockStyle

#endif // BLOCKSTYLE_H
