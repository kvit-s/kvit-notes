// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramlayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QtMath>

#include <cmath>

using namespace Mermaid;

// State-diagram layout. Composite states are laid out
// recursively: each composite's members are placed by the shared layered core
// in local coordinates, the composite then participates in its parent scope as
// one node sized to its content, and transitions are routed globally between
// the final absolute rects. Notes join their state's scope as pseudo-nodes on
// dashed tethers.
namespace Diagram {

namespace {

constexpr double kRankGap = 52.0;
constexpr double kNodeGap = 36.0;
constexpr double kMargin = 16.0;
constexpr double kPadX = 12.0;
constexpr double kTitleBand = 24.0;   // composite title band height

struct ResolvedStyle {
    QColor fill, stroke;
    qreal strokeWidth = 0;
    bool bold = false;
};
ResolvedStyle resolveStyle(const StateNode &s, const StateAst &ast)
{
    ResolvedStyle r;
    for (const QString &cls : s.cssClasses) {
        const ClassDef d = ast.classDefs.value(cls);
        if (d.hasFill) r.fill = d.fill;
        if (d.hasStroke) r.stroke = d.stroke;
        if (d.strokeWidth > 0) r.strokeWidth = d.strokeWidth;
        if (d.bold) r.bold = true;
    }
    return r;
}

QPointF borderPoint(const QRectF &r, const QPointF &toward)
{
    const QPointF c = r.center();
    QPointF d = toward - c;
    if (qFuzzyIsNull(d.x()) && qFuzzyIsNull(d.y()))
        return c;
    const double hw = r.width() / 2.0;
    const double hh = r.height() / 2.0;
    const double tx = qFuzzyIsNull(d.x()) ? 1e9 : hw / qAbs(d.x());
    const double ty = qFuzzyIsNull(d.y()) ? 1e9 : hh / qAbs(d.y());
    return c + d * qMin(tx, ty);
}

class StateLayout
{
public:
    StateLayout(const StateAst &ast, const LayoutOptions &opts, Scene &scene)
        : m_ast(ast), m_opts(opts), m_scene(scene),
          m_font(opts.fontFamily), m_fm((m_font.setPixelSize(opts.fontPixelSize),
                                         m_font))
    {
    }

    void run();

private:
    struct Item {
        int stateIndex = -1;   // >= 0: a state
        int noteIndex = -1;    // >= 0: a note pseudo-node
        QSizeF size;
        QPointF center;        // scope-local coordinates
    };

    double textW(const QString &s) const
    {
        double w = 0;
        for (const QString &l : labelLines(s))
            w = qMax(w, m_fm.horizontalAdvance(l));
        return w;
    }
    double textH(const QString &s) const
    {
        if (s.isEmpty())
            return 0.0;
        return labelLines(s).size() * m_fm.height();
    }

    QSizeF leafSize(const StateNode &s) const;
    // The direct child of `scope` on stateIndex's ancestor chain, as an item
    // id; -1 when the chain does not pass through `scope`.
    int projectToScope(int stateIndex, int scope) const;
    QSizeF placeScope(int scope);
    void emitScope(int scope, const QPointF &origin);
    void emitLeafState(const StateNode &s, const QRectF &r);

    const StateAst &m_ast;
    const LayoutOptions &m_opts;
    Scene &m_scene;
    QFont m_font;
    QFontMetricsF m_fm;

    QList<Item> m_items;
    QHash<int, int> m_itemOfState;       // state index -> item id
    QHash<int, int> m_itemOfNote;        // note index -> item id
    QHash<int, QList<int>> m_children;   // scope (-1 root) -> item ids
    QHash<int, QRectF> m_absRect;        // item id -> absolute rect
};

QSizeF StateLayout::leafSize(const StateNode &s) const
{
    const bool horizontal = m_ast.direction == Direction::LR
                            || m_ast.direction == Direction::RL;
    switch (s.kind) {
    case StateKind::Start: return QSizeF(14, 14);
    case StateKind::End: return QSizeF(18, 18);
    case StateKind::Choice: return QSizeF(30, 30);
    case StateKind::Fork:
    case StateKind::Join:
        return horizontal ? QSizeF(8, 64) : QSizeF(64, 8);
    case StateKind::Normal:
        break;
    }
    const double lineH = m_fm.height();
    double w = qMax(60.0, textW(s.label) + 2 * kPadX);
    double h = qMax(30.0, textH(s.label) + 14);
    if (!s.descriptions.isEmpty()) {
        for (const QString &d : s.descriptions)
            w = qMax(w, textW(d) + 2 * kPadX);
        h += s.descriptions.size() * lineH + 8;
    }
    return QSizeF(w, h);
}

int StateLayout::projectToScope(int stateIndex, int scope) const
{
    int cur = stateIndex;
    while (cur >= 0) {
        const int parent = m_ast.states.at(cur).parentIndex;
        if (parent == scope)
            return m_itemOfState.value(cur, -1);
        cur = parent;
    }
    return -1;
}

QSizeF StateLayout::placeScope(int scope)
{
    const QList<int> kids = m_children.value(scope);
    if (kids.isEmpty())
        return QSizeF(80, 40);

    // Sizes: composites first size their own contents recursively.
    QList<QSizeF> sizes;
    sizes.reserve(kids.size());
    for (const int itemId : kids) {
        Item &item = m_items[itemId];
        if (item.stateIndex >= 0
            && m_ast.states.at(item.stateIndex).composite) {
            const QSizeF inner = placeScope(item.stateIndex);
            item.size = QSizeF(inner.width() + 2 * kPadX,
                               inner.height() + kTitleBand + kPadX);
        }
        sizes.append(item.size);
    }

    // Edges projected to this scope.
    QList<LayeredEdge> ledges;
    for (const StateTransition &t : m_ast.transitions) {
        const int a = projectToScope(m_ast.indexOfState(t.from), scope);
        const int b = projectToScope(m_ast.indexOfState(t.to), scope);
        if (a < 0 || b < 0 || a == b)
            continue;
        ledges.append({ int(kids.indexOf(a)), int(kids.indexOf(b)), 1 });
    }
    for (int n = 0; n < m_ast.notes.size(); ++n) {
        const int noteItem = m_itemOfNote.value(n, -1);
        if (noteItem < 0 || !kids.contains(noteItem))
            continue;
        const int st = m_ast.indexOfState(m_ast.notes.at(n).stateId);
        const int stItem = st >= 0 ? m_itemOfState.value(st, -1) : -1;
        if (stItem >= 0 && kids.contains(stItem)) {
            if (m_ast.notes.at(n).leftOf)
                ledges.append({ int(kids.indexOf(noteItem)),
                                int(kids.indexOf(stItem)), 1 });
            else
                ledges.append({ int(kids.indexOf(stItem)),
                                int(kids.indexOf(noteItem)), 1 });
        }
    }

    const QList<QPointF> centers =
        layeredCenters(sizes, ledges, m_ast.direction, kRankGap, kNodeGap);

    // Normalize to a (0,0) top-left content origin.
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (int k = 0; k < kids.size(); ++k) {
        minX = qMin(minX, centers.at(k).x() - sizes.at(k).width() / 2);
        minY = qMin(minY, centers.at(k).y() - sizes.at(k).height() / 2);
        maxX = qMax(maxX, centers.at(k).x() + sizes.at(k).width() / 2);
        maxY = qMax(maxY, centers.at(k).y() + sizes.at(k).height() / 2);
    }
    for (int k = 0; k < kids.size(); ++k)
        m_items[kids.at(k)].center = centers.at(k) - QPointF(minX, minY);
    return QSizeF(maxX - minX, maxY - minY);
}

void StateLayout::emitLeafState(const StateNode &s, const QRectF &r)
{
    const ResolvedStyle st = resolveStyle(s, m_ast);
    const double lineH = m_fm.height();
    switch (s.kind) {
    case StateKind::Start: {
        Shape sh;
        sh.kind = Shape::Circle;
        sh.rect = r;
        sh.fillRole = Role::NodeStroke;   // solid dark disc
        sh.strokeRole = Role::NodeStroke;
        sh.nodeId = s.id;
        m_scene.shapes.append(sh);
        return;
    }
    case StateKind::End: {
        Shape outer;
        outer.kind = Shape::Circle;
        outer.rect = r;
        outer.fillRole = Role::Background;
        outer.strokeRole = Role::NodeStroke;
        outer.nodeId = s.id;
        m_scene.shapes.append(outer);
        Shape inner;
        inner.kind = Shape::Circle;
        inner.rect = r.adjusted(4, 4, -4, -4);
        inner.fillRole = Role::NodeStroke;
        inner.strokeRole = Role::NodeStroke;
        m_scene.shapes.append(inner);
        return;
    }
    case StateKind::Choice: {
        Shape sh;
        sh.kind = Shape::Rhombus;
        sh.rect = r;
        sh.nodeId = s.id;
        m_scene.shapes.append(sh);
        return;
    }
    case StateKind::Fork:
    case StateKind::Join: {
        Shape sh;
        sh.kind = Shape::Rect;
        sh.rect = r;
        sh.fillRole = Role::NodeStroke;
        sh.strokeRole = Role::NodeStroke;
        sh.nodeId = s.id;
        m_scene.shapes.append(sh);
        return;
    }
    case StateKind::Normal:
        break;
    }

    Shape box;
    box.kind = Shape::RoundRect;
    box.rect = r;
    box.nodeId = s.id;
    box.srcStart = s.srcSpan.start;
    box.srcLen = s.srcSpan.length;
    box.strokeWidth = st.strokeWidth > 0 ? st.strokeWidth : 1.5;
    if (st.fill.isValid()) box.fillOverride = st.fill;
    if (st.stroke.isValid()) box.strokeOverride = st.stroke;
    m_scene.shapes.append(box);

    const double titleH = textH(s.label) + 14;
    Text title;
    title.text = s.label;
    title.role = Role::Label;
    title.bold = st.bold || !s.descriptions.isEmpty();
    title.fontSize = m_opts.fontPixelSize;
    title.rect = QRectF(r.left(), r.top(), r.width(), titleH);
    m_scene.texts.append(title);

    if (!s.descriptions.isEmpty()) {
        Path sep;
        QPainterPath pp(QPointF(r.left(), r.top() + titleH));
        pp.lineTo(r.right(), r.top() + titleH);
        sep.path = pp;
        sep.strokeRole = Role::NodeStroke;
        sep.strokeWidth = 1.0;
        m_scene.paths.append(sep);
        double y = r.top() + titleH + 4;
        for (const QString &d : s.descriptions) {
            Text tx;
            tx.text = d;
            tx.role = Role::Label;
            tx.fontSize = qMax(10, m_opts.fontPixelSize - 1);
            tx.align = Qt::AlignLeft | Qt::AlignVCenter;
            tx.rect = QRectF(r.left() + kPadX, y, r.width() - 2 * kPadX, lineH);
            m_scene.texts.append(tx);
            y += lineH;
        }
    }
}

void StateLayout::emitScope(int scope, const QPointF &origin)
{
    for (const int itemId : m_children.value(scope)) {
        const Item &item = m_items.at(itemId);
        const QRectF r(origin.x() + item.center.x() - item.size.width() / 2,
                       origin.y() + item.center.y() - item.size.height() / 2,
                       item.size.width(), item.size.height());
        m_absRect.insert(itemId, r);
        if (item.stateIndex >= 0) {
            const StateNode &s = m_ast.states.at(item.stateIndex);
            if (s.composite) {
                Group g;
                g.rect = r;
                g.title = s.label;
                m_scene.groups.append(g);
                emitScope(item.stateIndex,
                          r.topLeft() + QPointF(kPadX, kTitleBand));
            } else {
                emitLeafState(s, r);
            }
        } else if (item.noteIndex >= 0) {
            const StateNote &n = m_ast.notes.at(item.noteIndex);
            Shape sh;
            sh.kind = Shape::Rect;
            sh.rect = r;
            sh.fillRole = Role::NoteFill;
            sh.strokeRole = Role::NoteStroke;
            sh.strokeWidth = 1.0;
            m_scene.shapes.append(sh);
            Text tx;
            tx.text = n.text;
            tx.role = Role::Label;
            tx.fontSize = qMax(10, m_opts.fontPixelSize - 1);
            tx.rect = r;
            m_scene.texts.append(tx);
        }
    }
}

void StateLayout::run()
{
    // Items for states.
    for (int i = 0; i < m_ast.states.size(); ++i) {
        Item item;
        item.stateIndex = i;
        if (!m_ast.states.at(i).composite)
            item.size = leafSize(m_ast.states.at(i));
        m_items.append(item);
        const int itemId = m_items.size() - 1;
        m_itemOfState.insert(i, itemId);
        m_children[m_ast.states.at(i).parentIndex].append(itemId);
    }
    // Items for notes (placed in their state's scope).
    for (int n = 0; n < m_ast.notes.size(); ++n) {
        const StateNote &note = m_ast.notes.at(n);
        Item item;
        item.noteIndex = n;
        const double w = qMin(textW(note.text), 260.0) + 20;
        const double h = qMax(textH(note.text), double(m_fm.height())) + 12;
        item.size = QSizeF(w, h);
        m_items.append(item);
        const int itemId = m_items.size() - 1;
        m_itemOfNote.insert(n, itemId);
        const int st = m_ast.indexOfState(note.stateId);
        m_children[st >= 0 ? m_ast.states.at(st).parentIndex : -1]
            .append(itemId);
    }

    placeScope(-1);
    emitScope(-1, QPointF(0, 0));

    // ---- transitions: routed globally between absolute rects ----
    const double lineH = m_fm.height();
    for (int ti = 0; ti < m_ast.transitions.size(); ++ti) {
        const StateTransition &t = m_ast.transitions.at(ti);
        const int a = m_itemOfState.value(m_ast.indexOfState(t.from), -1);
        const int b = m_itemOfState.value(m_ast.indexOfState(t.to), -1);
        if (a < 0 || b < 0)
            continue;
        const QRectF ra = m_absRect.value(a);
        const QRectF rb = m_absRect.value(b);
        Path p;
        p.edgeIndex = ti;
        p.srcStart = t.srcSpan.start;
        p.srcLen = t.srcSpan.length;
        p.strokeWidth = 1.4;
        p.endMarker = Marker::Arrow;
        QPointF pa, pb;
        if (a == b) {
            pa = QPointF(ra.right(), ra.center().y() - ra.height() * 0.2);
            pb = QPointF(ra.right(), ra.center().y() + ra.height() * 0.2);
            const double bulge = qMax(24.0, ra.width() * 0.4);
            QPainterPath pp(pa);
            pp.cubicTo(pa + QPointF(bulge, -bulge * 0.3),
                       pb + QPointF(bulge, bulge * 0.3), pb);
            p.path = pp;
            p.startDir = QPointF(-1, 0);
            p.endDir = QPointF(-1, 0);
        } else {
            pa = borderPoint(ra, rb.center());
            pb = borderPoint(rb, ra.center());
            QPainterPath pp(pa);
            pp.lineTo(pb);
            p.path = pp;
            QPointF d = pb - pa;
            const double len = std::hypot(d.x(), d.y());
            if (len > 0.001)
                d /= len;
            p.startDir = -d;
            p.endDir = d;
        }
        p.startPoint = pa;
        p.endPoint = pb;
        m_scene.paths.append(p);

        if (!t.label.isEmpty()) {
            Text tx;
            tx.text = t.label;
            tx.role = Role::EdgeLabel;
            tx.fontSize = qMax(10, m_opts.fontPixelSize - 1);
            tx.hasBackground = true;
            const QPointF mid = (pa + pb) / 2.0;
            const double w = textW(t.label) + 8;
            tx.rect = QRectF(mid.x() - w / 2, mid.y() - lineH / 2, w, lineH);
            m_scene.texts.append(tx);
        }
    }

    // ---- note tethers ----
    for (int n = 0; n < m_ast.notes.size(); ++n) {
        const StateNote &note = m_ast.notes.at(n);
        const int st = m_ast.indexOfState(note.stateId);
        if (st < 0)
            continue;
        const int noteItem = m_itemOfNote.value(n, -1);
        const int stItem = m_itemOfState.value(st, -1);
        if (noteItem < 0 || stItem < 0)
            continue;
        const QRectF rn = m_absRect.value(noteItem);
        const QRectF rs = m_absRect.value(stItem);
        Path p;
        p.penStyle = Qt::DashLine;
        p.strokeRole = Role::NoteStroke;
        p.strokeWidth = 1.0;
        const QPointF from = borderPoint(rn, rs.center());
        const QPointF to = borderPoint(rs, rn.center());
        QPainterPath pp(from);
        pp.lineTo(to);
        p.path = pp;
        p.startPoint = from;
        p.endPoint = to;
        m_scene.paths.append(p);
    }
}

} // namespace

Scene layoutStateDiagram(const StateAst &ast, const LayoutOptions &opts)
{
    Scene scene;
    scene.accTitle = !ast.accTitle.isEmpty() ? ast.accTitle : ast.title;
    scene.accDescr = ast.accDescr;

    // Pseudo start/end states are not counted in the summary.
    int visible = 0;
    for (const StateNode &s : ast.states)
        if (s.kind == StateKind::Normal)
            ++visible;
    scene.summary = QStringLiteral("Mermaid state diagram with %1 state%2 and "
                                   "%3 transition%4")
                        .arg(visible).arg(visible == 1 ? "" : "s")
                        .arg(ast.transitions.size())
                        .arg(ast.transitions.size() == 1 ? "" : "s");
    if (ast.states.isEmpty())
        return scene;

    StateLayout layout(ast, opts, scene);
    layout.run();
    finalizeSceneBounds(scene, kMargin);
    return scene;
}

} // namespace Diagram
