// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMPAINTER_H
#define DIAGRAMPAINTER_H

#include <QColor>
#include <QString>

#include "diagramscene.h"

class QPainter;

// Paints a Diagram::Scene with QPainter (diagrams-prd.md §8.3). Shared by the
// on-screen DiagramCanvas and the PDF raster path so both produce the same
// output. Semantic color roles are resolved from the supplied SceneColors; the
// caller sets any scale/translation on the painter before calling.
namespace Diagram {

struct SceneColors {
    QColor background;
    QColor nodeFill;
    QColor nodeStroke;
    QColor edge;
    QColor label;
    QColor edgeLabel;
    QColor edgeLabelBackground;
    QColor subgraphFill;
    QColor subgraphStroke;
    QColor noteFill = QColor(QStringLiteral("#fdf6c9"));
    QColor noteStroke = QColor(QStringLiteral("#c9b855"));
    QColor activationFill = QColor(QStringLiteral("#e8edf5"));
};

void paintScene(QPainter *painter, const Scene &scene, const SceneColors &colors,
                const QString &fontFamily);

} // namespace Diagram

#endif // DIAGRAMPAINTER_H
