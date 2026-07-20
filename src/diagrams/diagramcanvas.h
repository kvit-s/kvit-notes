// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMCANVAS_H
#define DIAGRAMCANVAS_H

#include <QQuickPaintedItem>
#include <QColor>
#include <QFutureWatcher>
#include <QString>

#include "diagrampainter.h"
#include "diagramscene.h"
#include "mermaidedits.h"
#include "mermaidrenderer.h"

// Native painter for Mermaid diagrams. It owns a Mermaid source, parses and
// lays it out OFF the UI thread (QtConcurrent, with a revision guard so a
// pooled/reused delegate never shows another block's late result), caches the
// last valid scene, and paints it with QPainter — no browser surface, no work
// in paint(). When the new source is invalid it keeps painting the last good
// scene and exposes the error, so a delegate can show "preview is from the
// last valid source". Colors are resolved from the theme tokens bound as
// properties, so a theme change repaints without re-laying out.
class DiagramCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontChanged)
    Q_PROPERTY(qreal renderScale READ renderScale WRITE setRenderScale NOTIFY renderScaleChanged)

    // Theme tokens (resolved at paint time).
    Q_PROPERTY(QColor nodeFillColor MEMBER m_nodeFill NOTIFY themeChanged)
    Q_PROPERTY(QColor nodeStrokeColor MEMBER m_nodeStroke NOTIFY themeChanged)
    Q_PROPERTY(QColor edgeColor MEMBER m_edge NOTIFY themeChanged)
    Q_PROPERTY(QColor labelColor MEMBER m_label NOTIFY themeChanged)
    Q_PROPERTY(QColor edgeLabelColor MEMBER m_edgeLabel NOTIFY themeChanged)
    Q_PROPERTY(QColor edgeLabelBackground MEMBER m_edgeLabelBg NOTIFY themeChanged)
    Q_PROPERTY(QColor subgraphFillColor MEMBER m_subgraphFill NOTIFY themeChanged)
    Q_PROPERTY(QColor subgraphStrokeColor MEMBER m_subgraphStroke NOTIFY themeChanged)
    Q_PROPERTY(QColor noteFillColor MEMBER m_noteFill NOTIFY themeChanged)
    Q_PROPERTY(QColor noteStrokeColor MEMBER m_noteStroke NOTIFY themeChanged)
    Q_PROPERTY(QColor activationFillColor MEMBER m_activationFill NOTIFY themeChanged)
    Q_PROPERTY(QColor pageBackgroundColor MEMBER m_pageBackground NOTIFY themeChanged)

    // §20.1 selection state and §20.5 linking.
    Q_PROPERTY(QString selectedNodeId READ selectedNodeId
                   WRITE setSelectedNodeId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedEdgeIndex READ selectedEdgeIndex
                   WRITE setSelectedEdgeIndex NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // Gestures are enabled only while the rendered scene corresponds to the
    // current source (§20.1); a last-good preview of invalid source gates off.
    Q_PROPERTY(bool sceneCurrent READ sceneCurrent NOTIFY resultChanged)
    Q_PROPERTY(QColor selectionRingColor MEMBER m_selectionRing NOTIFY themeChanged)
    // §20.3 manual arrangement: available for rendered flowcharts; a pos line
    // in the fence switches the layout into arranged mode.
    Q_PROPERTY(bool supportsArrangement READ supportsArrangement NOTIFY resultChanged)
    Q_PROPERTY(bool hasArrangement READ hasArrangement NOTIFY resultChanged)
    // §20.4 gestures share the flowchart+current gate; the last refusal text
    // is surfaced through the status affordance.
    Q_PROPERTY(QString gestureError READ gestureError NOTIFY gestureErrorChanged)
    // Phase 5d: sequence diagrams reorder by statement order instead.
    Q_PROPERTY(bool supportsSequenceReorder READ supportsSequenceReorder NOTIFY resultChanged)

    // Outputs for the delegate.
    Q_PROPERTY(qreal sceneWidth READ sceneWidth NOTIFY sceneChanged)
    Q_PROPERTY(qreal sceneHeight READ sceneHeight NOTIFY sceneChanged)
    Q_PROPERTY(bool hasScene READ hasScene NOTIFY sceneChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY renderingChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY resultChanged)
    Q_PROPERTY(bool unsupportedFamily READ unsupportedFamily NOTIFY resultChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY resultChanged)
    Q_PROPERTY(int errorLine READ errorLine NOTIFY resultChanged)
    Q_PROPERTY(int errorColumn READ errorColumn NOTIFY resultChanged)
    Q_PROPERTY(int diagnosticCount READ diagnosticCount NOTIFY resultChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY resultChanged)
    Q_PROPERTY(QString accessibleTitle READ accessibleTitle NOTIFY resultChanged)

public:
    explicit DiagramCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    // Drop the retained last-good scene and every per-block state. The
    // "preview is from the last valid source" behavior is scoped to edits of
    // ONE block; a pooled delegate reused for a different block must call
    // this so the previous block's scene is never presented as this block's
    // last valid render. Re-schedules a render of the current source (an LRU
    // cache hit when unchanged), so a reset never strands the canvas.
    Q_INVOKABLE void resetScene();

    // Save the current scene as a PNG at the given device scale (Phase 4
    // "Save PNG"). Renders with the currently bound theme colors on the
    // theme's background. Returns false when there is no scene or the file
    // cannot be written.
    Q_INVOKABLE bool savePng(const QString &filePath, qreal scale = 2.0) const;

    // The current scene as a Unicode box-drawing rendition — the "Copy as
    // text" action. Reads the retained scene under the same has-scene guard as
    // savePng (pooled delegates reset scenes; this never renders on demand).
    // Empty when there is no scene.
    Q_INVOKABLE QString textDiagram() const;

    // ---- §20.1 hit-testing and selection (item coordinates) ----
    Q_INVOKABLE QString nodeAt(qreal x, qreal y) const;
    Q_INVOKABLE int edgeAt(qreal x, qreal y) const;   // family edge index, -1
    Q_INVOKABLE void clearSelection();
    // Select the next/previous node in scene order (keyboard cycling, §12);
    // returns the newly selected id.
    Q_INVOKABLE QString cycleNode(int delta);
    // A short human/screen-reader description of the current selection.
    Q_INVOKABLE QString selectionLabel() const;
    // The selected node's rect in item coordinates (for affordances).
    Q_INVOKABLE QRectF selectionRect() const;

    // ---- §20.5 preview↔source linking ----
    // Source offset of the element at the point / of the selection; -1 none.
    Q_INVOKABLE int sourceOffsetAt(qreal x, qreal y) const;
    Q_INVOKABLE int sourceOffsetForSelection() const;
    Q_INVOKABLE int sourceLineForOffset(int offset) const;   // 1-based
    // Highlight the element whose span contains the offset (cursor linking).
    Q_INVOKABLE void highlightSourceOffset(int offset);

    // ---- §20.4 gesture wrappers -----------------------------------------
    // Each computes the gesture through the §20 edit engine and returns the
    // full new fence source (one undo step via updateContent), or an empty
    // string with gestureError set when the gesture is refused (§20.2).
    Q_INVOKABLE QString labelSelectionSource(const QString &newLabel);
    Q_INVOKABLE QString shapeSelectionSource(const QString &shapeName);
    Q_INVOKABLE QString renameSelectionSource(const QString &newId);
    Q_INVOKABLE QString deleteSelectionSource();
    Q_INVOKABLE QString edgeStyleSelectionSource(const QString &strokeName);
    Q_INVOKABLE QString styleSelectionSource(const QColor &fill,
                                             const QColor &stroke);
    Q_INVOKABLE QString quickAddSelectionSource();
    // The selected node's current label (seeds the inline label editor).
    Q_INVOKABLE QString selectedNodeLabel() const;
    QString gestureError() const { return m_gestureError; }

    // Phase 5d: move the selected message down/up (+1/-1) one message, or
    // the selected participant right/left one declaration (§20.4).
    Q_INVOKABLE QString moveSelectedMessageSource(int delta);
    Q_INVOKABLE QString moveSelectedParticipantSource(int delta);
    bool supportsSequenceReorder() const
    {
        return m_family == Mermaid::DiagramType::Sequence && sceneCurrent();
    }

    // ---- §20.1 anchor drag: ghost edge, committed on drop ----
    Q_INVOKABLE bool startEdgeDrag(qreal x, qreal y);
    Q_INVOKABLE void updateEdgeDrag(qreal x, qreal y);
    Q_INVOKABLE QString finishEdgeDragSource();
    Q_INVOKABLE void cancelEdgeDrag();

    // ---- §20.3 node dragging (item coordinates; commit on release) ----
    // Begin a drag on the node under (x, y). Returns false when the node is
    // unknown or arrangement is unavailable (non-flowchart / stale scene).
    Q_INVOKABLE bool startNodeDrag(const QString &nodeId, qreal x, qreal y);
    Q_INVOKABLE void updateNodeDrag(qreal x, qreal y);
    // Commit: the full-snapshot pos-line write (§20.3). Returns the new fence
    // source, or an empty string when nothing changed / the drag was invalid.
    Q_INVOKABLE QString finishNodeDragSource();
    Q_INVOKABLE void cancelNodeDrag();
    // Reset layout: the source with the pos line removed ("" when absent).
    Q_INVOKABLE QString resetArrangementSource() const;

    bool supportsArrangement() const
    {
        return m_family == Mermaid::DiagramType::Flowchart && sceneCurrent();
    }
    bool hasArrangement() const { return m_hasArrangement; }

    QString selectedNodeId() const { return m_selectedNode; }
    void setSelectedNodeId(const QString &id);
    int selectedEdgeIndex() const { return m_selectedEdge; }
    void setSelectedEdgeIndex(int index);
    bool hasSelection() const
    {
        return !m_selectedNode.isEmpty() || m_selectedEdge >= 0;
    }
    bool sceneCurrent() const
    {
        return m_hasScene && !m_hasError && m_sceneSource == m_source;
    }

    QString source() const { return m_source; }
    void setSource(const QString &s);
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &f);
    int fontPixelSize() const { return m_fontPixelSize; }
    void setFontPixelSize(int px);
    qreal renderScale() const { return m_renderScale; }
    void setRenderScale(qreal s);

    qreal sceneWidth() const { return m_scene.bounds.width(); }
    qreal sceneHeight() const { return m_scene.bounds.height(); }
    bool hasScene() const { return m_hasScene; }
    bool rendering() const { return m_rendering; }
    bool hasError() const { return m_hasError; }
    bool unsupportedFamily() const { return m_unsupported; }
    QString errorText() const { return m_errorText; }
    int errorLine() const { return m_errorLine; }
    int errorColumn() const { return m_errorColumn; }
    int diagnosticCount() const { return m_diagnosticCount; }
    QString summary() const { return m_summary; }
    QString accessibleTitle() const { return m_accTitle; }

signals:
    void sourceChanged();
    void fontChanged();
    void renderScaleChanged();
    void themeChanged();
    void sceneChanged();
    void renderingChanged();
    void resultChanged();
    void selectionChanged();
    void gestureErrorChanged();

private:
    void scheduleRender();
    void applyResult(const Diagram::RenderResult &r, const QString &src);
    void updateImplicitSize();
    QPointF toSceneCoords(qreal x, qreal y) const
    {
        const qreal s = m_renderScale > 0 ? m_renderScale : 1.0;
        return QPointF(x / s, y / s);
    }

    QString m_source;
    QString m_fontFamily = QStringLiteral("sans-serif");
    int m_fontPixelSize = 14;
    qreal m_renderScale = 1.0;

    QColor m_nodeFill = QColor(QStringLiteral("#eef3fb"));
    QColor m_nodeStroke = QColor(QStringLiteral("#4b6ea8"));
    QColor m_edge = QColor(QStringLiteral("#4b5563"));
    QColor m_label = QColor(QStringLiteral("#1f2937"));
    QColor m_edgeLabel = QColor(QStringLiteral("#374151"));
    QColor m_edgeLabelBg = QColor(QStringLiteral("#ffffff"));
    QColor m_subgraphFill = QColor(0, 0, 0, 12);
    QColor m_subgraphStroke = QColor(QStringLiteral("#94a3b8"));
    QColor m_noteFill = QColor(QStringLiteral("#fdf6c9"));
    QColor m_noteStroke = QColor(QStringLiteral("#c9b855"));
    QColor m_activationFill = QColor(QStringLiteral("#e8edf5"));
    QColor m_pageBackground = QColor(Qt::white);
    QColor m_selectionRing = QColor(QStringLiteral("#3b82f6"));

    Diagram::SceneColors sceneColors() const;

    QString m_selectedNode;
    int m_selectedEdge = -1;
    QString m_highlightNode;
    int m_highlightEdge = -1;
    QString m_sceneSource;   // the source the current scene was rendered from
    Mermaid::DiagramType m_family = Mermaid::DiagramType::Unknown;
    bool m_hasArrangement = false;

    // Drag state (§20.3): scene coordinates; ghost committed on release.
    bool m_dragActive = false;
    QString m_dragNode;
    QPointF m_dragGrab;      // pointer position at press (scene coords)
    QPointF m_dragOrigin;    // node center at press
    QPointF m_dragCenter;    // current (snapped) node center
    QList<qreal> m_guideXs;  // alignment guides while dragging
    QList<qreal> m_guideYs;
    QRectF dragNodeRect() const;

    // §20.4 gesture state.
    QString m_gestureError;
    QString gestureResult(const Mermaid::Edits::Result &r);
    // Ghost-edge drag (§20.1 anchor drag).
    bool m_edgeDragActive = false;
    QString m_edgeDragFrom;
    QPointF m_edgeDragPointer;   // scene coords
    QString m_edgeDragTarget;

    Diagram::Scene m_scene;          // last valid scene (kept across errors)
    bool m_hasScene = false;
    bool m_rendering = false;
    bool m_hasError = false;
    bool m_unsupported = false;
    QString m_errorText;
    int m_errorLine = 0;
    int m_errorColumn = 0;
    int m_diagnosticCount = 0;
    QString m_summary;
    QString m_accTitle;

    quint64 m_revision = 0;
};

#endif // DIAGRAMCANVAS_H
