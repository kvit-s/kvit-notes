// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TEXTDIAGRAM_H
#define TEXTDIAGRAM_H

#include <QString>

#include "diagramscene.h"

// Scene → Unicode box-drawing serializer: the
// "Copy as text" rendition of a rendered Mermaid diagram. Consumes the
// same layout Scene the painter consumes, so what you copy matches what
// you see; emits only the glyph vocabulary DiagramRepair recognizes, so
// the output is already-straight — repair(render(scene)) == render(scene)
// — and the classifier accepts it as a character diagram.
//
// Fidelity is tiered by family (the plan's cut line): flowcharts and
// sequence diagrams come out well from this generic path; class, state,
// and ER get best-effort output — their UML/ER markers deliberately
// degrade to △ ◇ o and crow's-feet to < > ^ v.
namespace Diagram {

QString renderText(const Scene &scene);

} // namespace Diagram

#endif // TEXTDIAGRAM_H
