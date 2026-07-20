// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaidrenderer.h"
#include "mermaidparser.h"

#include <QCache>
#include <QCryptographicHash>
#include <QMutex>

using namespace Mermaid;

namespace Diagram {

namespace {

QString cacheKey(const QString &source, const LayoutOptions &opts)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(source.toUtf8());
    h.addData(opts.fontFamily.toUtf8());
    const QString suffix = QStringLiteral("|%1|%2")
                               .arg(opts.fontPixelSize)
                               .arg(int(opts.direction));
    h.addData(suffix.toUtf8());
    return QString::fromLatin1(h.result().toHex());
}

// A bounded process-wide LRU. Scenes are small; a modest count cap approximates
// the documented 32 MiB budget without measuring each.
QMutex &cacheMutex()
{
    static QMutex m;
    return m;
}
QCache<QString, RenderResult> &cache()
{
    static QCache<QString, RenderResult> c(96);
    return c;
}

RenderResult compute(const QString &source, const LayoutOptions &opts)
{
    RenderResult r;
    MermaidParser parser;
    const ParseResult pr = parser.parse(source);
    r.diagnostics = pr.diagnostics;
    r.familyName = pr.familyName;
    r.family = pr.type;
    r.hasArrangement = pr.flowchart.hasPosLine;
    r.hasError = pr.hasErrors();
    if (pr.hasErrors())
        r.firstError = pr.firstError();

    if (pr.type == DiagramType::Flowchart) {
        LayoutOptions o = opts;
        o.direction = pr.flowchart.direction;
        r.scene = layoutFlowchart(pr.flowchart, o);
        r.valid = !pr.flowchart.nodes.isEmpty();
        return r;
    }
    if (pr.type == DiagramType::Sequence && pr.supported) {
        r.scene = layoutSequence(pr.sequence, opts);
        r.valid = !pr.sequence.participants.isEmpty();
        return r;
    }
    if (pr.type == DiagramType::Class && pr.supported) {
        r.scene = layoutClassDiagram(pr.classDiagram, opts);
        r.valid = !pr.classDiagram.classes.isEmpty();
        return r;
    }
    if (pr.type == DiagramType::State && pr.supported) {
        r.scene = layoutStateDiagram(pr.stateDiagram, opts);
        r.valid = !pr.stateDiagram.states.isEmpty();
        return r;
    }
    if (pr.type == DiagramType::Er && pr.supported) {
        r.scene = layoutErDiagram(pr.er, opts);
        r.valid = !pr.er.entities.isEmpty();
        return r;
    }

    r.unsupportedFamily = (pr.type != DiagramType::Unknown && !pr.supported);
    r.valid = false;
    return r;
}

} // namespace

RenderResult render(const QString &source, const LayoutOptions &opts)
{
    const QString key = cacheKey(source, opts);
    {
        QMutexLocker lock(&cacheMutex());
        if (const RenderResult *hit = cache().object(key))
            return *hit;
    }
    RenderResult result = compute(source, opts);
    {
        QMutexLocker lock(&cacheMutex());
        cache().insert(key, new RenderResult(result));
    }
    return result;
}

void clearCache()
{
    QMutexLocker lock(&cacheMutex());
    cache().clear();
}

int cacheCount()
{
    QMutexLocker lock(&cacheMutex());
    return cache().count();
}

} // namespace Diagram
