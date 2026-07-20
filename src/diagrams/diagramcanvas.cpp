// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramcanvas.h"

#include "diagrambudget.h"
#include "diagrampainter.h"
#include "mermaidedits.h"
#include "mermaidparser.h"
#include "textdiagram.h"

#include <QImage>
#include <QPainter>
#include <QtConcurrent>

DiagramCanvas::DiagramCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFillColor(Qt::transparent);
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::Image);
}

void DiagramCanvas::setSource(const QString &s)
{
    if (m_source == s)
        return;
    m_source = s;
    emit sourceChanged();
    scheduleRender();
}

void DiagramCanvas::setFontFamily(const QString &f)
{
    if (m_fontFamily == f)
        return;
    m_fontFamily = f;
    emit fontChanged();
    scheduleRender();
}

void DiagramCanvas::setFontPixelSize(int px)
{
    if (px <= 0 || m_fontPixelSize == px)
        return;
    m_fontPixelSize = px;
    emit fontChanged();
    scheduleRender();
}

void DiagramCanvas::setRenderScale(qreal s)
{
    // The floor must admit fit-to-window scales for very long diagrams
    // (e.g. a 15,000 px flowchart fit into a 720 px window needs ~0.05).
    s = qBound(0.02, s, 8.0);
    if (qFuzzyCompare(m_renderScale, s))
        return;
    m_renderScale = s;
    updateImplicitSize();
    update();
    emit renderScaleChanged();
}

void DiagramCanvas::updateImplicitSize()
{
    // Scene bounds derive from node coordinates, which a manual arrangement
    // comment in the note controls. An implicit size in the millions wedges
    // the scene graph, so it is bounded here as well as at the parser.
    setImplicitSize(qMin(m_scene.bounds.width() * m_renderScale,
                         Diagram::kMaxSceneSpan),
                    qMin(m_scene.bounds.height() * m_renderScale,
                         Diagram::kMaxSceneSpan));
}

void DiagramCanvas::resetScene()
{
    m_scene = Diagram::Scene();
    m_hasScene = false;
    m_sceneSource.clear();
    m_summary.clear();
    m_accTitle.clear();
    m_family = Mermaid::DiagramType::Unknown;
    m_hasArrangement = false;
    m_hasError = false;
    m_unsupported = false;
    m_errorText.clear();
    m_errorLine = 0;
    m_errorColumn = 0;
    m_diagnosticCount = 0;
    m_selectedNode.clear();
    m_selectedEdge = -1;
    m_highlightNode.clear();
    m_highlightEdge = -1;
    m_dragActive = false;
    m_edgeDragActive = false;
    updateImplicitSize();
    update();
    emit sceneChanged();
    emit resultChanged();
    emit selectionChanged();
    // Property-update order on delegate reuse is unspecified: the new source
    // may have been set before or after this call (or be byte-identical to
    // the old one, in which case setSource() short-circuited). Re-render the
    // current source unconditionally; the renderer's LRU makes it cheap.
    if (!m_source.isEmpty())
        scheduleRender();
}

void DiagramCanvas::scheduleRender()
{
    const quint64 rev = ++m_revision;
    Diagram::LayoutOptions opts;
    opts.fontFamily = m_fontFamily;
    opts.fontPixelSize = m_fontPixelSize;
    const QString src = m_source;

    if (!m_rendering) {
        m_rendering = true;
        emit renderingChanged();
    }

    auto *watcher = new QFutureWatcher<Diagram::RenderResult>(this);
    connect(watcher, &QFutureWatcher<Diagram::RenderResult>::finished, this,
            [this, watcher, rev, src]() {
                if (rev == m_revision) {
                    m_rendering = false;
                    emit renderingChanged();
                    applyResult(watcher->result(), src);
                }
                watcher->deleteLater();
            });
    watcher->setFuture(QtConcurrent::run([src, opts]() {
        return Diagram::render(src, opts);
    }));
}

void DiagramCanvas::applyResult(const Diagram::RenderResult &r,
                                const QString &src)
{
    m_diagnosticCount = r.diagnostics.size();
    m_unsupported = r.unsupportedFamily;
    m_family = r.family;
    m_hasArrangement = r.hasArrangement;
    m_hasError = r.hasError || r.unsupportedFamily;
    if (m_hasError) {
        m_errorText = r.firstError.message;
        m_errorLine = r.firstError.line;
        m_errorColumn = r.firstError.column;
        if (m_errorText.isEmpty() && r.unsupportedFamily)
            m_errorText = QStringLiteral("Unsupported Mermaid diagram type");
    } else {
        m_errorText.clear();
        m_errorLine = 0;
        m_errorColumn = 0;
    }

    // Keep the last valid scene while the new source is invalid (last-good
    // preview).
    if (r.valid) {
        m_scene = r.scene;
        m_hasScene = true;
        m_summary = r.scene.summary;
        m_accTitle = r.scene.accTitle;
        m_sceneSource = src;
        // A selected element that no longer exists is deselected.
        if (!m_selectedNode.isEmpty() || m_selectedEdge >= 0) {
            bool nodeAlive = m_selectedNode.isEmpty();
            bool edgeAlive = m_selectedEdge < 0;
            for (const Diagram::Shape &s : m_scene.shapes)
                if (!nodeAlive && s.nodeId == m_selectedNode)
                    nodeAlive = true;
            for (const Diagram::Path &p : m_scene.paths)
                if (!edgeAlive && p.edgeIndex == m_selectedEdge)
                    edgeAlive = true;
            if (!nodeAlive || !edgeAlive)
                clearSelection();
        }
        updateImplicitSize();
        emit sceneChanged();
    }
    // When invalid, the previous scene and summary are retained (last-good).
    update();
    emit resultChanged();
}

// ---- Hit-testing and selection ----

QString DiagramCanvas::nodeAt(qreal x, qreal y) const
{
    if (!m_hasScene)
        return QString();
    const QPointF pt = toSceneCoords(x, y);
    for (int i = m_scene.shapes.size() - 1; i >= 0; --i) {
        const Diagram::Shape &s = m_scene.shapes.at(i);
        if (!s.nodeId.isEmpty() && s.rect.contains(pt))
            return s.nodeId;
    }
    return QString();
}

int DiagramCanvas::edgeAt(qreal x, qreal y) const
{
    if (!m_hasScene)
        return -1;
    const QPointF pt = toSceneCoords(x, y);
    for (int i = m_scene.paths.size() - 1; i >= 0; --i) {
        const Diagram::Path &p = m_scene.paths.at(i);
        if (p.edgeIndex < 0)
            continue;
        QPainterPathStroker stroker;
        stroker.setWidth(9.0);
        if (stroker.createStroke(p.path).contains(pt))
            return p.edgeIndex;
    }
    return -1;
}

void DiagramCanvas::setSelectedNodeId(const QString &id)
{
    if (m_selectedNode == id && m_selectedEdge < 0)
        return;
    m_selectedNode = id;
    m_selectedEdge = -1;
    update();
    emit selectionChanged();
}

void DiagramCanvas::setSelectedEdgeIndex(int index)
{
    if (m_selectedEdge == index && m_selectedNode.isEmpty())
        return;
    m_selectedEdge = index;
    m_selectedNode.clear();
    update();
    emit selectionChanged();
}

void DiagramCanvas::clearSelection()
{
    if (!hasSelection())
        return;
    m_selectedNode.clear();
    m_selectedEdge = -1;
    update();
    emit selectionChanged();
}

QString DiagramCanvas::cycleNode(int delta)
{
    QStringList ids;
    for (const Diagram::Shape &s : m_scene.shapes)
        if (!s.nodeId.isEmpty() && !ids.contains(s.nodeId))
            ids.append(s.nodeId);
    if (ids.isEmpty())
        return QString();
    int index = ids.indexOf(m_selectedNode);
    if (index < 0)
        index = delta >= 0 ? -1 : 0;
    index = (index + delta + ids.size()) % ids.size();
    setSelectedNodeId(ids.at(index));
    return ids.at(index);
}

QString DiagramCanvas::selectionLabel() const
{
    if (!m_selectedNode.isEmpty())
        return tr("Node %1").arg(m_selectedNode);
    if (m_selectedEdge >= 0)
        return tr("Connection %1").arg(m_selectedEdge + 1);
    return QString();
}

QRectF DiagramCanvas::selectionRect() const
{
    if (m_selectedNode.isEmpty())
        return QRectF();
    for (const Diagram::Shape &s : m_scene.shapes) {
        if (s.nodeId == m_selectedNode) {
            const qreal sc = m_renderScale;
            return QRectF(s.rect.x() * sc, s.rect.y() * sc,
                          s.rect.width() * sc, s.rect.height() * sc);
        }
    }
    return QRectF();
}

// ---- Gesture wrappers ----

QString DiagramCanvas::gestureResult(const Mermaid::Edits::Result &r)
{
    const QString error = r.ok ? QString() : r.error;
    if (error != m_gestureError) {
        m_gestureError = error;
        emit gestureErrorChanged();
    }
    if (!r.ok || r.source == m_source)
        return QString();
    return r.source;
}

QString DiagramCanvas::labelSelectionSource(const QString &newLabel)
{
    if (!supportsArrangement() || m_selectedNode.isEmpty())
        return QString();
    return gestureResult(
        Mermaid::Edits::setNodeLabel(m_source, m_selectedNode, newLabel));
}

QString DiagramCanvas::shapeSelectionSource(const QString &shapeName)
{
    if (!supportsArrangement() || m_selectedNode.isEmpty())
        return QString();
    static const QHash<QString, Mermaid::NodeShape> kShapes{
        { QStringLiteral("rect"), Mermaid::NodeShape::Rect },
        { QStringLiteral("rounded"), Mermaid::NodeShape::RoundRect },
        { QStringLiteral("stadium"), Mermaid::NodeShape::Stadium },
        { QStringLiteral("subroutine"), Mermaid::NodeShape::Subroutine },
        { QStringLiteral("cylinder"), Mermaid::NodeShape::Cylinder },
        { QStringLiteral("circle"), Mermaid::NodeShape::Circle },
        { QStringLiteral("doublecircle"), Mermaid::NodeShape::DoubleCircle },
        { QStringLiteral("rhombus"), Mermaid::NodeShape::Rhombus },
        { QStringLiteral("hexagon"), Mermaid::NodeShape::Hexagon },
        { QStringLiteral("parallelogram"), Mermaid::NodeShape::Parallelogram },
        { QStringLiteral("trapezoid"), Mermaid::NodeShape::Trapezoid },
        { QStringLiteral("odd"), Mermaid::NodeShape::Odd },
        { QStringLiteral("ellipse"), Mermaid::NodeShape::Ellipse },
    };
    if (!kShapes.contains(shapeName))
        return QString();
    return gestureResult(Mermaid::Edits::setNodeShape(
        m_source, m_selectedNode, kShapes.value(shapeName)));
}

QString DiagramCanvas::renameSelectionSource(const QString &newId)
{
    if (!supportsArrangement() || m_selectedNode.isEmpty())
        return QString();
    return gestureResult(
        Mermaid::Edits::renameNode(m_source, m_selectedNode, newId));
}

QString DiagramCanvas::deleteSelectionSource()
{
    if (!supportsArrangement())
        return QString();
    if (!m_selectedNode.isEmpty())
        return gestureResult(
            Mermaid::Edits::deleteNode(m_source, m_selectedNode));
    if (m_selectedEdge >= 0)
        return gestureResult(
            Mermaid::Edits::deleteEdge(m_source, m_selectedEdge));
    return QString();
}

QString DiagramCanvas::edgeStyleSelectionSource(const QString &strokeName)
{
    if (!supportsArrangement() || m_selectedEdge < 0)
        return QString();
    Mermaid::EdgeStroke stroke = Mermaid::EdgeStroke::Solid;
    if (strokeName == QLatin1String("dotted"))
        stroke = Mermaid::EdgeStroke::Dotted;
    else if (strokeName == QLatin1String("thick"))
        stroke = Mermaid::EdgeStroke::Thick;
    return gestureResult(
        Mermaid::Edits::setEdgeStroke(m_source, m_selectedEdge, stroke));
}

QString DiagramCanvas::styleSelectionSource(const QColor &fill,
                                            const QColor &stroke)
{
    if (!supportsArrangement() || m_selectedNode.isEmpty())
        return QString();
    return gestureResult(
        Mermaid::Edits::setNodeStyle(m_source, m_selectedNode, fill, stroke));
}

QString DiagramCanvas::quickAddSelectionSource()
{
    if (!supportsArrangement() || m_selectedNode.isEmpty())
        return QString();
    return gestureResult(
        Mermaid::Edits::quickAddNode(m_source, m_selectedNode));
}

QString DiagramCanvas::selectedNodeLabel() const
{
    // The scene has no label registry; read it back from a fresh parse.
    Mermaid::MermaidParser parser;
    const Mermaid::ParseResult pr = parser.parse(m_source);
    const int i = pr.flowchart.indexOfNode(m_selectedNode);
    return i >= 0 ? pr.flowchart.nodes.at(i).label : QString();
}

// ---- Sequence reordering ----

QString DiagramCanvas::moveSelectedMessageSource(int delta)
{
    if (!supportsSequenceReorder() || m_selectedEdge < 0)
        return QString();
    return gestureResult(
        Mermaid::Edits::moveSequenceMessage(m_source, m_selectedEdge, delta));
}

QString DiagramCanvas::moveSelectedParticipantSource(int delta)
{
    if (!supportsSequenceReorder() || m_selectedNode.isEmpty())
        return QString();
    return gestureResult(Mermaid::Edits::moveSequenceParticipant(
        m_source, m_selectedNode, delta));
}

// ---- Ghost-edge drag ----

bool DiagramCanvas::startEdgeDrag(qreal x, qreal y)
{
    if (!supportsArrangement() || m_selectedNode.isEmpty())
        return false;
    m_edgeDragActive = true;
    m_edgeDragFrom = m_selectedNode;
    m_edgeDragPointer = toSceneCoords(x, y);
    m_edgeDragTarget.clear();
    update();
    return true;
}

void DiagramCanvas::updateEdgeDrag(qreal x, qreal y)
{
    if (!m_edgeDragActive)
        return;
    m_edgeDragPointer = toSceneCoords(x, y);
    const QString target = nodeAt(x, y);
    m_edgeDragTarget = (target == m_edgeDragFrom) ? QString() : target;
    update();
}

QString DiagramCanvas::finishEdgeDragSource()
{
    if (!m_edgeDragActive)
        return QString();
    const QString from = m_edgeDragFrom;
    const QString to = m_edgeDragTarget;
    cancelEdgeDrag();
    if (to.isEmpty())
        return QString();
    return gestureResult(Mermaid::Edits::insertEdge(m_source, from, to));
}

void DiagramCanvas::cancelEdgeDrag()
{
    if (!m_edgeDragActive)
        return;
    m_edgeDragActive = false;
    m_edgeDragFrom.clear();
    m_edgeDragTarget.clear();
    update();
}

// ---- Node dragging ----

QRectF DiagramCanvas::dragNodeRect() const
{
    for (const Diagram::Shape &s : m_scene.shapes)
        if (s.nodeId == m_dragNode)
            return s.rect;
    return QRectF();
}

bool DiagramCanvas::startNodeDrag(const QString &nodeId, qreal x, qreal y)
{
    if (!supportsArrangement() || nodeId.isEmpty())
        return false;
    const QRectF r = dragNodeRect();
    Q_UNUSED(r);
    QRectF nodeRect;
    for (const Diagram::Shape &s : m_scene.shapes)
        if (s.nodeId == nodeId)
            nodeRect = s.rect;
    if (nodeRect.isNull())
        return false;
    m_dragActive = true;
    m_dragNode = nodeId;
    m_dragGrab = toSceneCoords(x, y);
    m_dragOrigin = nodeRect.center();
    m_dragCenter = m_dragOrigin;
    m_guideXs.clear();
    m_guideYs.clear();
    return true;
}

void DiagramCanvas::updateNodeDrag(qreal x, qreal y)
{
    if (!m_dragActive)
        return;
    const QPointF pointer = toSceneCoords(x, y);
    QPointF center = m_dragOrigin + (pointer - m_dragGrab);
    // Snap to an 8 px grid, then to neighbour alignment guides.
    center = QPointF(qRound(center.x() / 8.0) * 8.0,
                     qRound(center.y() / 8.0) * 8.0);
    m_guideXs.clear();
    m_guideYs.clear();
    for (const Diagram::Shape &s : m_scene.shapes) {
        if (s.nodeId.isEmpty() || s.nodeId == m_dragNode)
            continue;
        const QPointF other = s.rect.center();
        if (qAbs(other.x() - center.x()) < 6.0) {
            center.setX(other.x());
            if (!m_guideXs.contains(other.x()))
                m_guideXs.append(other.x());
        }
        if (qAbs(other.y() - center.y()) < 6.0) {
            center.setY(other.y());
            if (!m_guideYs.contains(other.y()))
                m_guideYs.append(other.y());
        }
    }
    m_dragCenter = center;
    update();
}

QString DiagramCanvas::finishNodeDragSource()
{
    if (!m_dragActive)
        return QString();
    const QString dragged = m_dragNode;
    const QPointF target = m_dragCenter;
    cancelNodeDrag();
    if (target == m_dragOrigin)
        return QString();   // a no-movement write is a no-op

    // Full snapshot in scene (= source) order: every node's current center,
    // the dragged node at its new position.
    QList<QPair<QString, QPointF>> positions;
    QStringList seen;
    for (const Diagram::Shape &s : m_scene.shapes) {
        if (s.nodeId.isEmpty() || seen.contains(s.nodeId))
            continue;
        seen.append(s.nodeId);
        positions.append({ s.nodeId,
                           s.nodeId == dragged ? target : s.rect.center() });
    }
    const Mermaid::Edits::Result r =
        Mermaid::Edits::writeArrangement(m_source, positions);
    if (!r.ok || r.source == m_source)
        return QString();
    return r.source;
}

void DiagramCanvas::cancelNodeDrag()
{
    if (!m_dragActive)
        return;
    m_dragActive = false;
    m_dragNode.clear();
    m_guideXs.clear();
    m_guideYs.clear();
    update();
}

QString DiagramCanvas::resetArrangementSource() const
{
    const Mermaid::Edits::Result r =
        Mermaid::Edits::resetArrangement(m_source);
    if (!r.ok || r.source == m_source)
        return QString();
    return r.source;
}

// ---- Preview↔source linking ----

int DiagramCanvas::sourceOffsetAt(qreal x, qreal y) const
{
    const QString node = nodeAt(x, y);
    if (!node.isEmpty()) {
        for (const Diagram::Shape &s : m_scene.shapes)
            if (s.nodeId == node && s.srcStart >= 0)
                return s.srcStart;
    }
    const int edge = edgeAt(x, y);
    if (edge >= 0) {
        for (const Diagram::Path &p : m_scene.paths)
            if (p.edgeIndex == edge && p.srcStart >= 0)
                return p.srcStart;
    }
    return -1;
}

int DiagramCanvas::sourceOffsetForSelection() const
{
    if (!m_selectedNode.isEmpty()) {
        for (const Diagram::Shape &s : m_scene.shapes)
            if (s.nodeId == m_selectedNode && s.srcStart >= 0)
                return s.srcStart;
    }
    if (m_selectedEdge >= 0) {
        for (const Diagram::Path &p : m_scene.paths)
            if (p.edgeIndex == m_selectedEdge && p.srcStart >= 0)
                return p.srcStart;
    }
    return -1;
}

int DiagramCanvas::sourceLineForOffset(int offset) const
{
    if (offset < 0)
        return -1;
    int line = 1;
    const int end = qMin(offset, int(m_source.size()));
    for (int i = 0; i < end; ++i)
        if (m_source.at(i) == u'\n')
            ++line;
    return line;
}

void DiagramCanvas::highlightSourceOffset(int offset)
{
    QString bestNode;
    int bestEdge = -1;
    int bestLen = -1;
    if (offset >= 0) {
        for (const Diagram::Shape &s : m_scene.shapes) {
            if (s.nodeId.isEmpty() || s.srcStart < 0)
                continue;
            if (offset >= s.srcStart && offset <= s.srcStart + s.srcLen
                && (bestLen < 0 || s.srcLen < bestLen)) {
                bestNode = s.nodeId;
                bestEdge = -1;
                bestLen = s.srcLen;
            }
        }
        for (const Diagram::Path &p : m_scene.paths) {
            if (p.edgeIndex < 0 || p.srcStart < 0)
                continue;
            if (offset >= p.srcStart && offset <= p.srcStart + p.srcLen
                && (bestLen < 0 || p.srcLen < bestLen)) {
                bestNode.clear();
                bestEdge = p.edgeIndex;
                bestLen = p.srcLen;
            }
        }
    }
    if (bestNode == m_highlightNode && bestEdge == m_highlightEdge)
        return;
    m_highlightNode = bestNode;
    m_highlightEdge = bestEdge;
    update();
}

Diagram::SceneColors DiagramCanvas::sceneColors() const
{
    Diagram::SceneColors colors;
    colors.background = Qt::transparent;
    colors.nodeFill = m_nodeFill;
    colors.nodeStroke = m_nodeStroke;
    colors.edge = m_edge;
    colors.label = m_label;
    colors.edgeLabel = m_edgeLabel;
    colors.edgeLabelBackground = m_edgeLabelBg;
    colors.subgraphFill = m_subgraphFill;
    colors.subgraphStroke = m_subgraphStroke;
    colors.noteFill = m_noteFill;
    colors.noteStroke = m_noteStroke;
    colors.activationFill = m_activationFill;
    return colors;
}

void DiagramCanvas::paint(QPainter *painter)
{
    if (!m_hasScene || m_scene.isEmpty())
        return;
    painter->save();
    painter->scale(m_renderScale, m_renderScale);
    Diagram::paintScene(painter, m_scene, sceneColors(), m_fontFamily);

    // Selection ring and cursor-link highlight.
    auto ringNode = [&](const QString &id, qreal width, int alpha) {
        QColor c = m_selectionRing;
        c.setAlpha(alpha);
        painter->setPen(QPen(c, width));
        painter->setBrush(Qt::NoBrush);
        for (const Diagram::Shape &s : m_scene.shapes)
            if (s.nodeId == id)
                painter->drawRoundedRect(s.rect.adjusted(-3, -3, 3, 3), 5, 5);
    };
    auto ringEdge = [&](int index, qreal width, int alpha) {
        QColor c = m_selectionRing;
        c.setAlpha(alpha);
        QPen pen(c, width);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        for (const Diagram::Path &p : m_scene.paths)
            if (p.edgeIndex == index)
                painter->drawPath(p.path);
    };
    if (!m_highlightNode.isEmpty() && m_highlightNode != m_selectedNode)
        ringNode(m_highlightNode, 1.6, 130);
    if (m_highlightEdge >= 0 && m_highlightEdge != m_selectedEdge)
        ringEdge(m_highlightEdge, 4.0, 90);
    if (!m_selectedNode.isEmpty())
        ringNode(m_selectedNode, 2.2, 255);
    if (m_selectedEdge >= 0)
        ringEdge(m_selectedEdge, 4.5, 150);

    // Ghost edge while anchor-dragging; drops on the ringed target.
    if (m_edgeDragActive) {
        QRectF fromRect;
        for (const Diagram::Shape &s : m_scene.shapes)
            if (s.nodeId == m_edgeDragFrom)
                fromRect = s.rect;
        if (!fromRect.isNull()) {
            QColor c = m_selectionRing;
            c.setAlpha(190);
            painter->setPen(QPen(c, 2.0, Qt::DashLine, Qt::RoundCap));
            const QPointF start = fromRect.center();
            painter->drawLine(start, m_edgeDragPointer);
        }
        if (!m_edgeDragTarget.isEmpty())
            ringNode(m_edgeDragTarget, 2.2, 220);
    }

    // Drag ghost and alignment guides; the drag commits on release.
    if (m_dragActive) {
        QColor guide = m_selectionRing;
        guide.setAlpha(110);
        painter->setPen(QPen(guide, 1.0, Qt::DashLine));
        for (const qreal gx : m_guideXs)
            painter->drawLine(QPointF(gx, 0),
                              QPointF(gx, m_scene.bounds.height()));
        for (const qreal gy : m_guideYs)
            painter->drawLine(QPointF(0, gy),
                              QPointF(m_scene.bounds.width(), gy));
        const QRectF orig = dragNodeRect();
        if (!orig.isNull()) {
            QRectF ghost = orig;
            ghost.moveCenter(m_dragCenter);
            QColor c = m_selectionRing;
            c.setAlpha(200);
            painter->setPen(QPen(c, 2.0, Qt::DashLine));
            QColor fill = m_selectionRing;
            fill.setAlpha(30);
            painter->setBrush(fill);
            painter->drawRoundedRect(ghost, 6, 6);
        }
    }
    painter->restore();
}

bool DiagramCanvas::savePng(const QString &filePath, qreal scale) const
{
    if (!m_hasScene || m_scene.isEmpty() || filePath.isEmpty())
        return false;
    scale = qBound(0.5, scale, 8.0);
    // The requested raster is bounded twice: each edge, and the total area. A
    // scene wide enough to exceed either is scaled down to fit rather than
    // asking for a backing store the process cannot serve.
    qreal w = m_scene.bounds.width() * scale;
    qreal h = m_scene.bounds.height() * scale;
    const qreal edgeFit = qMin(1.0, qMin(Diagram::kMaxRasterEdge / qMax(w, 1.0),
                                         Diagram::kMaxRasterEdge / qMax(h, 1.0)));
    w *= edgeFit;
    h *= edgeFit;
    const qreal area = qMax(w * h, 1.0);
    const qreal areaFit =
        qMin(1.0, std::sqrt(double(Diagram::kMaxRasterPixels) / area));
    w *= areaFit;
    h *= areaFit;
    const QSize px(qMax(1, int(w)), qMax(1, int(h)));
    QImage image(px, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return false;
    image.fill(m_pageBackground);
    QPainter p(&image);
    // Paint at the scale the raster was actually allocated for, so a scene
    // clamped to fit is drawn whole rather than cropped to its top-left.
    p.scale(scale * edgeFit * areaFit, scale * edgeFit * areaFit);
    Diagram::paintScene(&p, m_scene, sceneColors(), m_fontFamily);
    p.end();
    return image.save(filePath, "PNG");
}

QString DiagramCanvas::textDiagram() const
{
    if (!sceneCurrent() || m_scene.isEmpty())
        return QString();
    return Diagram::renderText(m_scene);
}
