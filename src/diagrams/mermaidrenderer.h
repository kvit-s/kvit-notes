// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDRENDERER_H
#define MERMAIDRENDERER_H

#include <QList>
#include <QString>

#include "diagramlayout.h"
#include "diagramscene.h"
#include "mermaidast.h"

// The parse+layout stage behind a cache. Pure and thread-safe so
// DiagramCanvas can run it off the UI thread; results are keyed by source
// hash, font family, font size, and direction (layout is width-independent),
// and kept in a bounded process-wide LRU so scrolling back to a diagram, or
// re-rendering after an unrelated edit, is a cache hit.
namespace Diagram {

struct RenderResult {
    bool valid = false;              // a renderable scene was produced
    bool unsupportedFamily = false;  // a known but not-yet-supported family
    QString familyName;
    Mermaid::DiagramType family = Mermaid::DiagramType::Unknown;
    bool hasArrangement = false;     // a pos line is present (flowchart)
    QList<Mermaid::Diagnostic> diagnostics;
    Mermaid::Diagnostic firstError;
    bool hasError = false;
    Scene scene;
};

// Parse `source` and, for the flowchart family, lay it out. Consults and fills
// the LRU cache. Safe to call from any thread.
RenderResult render(const QString &source, const LayoutOptions &opts);

// Test/diagnostics hooks.
void clearCache();
int cacheCount();

} // namespace Diagram

#endif // MERMAIDRENDERER_H
