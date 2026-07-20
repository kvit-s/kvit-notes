// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramlayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>

#include <algorithm>

using namespace Mermaid;

// Sequence-diagram layout (diagrams-prd.md §8.4): lifelines establish columns,
// messages and fragments establish vertical bands, and labels expand columns
// before final placement. Deterministic for identical source, font, and
// options; visually follows the conventions a Mermaid user recognizes
// (activation bars, dashed return arrows, note boxes, labeled fragment frames)
// without copying Mermaid.js pixel geometry.
namespace Diagram {

namespace {

constexpr double kColGapMin = 46.0;    // minimum gap between lifeline centers
constexpr double kBoxGap = 24.0;       // extra gap across `box` boundaries
constexpr double kRowGap = 12.0;       // vertical air between event rows
constexpr double kActHalf = 5.0;       // activation bar half-width
constexpr double kSelfLoopW = 40.0;    // self-message loop width
constexpr double kFrameHeader = 22.0;  // fragment header band height
constexpr double kMargin = 16.0;

QColor parseCssColor(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty() || s.compare(QLatin1String("transparent"),
                                 Qt::CaseInsensitive) == 0)
        return QColor();
    if (s.startsWith(QLatin1String("rgb"), Qt::CaseInsensitive)) {
        const int open = s.indexOf(u'(');
        const int close = s.indexOf(u')');
        if (open < 0 || close <= open)
            return QColor();
        const QStringList parts =
            s.mid(open + 1, close - open - 1).split(u',', Qt::SkipEmptyParts);
        if (parts.size() < 3)
            return QColor();
        bool ok1 = false, ok2 = false, ok3 = false;
        const int r = parts.at(0).trimmed().toInt(&ok1);
        const int g = parts.at(1).trimmed().toInt(&ok2);
        const int b = parts.at(2).trimmed().toInt(&ok3);
        if (!ok1 || !ok2 || !ok3)
            return QColor();
        QColor c(r, g, b);
        if (parts.size() >= 4) {
            bool ok4 = false;
            const double a = parts.at(3).trimmed().toDouble(&ok4);
            if (ok4)
                c.setAlphaF(qBound(0.0, a, 1.0));
        }
        return c;
    }
    const QColor c(s);
    return c.isValid() ? c : QColor();
}

const char *blockName(SeqEvent::Block b)
{
    switch (b) {
    case SeqEvent::Loop: return "loop";
    case SeqEvent::Alt: return "alt";
    case SeqEvent::Opt: return "opt";
    case SeqEvent::Par: return "par";
    case SeqEvent::Critical: return "critical";
    case SeqEvent::Break: return "break";
    case SeqEvent::Rect: return "rect";
    }
    return "";
}

Marker markerForHead(SeqHead head)
{
    switch (head) {
    case SeqHead::Filled: return Marker::Arrow;
    case SeqHead::Open: return Marker::OpenArrow;
    case SeqHead::Cross: return Marker::Cross;
    case SeqHead::Point: return Marker::Dot;
    }
    return Marker::Arrow;
}

} // namespace

Scene layoutSequence(const SequenceAst &ast, const LayoutOptions &opts)
{
    Scene scene;
    scene.accTitle = !ast.accTitle.isEmpty() ? ast.accTitle : ast.title;
    scene.accDescr = ast.accDescr;

    const int P = ast.participants.size();
    const int M = ast.messageCount();
    scene.summary = QStringLiteral("Mermaid sequence diagram with %1 "
                                   "participant%2 and %3 message%4")
                        .arg(P).arg(P == 1 ? "" : "s")
                        .arg(M).arg(M == 1 ? "" : "s");
    if (P == 0)
        return scene;

    QFont font(opts.fontFamily);
    font.setPixelSize(opts.fontPixelSize);
    const QFontMetricsF fm(font);
    const double lineH = fm.height();

    auto textW = [&](const QString &s) {
        double w = 0;
        for (const QString &l : labelLines(s))
            w = qMax(w, fm.horizontalAdvance(l));
        return w;
    };
    auto textH = [&](const QString &s) {
        if (s.isEmpty())
            return 0.0;
        return labelLines(s).size() * lineH;
    };

    QHash<QString, int> idx;
    for (int i = 0; i < P; ++i)
        idx.insert(ast.participants.at(i).id, i);

    // ---- header boxes ----
    QList<double> headW(P), headH(P);
    for (int i = 0; i < P; ++i) {
        const SeqParticipant &p = ast.participants.at(i);
        if (p.actorFigure) {
            headW[i] = qMax(38.0, textW(p.label) + 4);
            headH[i] = 42 + textH(p.label) + 4;
        } else {
            headW[i] = qMax(60.0, textW(p.label) + 26);
            headH[i] = qMax(34.0, textH(p.label) + 18);
        }
    }
    double bandH = 0;
    for (int i = 0; i < P; ++i)
        bandH = qMax(bandH, headH[i]);

    // ---- column solve: adjacent minimums, then widen for spanning labels ----
    QList<double> gap(qMax(0, P - 1), 0.0);
    for (int g = 0; g + 1 < P; ++g) {
        gap[g] = headW[g] / 2 + headW[g + 1] / 2 + kColGapMin;
        const int b1 = ast.participants.at(g).boxIndex;
        const int b2 = ast.participants.at(g + 1).boxIndex;
        if (b1 != b2)
            gap[g] += kBoxGap;
    }
    double leftExtra = 0, rightExtra = 0;

    struct SpanReq { int a, b; double width; };
    QList<SpanReq> spans;
    for (const SeqEvent &e : ast.events) {
        if (e.kind == SeqEvent::Message) {
            const int i = idx.value(e.from, -1);
            const int j = idx.value(e.to, -1);
            if (i < 0 || j < 0)
                continue;
            if (i == j) {
                const double need = kSelfLoopW + textW(e.text) + 24;
                if (i + 1 < P)
                    spans.append({ i, i + 1, need + headW[i + 1] / 2 });
                else
                    rightExtra = qMax(rightExtra, need);
            } else {
                spans.append({ qMin(i, j), qMax(i, j), textW(e.text) + 34 });
            }
        } else if (e.kind == SeqEvent::Note) {
            const int i = idx.value(e.from, -1);
            if (i < 0)
                continue;
            const double w = qMin(textW(e.text), 260.0) + 24;
            switch (e.placement) {
            case SeqEvent::LeftOf:
                if (i > 0)
                    spans.append({ i - 1, i, w + 16 });
                else
                    leftExtra = qMax(leftExtra, w + 4);
                break;
            case SeqEvent::RightOf:
                if (i + 1 < P)
                    spans.append({ i, i + 1, w + 16 });
                else
                    rightExtra = qMax(rightExtra, w + 4);
                break;
            case SeqEvent::Over: {
                const int j = e.to.isEmpty() ? i : idx.value(e.to, i);
                if (j != i) {
                    spans.append({ qMin(i, j), qMax(i, j), w });
                } else {
                    if (i > 0)
                        spans.append({ i - 1, i, w / 2 + 8 });
                    else
                        leftExtra = qMax(leftExtra, w / 2);
                    if (i + 1 < P)
                        spans.append({ i, i + 1, w / 2 + 8 });
                    else
                        rightExtra = qMax(rightExtra, w / 2);
                }
                break;
            }
            }
        }
    }
    std::stable_sort(spans.begin(), spans.end(),
                     [](const SpanReq &x, const SpanReq &y) {
                         return (x.b - x.a) < (y.b - y.a);
                     });
    for (const SpanReq &s : spans) {
        if (s.b == s.a + 1) {
            gap[s.a] = qMax(gap[s.a], s.width);
        } else {
            double total = 0;
            for (int k = s.a; k < s.b; ++k)
                total += gap[k];
            if (total < s.width) {
                const double add = (s.width - total) / (s.b - s.a);
                for (int k = s.a; k < s.b; ++k)
                    gap[k] += add;
            }
        }
    }
    QList<double> cx(P);
    cx[0] = leftExtra + headW[0] / 2;
    for (int g = 0; g + 1 < P; ++g)
        cx[g + 1] = cx[g] + gap[g];

    // ---- vertical walk ----
    double y = 0;
    if (!ast.title.isEmpty()) {
        Text t;
        t.text = ast.title;
        t.role = Role::Label;
        t.fontSize = opts.fontPixelSize + 2;
        t.bold = true;
        const double w = textW(ast.title) + 20;
        t.rect = QRectF((cx.first() + cx.last()) / 2 - w / 2, y,
                        w, lineH + 4);
        scene.texts.append(t);
        y += lineH + 14;
    }

    bool anyBoxTitle = false;
    for (const SeqBox &b : ast.boxes)
        if (!b.title.isEmpty())
            anyBoxTitle = true;
    const double boxTop = y;
    if (!ast.boxes.isEmpty())
        y += (anyBoxTitle ? lineH + 8 : 6);

    const double headerTop = y;
    const double lifeTop = headerTop + bandH;
    double cursor = lifeTop + 14;

    // Activation stacks and finished bars.
    QList<QList<double>> act(P);
    struct ActBar { int p; double y0, y1; int level; };
    QList<ActBar> bars;
    auto barHalf = [&](int p) {
        return act[p].isEmpty() ? 0.0
                                : kActHalf + (act[p].size() - 1) * 3.0;
    };
    auto popActivation = [&](int p, double atY) {
        if (act[p].isEmpty())
            return;
        const double y0 = act[p].takeLast();
        bars.append({ p, y0, atY, int(act[p].size()) });
    };

    // Fragment frames.
    struct Frame {
        SeqEvent::Block block = SeqEvent::Loop;
        QString label;
        double startY = 0;
        double endY = 0;
        int depth = 0;
        double minX = 1e18, maxX = -1e18;
        QList<QPair<double, QString>> dividers;
    };
    QList<Frame> stack;
    QList<Frame> closed;
    auto touchX = [&](double x0, double x1) {
        for (Frame &f : stack) {
            f.minX = qMin(f.minX, x0);
            f.maxX = qMax(f.maxX, x1);
        }
    };

    struct Msg {
        int i, j;
        double xFrom, xTo, yArrow;
        QString label;
        double labelY = 0;
        SeqLine line;
        SeqHead head;
        bool bidir;
        bool self;
        int eventIndex = -1;   // §20.1 selection identity
        int srcStart = -1;     // §20.5 linking span
        int srcLen = 0;
    };
    QList<Msg> msgs;
    struct NoteBox { QRectF rect; QString text; };
    QList<NoteBox> notes;

    bool autoOn = false;
    int autoNum = 1, autoStep = 1;

    for (int evIndex = 0; evIndex < ast.events.size(); ++evIndex) {
        const SeqEvent &e = ast.events.at(evIndex);
        switch (e.kind) {
        case SeqEvent::Autonumber:
            autoOn = e.autonumberVisible;
            autoNum = e.autonumberStart;
            autoStep = e.autonumberStep;
            break;
        case SeqEvent::Activate: {
            const int p = idx.value(e.from, -1);
            if (p >= 0)
                act[p].append(cursor);
            break;
        }
        case SeqEvent::Deactivate: {
            const int p = idx.value(e.from, -1);
            if (p >= 0)
                popActivation(p, cursor);
            break;
        }
        case SeqEvent::BlockStart: {
            Frame f;
            f.block = e.block;
            f.label = e.blockLabel;
            f.startY = cursor;
            f.depth = stack.size();
            stack.append(f);
            cursor += (e.block == SeqEvent::Rect) ? 8 : kFrameHeader + 4;
            break;
        }
        case SeqEvent::BlockDivider: {
            if (!stack.isEmpty()) {
                stack.last().dividers.append({ cursor + 2, e.blockLabel });
                cursor += lineH + 12;
            }
            break;
        }
        case SeqEvent::BlockEnd: {
            if (stack.isEmpty())
                break;
            Frame f = stack.takeLast();
            f.endY = cursor + 4;
            cursor += 14;
            if (f.minX > f.maxX) {   // no events inside: span all lifelines
                f.minX = cx.first() - 20;
                f.maxX = cx.last() + 20;
            }
            if (!stack.isEmpty()) {
                stack.last().minX = qMin(stack.last().minX, f.minX);
                stack.last().maxX = qMax(stack.last().maxX, f.maxX);
            }
            closed.append(f);
            break;
        }
        case SeqEvent::Message: {
            const int i = idx.value(e.from, -1);
            const int j = idx.value(e.to, -1);
            if (i < 0 || j < 0)
                break;
            QString label = e.text;
            if (autoOn) {
                label = label.isEmpty()
                    ? QString::number(autoNum)
                    : QStringLiteral("%1. %2").arg(autoNum).arg(label);
            }
            autoNum += autoStep;
            const double th = textH(label);
            Msg m;
            m.i = i;
            m.j = j;
            m.line = e.line;
            m.head = e.head;
            m.bidir = e.bidirectional;
            m.label = label;
            m.self = (i == j);
            m.eventIndex = evIndex;
            m.srcStart = e.srcSpan.start;
            m.srcLen = e.srcSpan.length;
            if (m.self) {
                m.labelY = cursor;
                m.yArrow = cursor + qMax(th, 4.0) + 4;
                m.xFrom = cx[i] + barHalf(i);
                m.xTo = cx[i] + barHalf(i);
                touchX(cx[i] - 12,
                       cx[i] + kSelfLoopW + textW(label) + 20);
                cursor = m.yArrow + 20 + kRowGap;
            } else {
                m.labelY = cursor;
                m.yArrow = cursor + th + 5;
                const int dir = j > i ? 1 : -1;
                m.xFrom = cx[i] + dir * barHalf(i);
                touchX(qMin(cx[i], cx[j]) - 12, qMax(cx[i], cx[j]) + 12);
                cursor = m.yArrow + kRowGap;
            }
            // Activation shorthand: the bar starts / ends at the arrow.
            if (e.activateTarget && !m.self)
                act[j].append(m.yArrow);
            if (e.deactivateSource && !m.self)
                popActivation(i, m.yArrow);
            if (!m.self) {
                const int dir = j > i ? 1 : -1;
                m.xTo = cx[j] - dir * barHalf(j);
            }
            msgs.append(m);
            break;
        }
        case SeqEvent::Note: {
            const int i = idx.value(e.from, -1);
            if (i < 0)
                break;
            const double w = qMin(textW(e.text), 260.0) + 20;
            const double h = qMax(textH(e.text), lineH) + 12;
            QRectF r;
            switch (e.placement) {
            case SeqEvent::LeftOf:
                r = QRectF(cx[i] - 12 - w, cursor, w, h);
                break;
            case SeqEvent::RightOf:
                r = QRectF(cx[i] + 12, cursor, w, h);
                break;
            case SeqEvent::Over: {
                const int j = e.to.isEmpty() ? i : idx.value(e.to, i);
                const double mid = (cx[i] + cx[qMax(0, j)]) / 2;
                double left = qMin(cx[i], cx[qMax(0, j)]);
                double right = qMax(cx[i], cx[qMax(0, j)]);
                const double wOver = qMax(w, right - left + 28);
                r = QRectF(mid - wOver / 2, cursor, wOver, h);
                break;
            }
            }
            notes.append({ r, e.text });
            touchX(r.left() - 6, r.right() + 6);
            cursor += h + kRowGap;
            break;
        }
        }
    }

    // Close leftover activations and frames at the bottom.
    for (int p = 0; p < P; ++p)
        while (!act[p].isEmpty())
            popActivation(p, cursor);
    while (!stack.isEmpty()) {
        Frame f = stack.takeLast();
        f.endY = cursor + 4;
        if (f.minX > f.maxX) {
            f.minX = cx.first() - 20;
            f.maxX = cx.last() + 20;
        }
        closed.append(f);
        cursor += 10;
    }

    const double lifeBottom = cursor + 6;

    // ---- emit: boxes and rect-blocks (background groups) ----
    QList<QRectF> boxRects(ast.boxes.size());
    if (!ast.boxes.isEmpty()) {
        QList<double> bMin(ast.boxes.size(), 1e18);
        QList<double> bMax(ast.boxes.size(), -1e18);
        for (int i = 0; i < P; ++i) {
            const int b = ast.participants.at(i).boxIndex;
            if (b < 0)
                continue;
            bMin[b] = qMin(bMin[b], cx[i] - headW[i] / 2 - 10);
            bMax[b] = qMax(bMax[b], cx[i] + headW[i] / 2 + 10);
        }
        for (int b = 0; b < ast.boxes.size(); ++b) {
            if (bMin[b] > bMax[b])
                continue;
            Group g;
            g.rect = QRectF(bMin[b], boxTop,
                            bMax[b] - bMin[b],
                            lifeBottom + bandH + 6 - boxTop);
            boxRects[b] = g.rect;
            if (ast.boxes.at(b).color.isValid()) {
                QColor c = ast.boxes.at(b).color;
                c.setAlpha(70);
                g.fillOverride = c;
            }
            scene.groups.append(g);
        }
    }

    int maxDepth = 0;
    for (const Frame &f : closed)
        maxDepth = qMax(maxDepth, f.depth);
    for (const Frame &f : closed) {
        if (f.block != SeqEvent::Rect)
            continue;
        Group g;
        const double pad = 8 + (maxDepth - f.depth) * 6;
        g.rect = QRectF(f.minX - pad, f.startY, f.maxX - f.minX + 2 * pad,
                        f.endY - f.startY);
        const QColor c = parseCssColor(f.label);
        if (c.isValid())
            g.fillOverride = c;
        g.noBorder = true;
        scene.groups.append(g);
    }

    // ---- emit: lifelines ----
    for (int i = 0; i < P; ++i) {
        Path p;
        QPainterPath pp(QPointF(cx[i], lifeTop));
        pp.lineTo(cx[i], lifeBottom);
        p.path = pp;
        p.penStyle = Qt::DashLine;
        p.strokeWidth = 1.0;
        p.startPoint = QPointF(cx[i], lifeTop);
        p.endPoint = QPointF(cx[i], lifeBottom);
        scene.paths.append(p);
    }

    // ---- emit: activation bars ----
    for (const ActBar &b : bars) {
        Shape s;
        s.kind = Shape::Rect;
        const double xoff = b.level * 3.0;
        s.rect = QRectF(cx[b.p] - kActHalf + xoff, b.y0,
                        kActHalf * 2, qMax(6.0, b.y1 - b.y0));
        s.fillRole = Role::Activation;
        s.strokeRole = Role::NodeStroke;
        s.strokeWidth = 1.0;
        scene.shapes.append(s);
    }

    // ---- emit: messages ----
    for (const Msg &m : msgs) {
        Path p;
        p.edgeIndex = m.eventIndex;
        p.srcStart = m.srcStart;
        p.srcLen = m.srcLen;
        p.penStyle = m.line == SeqLine::Dotted ? Qt::DashLine : Qt::SolidLine;
        p.strokeWidth = 1.4;
        if (m.self) {
            const double x0 = m.xFrom;
            const double yTop = m.yArrow;
            const double yBot = m.yArrow + 16;
            QPainterPath pp(QPointF(x0, yTop));
            pp.lineTo(x0 + kSelfLoopW, yTop);
            pp.lineTo(x0 + kSelfLoopW, yBot);
            pp.lineTo(x0 + 2, yBot);
            p.path = pp;
            p.startPoint = QPointF(x0, yTop);
            p.endPoint = QPointF(x0 + 2, yBot);
            p.startDir = QPointF(1, 0);
            p.endDir = QPointF(-1, 0);
        } else {
            QPainterPath pp(QPointF(m.xFrom, m.yArrow));
            pp.lineTo(m.xTo, m.yArrow);
            p.path = pp;
            p.startPoint = QPointF(m.xFrom, m.yArrow);
            p.endPoint = QPointF(m.xTo, m.yArrow);
            const double dir = m.xTo >= m.xFrom ? 1.0 : -1.0;
            p.startDir = QPointF(-dir, 0);
            p.endDir = QPointF(dir, 0);
        }
        p.endMarker = markerForHead(m.head);
        p.startMarker = m.bidir ? p.endMarker : Marker::None;
        scene.paths.append(p);

        if (!m.label.isEmpty()) {
            Text t;
            t.text = m.label;
            t.role = Role::EdgeLabel;
            t.fontSize = qMax(10, opts.fontPixelSize - 1);
            const double w = textW(m.label) + 8;
            const double h = textH(m.label);
            if (m.self) {
                t.rect = QRectF(m.xFrom + kSelfLoopW + 8, m.labelY, w, h + 4);
                t.align = Qt::AlignLeft | Qt::AlignVCenter;
            } else {
                t.rect = QRectF((m.xFrom + m.xTo) / 2 - w / 2, m.labelY,
                                w, h + 2);
            }
            scene.texts.append(t);
        }
    }

    // ---- emit: notes ----
    for (const NoteBox &n : notes) {
        Shape s;
        s.kind = Shape::Rect;
        s.rect = n.rect;
        s.fillRole = Role::NoteFill;
        s.strokeRole = Role::NoteStroke;
        s.strokeWidth = 1.0;
        scene.shapes.append(s);
        Text t;
        t.text = n.text;
        t.role = Role::Label;
        t.fontSize = qMax(10, opts.fontPixelSize - 1);
        t.rect = n.rect;
        scene.texts.append(t);
    }

    // ---- emit: fragment frames ----
    for (const Frame &f : closed) {
        if (f.block == SeqEvent::Rect)
            continue;
        const double pad = 10 + (maxDepth - f.depth) * 6;
        const QRectF r(f.minX - pad, f.startY, f.maxX - f.minX + 2 * pad,
                       f.endY - f.startY);
        Path border;
        QPainterPath pp;
        pp.addRect(r);
        border.path = pp;
        border.strokeRole = Role::SubgraphStroke;
        border.strokeWidth = 1.2;
        scene.paths.append(border);

        // The kind chip at the frame's top-left.
        const QString kindText = QLatin1String(blockName(f.block));
        const double chipW = fm.horizontalAdvance(kindText) + 14;
        Shape chip;
        chip.kind = Shape::Rect;
        chip.rect = QRectF(r.left(), r.top(), chipW, kFrameHeader - 4);
        chip.fillRole = Role::SubgraphFill;
        chip.strokeRole = Role::SubgraphStroke;
        chip.strokeWidth = 1.0;
        scene.shapes.append(chip);
        Text chipText;
        chipText.text = kindText;
        chipText.role = Role::Label;
        chipText.fontSize = qMax(9, opts.fontPixelSize - 3);
        chipText.bold = true;
        chipText.rect = chip.rect;
        scene.texts.append(chipText);

        if (!f.label.isEmpty()) {
            Text cond;
            cond.text = QStringLiteral("[%1]").arg(f.label);
            cond.role = Role::EdgeLabel;
            cond.italic = true;
            cond.fontSize = qMax(9, opts.fontPixelSize - 2);
            cond.align = Qt::AlignLeft | Qt::AlignVCenter;
            cond.rect = QRectF(r.left() + chipW + 8, r.top(),
                               qMax(10.0, r.width() - chipW - 12),
                               kFrameHeader - 4);
            scene.texts.append(cond);
        }
        for (const auto &d : f.dividers) {
            Path div;
            QPainterPath dp(QPointF(r.left(), d.first));
            dp.lineTo(r.right(), d.first);
            div.path = dp;
            div.penStyle = Qt::DashLine;
            div.strokeRole = Role::SubgraphStroke;
            div.strokeWidth = 1.0;
            scene.paths.append(div);
            if (!d.second.isEmpty()) {
                Text t;
                t.text = QStringLiteral("[%1]").arg(d.second);
                t.role = Role::EdgeLabel;
                t.italic = true;
                t.fontSize = qMax(9, opts.fontPixelSize - 2);
                t.align = Qt::AlignLeft | Qt::AlignVCenter;
                t.rect = QRectF(r.left() + 8, d.first + 2,
                                qMax(10.0, r.width() - 16), lineH);
                scene.texts.append(t);
            }
        }
    }

    // ---- emit: participant headers (top and mirrored bottom) ----
    auto emitHeader = [&](int i, double top) {
        const SeqParticipant &p = ast.participants.at(i);
        if (p.actorFigure) {
            Shape s;
            s.kind = Shape::Actor;
            s.rect = QRectF(cx[i] - 17, top, 34, 40);
            s.nodeId = p.id;
            s.srcStart = p.srcSpan.start;
            s.srcLen = p.srcSpan.length;
            scene.shapes.append(s);
            Text t;
            t.text = p.label;
            t.role = Role::Label;
            t.fontSize = opts.fontPixelSize;
            t.rect = QRectF(cx[i] - headW[i] / 2, top + 42,
                            headW[i], textH(p.label) + 2);
            scene.texts.append(t);
        } else {
            Shape s;
            s.kind = Shape::Rect;
            s.rect = QRectF(cx[i] - headW[i] / 2, top, headW[i], headH[i]);
            s.nodeId = p.id;
            s.srcStart = p.srcSpan.start;
            s.srcLen = p.srcSpan.length;
            scene.shapes.append(s);
            Text t;
            t.text = p.label;
            t.role = Role::Label;
            t.fontSize = opts.fontPixelSize;
            t.rect = s.rect;
            scene.texts.append(t);
        }
    };
    for (int i = 0; i < P; ++i)
        emitHeader(i, lifeTop - headH[i]);
    for (int i = 0; i < P; ++i)
        emitHeader(i, lifeBottom);

    // Box titles above the header band.
    for (int b = 0; b < ast.boxes.size(); ++b) {
        if (ast.boxes.at(b).title.isEmpty() || boxRects.at(b).isNull())
            continue;
        const QRectF r = boxRects.at(b);
        Text t;
        t.text = ast.boxes.at(b).title;
        t.role = Role::Label;
        t.bold = true;
        t.fontSize = qMax(10, opts.fontPixelSize - 1);
        t.rect = QRectF(r.left(), r.top() + 2, r.width(), lineH);
        scene.texts.append(t);
    }

    if (rightExtra > 0) {
        // Reserve right-side room for trailing self-loops / notes.
        Text spacer;
        spacer.text = QString();
        spacer.rect = QRectF(cx.last() + rightExtra, lifeTop, 1, 1);
        spacer.role = Role::Label;
        scene.texts.append(spacer);
    }

    finalizeSceneBounds(scene, kMargin);
    return scene;
}

} // namespace Diagram
