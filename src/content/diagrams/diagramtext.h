// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMTEXT_H
#define DIAGRAMTEXT_H

#include <QFont>
#include <QSizeF>
#include <QString>

// Mathematics in a diagram label (diagram-math.md).
//
// Mermaid settled the syntax upstream in v10.9.0: a label may be a LaTeX
// expression delimited by `$$`, and it is supported in flowchart node and edge
// labels and in sequence-diagram participants, messages and notes. This module
// recognizes that form and measures it; layout sizes the box from the result
// and the painter typesets the same expression into it.
//
// Only a WHOLE label counts. A label that merely contains a `$$` span among
// other words stays text, because a mixed label has no single font and neither
// QPainter::drawText nor the scene's Text primitive can express one. A single
// `$` is likewise always literal, which is what keeps currency amounts and
// shell variables in existing diagrams rendering as they always have.
//
// Nothing here decides how a label is measured when it is NOT mathematics.
// Each layout keeps its own text measurement untouched, which is deliberate:
// the families differ in whether they honour `<br>`, and the painter draws
// exactly what each of them assumed.
namespace Diagram {

// The TeX inside a label that is one whole `$$…$$` expression, or an empty
// string for every other label. Line-break markup is normalized first, so a
// label typed as `$$x^2$$<br>` still counts.
QString mathLabel(const QString &label);

// The pixel size mathematics should be set at beside text in `font`, matching
// the two fonts' x-heights. Layout and the painter both ask, so that the box
// a formula is measured into is the box it is drawn into.
int mathLabelPixelSize(const QFont &font);

// The rendered size of `tex` at that size, in logical pixels. An invalid size
// means the expression does not parse, and the caller falls back to measuring
// and drawing the label's source.
QSizeF mathLabelSize(const QString &tex, const QFont &font);

} // namespace Diagram

#endif // DIAGRAMTEXT_H
