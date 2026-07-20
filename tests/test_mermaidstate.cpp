// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "diagrams/diagramlayout.h"
#include "diagrams/mermaidparser.h"
#include "diagrams/mermaidrenderer.h"

using namespace Mermaid;
using namespace Diagram;

// State-diagram parser and layout, built grammar-first against the pinned
// mermaid@11.16.0 stateDiagram.jison. Some fixtures follow that repository's
// demos/state.html (MIT license, (c) Knut Sveidqvist).
class TestMermaidState : public QObject
{
    Q_OBJECT

    static ParseResult parse(const QString &src)
    {
        MermaidParser p;
        return p.parse(src);
    }
    static LayoutOptions opts()
    {
        LayoutOptions o;
        o.fontFamily = QStringLiteral("sans-serif");
        o.fontPixelSize = 14;
        return o;
    }
    static const StateNode *state(const StateAst &a, const QString &id)
    {
        const int i = a.indexOfState(id);
        return i >= 0 ? &a.states.at(i) : nullptr;
    }

private slots:
    void familyIsSupported()
    {
        for (const char *header : { "stateDiagram", "stateDiagram-v2" }) {
            const ParseResult r = parse(
                QStringLiteral("%1\n  [*] --> Still\n  Still --> [*]")
                    .arg(QLatin1String(header)));
            QCOMPARE(r.type, DiagramType::State);
            QVERIFY(r.supported);
            QVERIFY(!r.hasErrors());
        }
    }

    void startAndEndPseudoStates()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  [*] --> Still\n"
            "  Still --> Moving\n"
            "  Moving --> [*]\n"));
        QVERIFY(!r.hasErrors());
        const StateAst &a = r.stateDiagram;
        int starts = 0, ends = 0;
        for (const StateNode &s : a.states) {
            if (s.kind == StateKind::Start) ++starts;
            if (s.kind == StateKind::End) ++ends;
        }
        QCOMPARE(starts, 1);
        QCOMPARE(ends, 1);
        QCOMPARE(a.transitions.size(), 3);
    }

    void transitionLabels()
    {
        const ParseResult r = parse(
            "stateDiagram-v2\n  Still --> Moving : start moving");
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.stateDiagram.transitions.size(), 1);
        QCOMPARE(r.stateDiagram.transitions.first().label,
                 QString("start moving"));
    }

    void longDescriptionsAndColonText()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  state \"This is a state description\" as s2\n"
            "  s2 : more detail\n"
            "  s2 : and another line\n"));
        QVERIFY(!r.hasErrors());
        const StateNode *s = state(r.stateDiagram, "s2");
        QVERIFY(s);
        QCOMPARE(s->label, QString("This is a state description"));
        QCOMPARE(s->descriptions.size(), 2);
    }

    void compositeStatesScopeStartAndMembers()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  [*] --> First\n"
            "  state First {\n"
            "    [*] --> second\n"
            "    second --> [*]\n"
            "  }\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        const StateAst &a = r.stateDiagram;
        const StateNode *first = state(a, "First");
        QVERIFY(first);
        QVERIFY(first->composite);
        const StateNode *second = state(a, "second");
        QVERIFY(second);
        QCOMPARE(second->parentIndex, a.indexOfState("First"));
        // The [*] inside the composite is distinct from the root one.
        int starts = 0;
        for (const StateNode &s : a.states)
            if (s.kind == StateKind::Start)
                ++starts;
        QCOMPARE(starts, 2);
    }

    void forkJoinChoice()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  state fork1 <<fork>>\n"
            "  state join1 <<join>>\n"
            "  state pick <<choice>>\n"
            "  [*] --> fork1\n"
            "  fork1 --> A\n"
            "  fork1 --> B\n"
            "  A --> join1\n"
            "  B --> join1\n"
            "  join1 --> pick\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(state(r.stateDiagram, "fork1")->kind, StateKind::Fork);
        QCOMPARE(state(r.stateDiagram, "join1")->kind, StateKind::Join);
        QCOMPARE(state(r.stateDiagram, "pick")->kind, StateKind::Choice);
    }

    void notesSingleAndMultiline()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  Active --> Idle\n"
            "  note right of Active : quick note\n"
            "  note left of Idle\n"
            "    a longer note\n"
            "    across lines\n"
            "  end note\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QCOMPARE(r.stateDiagram.notes.size(), 2);
        QCOMPARE(r.stateDiagram.notes.at(0).stateId, QString("Active"));
        QVERIFY(!r.stateDiagram.notes.at(0).leftOf);
        QVERIFY(r.stateDiagram.notes.at(1).leftOf);
        QVERIFY(r.stateDiagram.notes.at(1).text.contains("across lines"));
    }

    void directionAndStyling()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  direction LR\n"
            "  classDef badBadEvent fill:#f00\n"
            "  A --> B:::badBadEvent\n"
            "  class A badBadEvent\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.stateDiagram.direction, Direction::LR);
        QVERIFY(r.stateDiagram.classDefs.contains("badBadEvent"));
        QVERIFY(state(r.stateDiagram, "B")->cssClasses.contains("badBadEvent"));
        QVERIFY(state(r.stateDiagram, "A")->cssClasses.contains("badBadEvent"));
    }

    void restrictedStatementsWarnNotFail()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  A --> B\n"
            "  scale 350 width\n"
            "  click A href \"https://example.com\"\n"
            "  state C {\n"
            "    D --> E\n"
            "    --\n"
            "    F --> G\n"
            "  }\n"
            "  hide empty description\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        int warnings = 0;
        for (const Diagnostic &d : r.diagnostics)
            if (d.severity == Diagnostic::Warning)
                ++warnings;
        QVERIFY(warnings >= 3);   // scale, click, concurrency divider
    }

    void missingBraceIsAnError()
    {
        const ParseResult r = parse(
            "stateDiagram-v2\n  state A {\n  B --> C");
        QVERIFY(r.hasErrors());
        QVERIFY(r.firstError().message.contains("}"));
    }

    // ---- layout ----

    void layoutStartEndAndTransition()
    {
        const ParseResult r = parse(
            "stateDiagram-v2\n  [*] --> Still\n  Still --> [*]");
        const Scene s = layoutStateDiagram(r.stateDiagram, opts());
        QVERIFY(s.bounds.width() > 0);
        // Start disc + end double circle (two shapes) + one state box.
        QVERIFY(s.shapes.size() >= 4);
        int arrows = 0;
        for (const Path &p : s.paths)
            if (p.endMarker == Marker::Arrow)
                ++arrows;
        QCOMPARE(arrows, 2);
        QVERIFY(s.summary.contains("1 state"));
    }

    void layoutCompositeGroupContainsMembers()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  [*] --> First\n"
            "  state First {\n"
            "    [*] --> second\n"
            "  }\n"));
        const Scene s = layoutStateDiagram(r.stateDiagram, opts());
        QCOMPARE(s.groups.size(), 1);
        QCOMPARE(s.groups.first().title, QString("First"));
        // The member state box lies inside the composite group's rect.
        bool memberInside = false;
        for (const Shape &sh : s.shapes)
            if (sh.nodeId == QLatin1String("second")
                && s.groups.first().rect.contains(sh.rect.center()))
                memberInside = true;
        QVERIFY(memberInside);
    }

    void layoutNoteAndTether()
    {
        const ParseResult r = parse(QStringLiteral(
            "stateDiagram-v2\n"
            "  Active --> Idle\n"
            "  note right of Active : remember me\n"));
        const Scene s = layoutStateDiagram(r.stateDiagram, opts());
        bool note = false, tether = false;
        for (const Shape &sh : s.shapes)
            if (sh.fillRole == Role::NoteFill)
                note = true;
        for (const Path &p : s.paths)
            if (p.penStyle == Qt::DashLine
                && p.strokeRole == Role::NoteStroke)
                tether = true;
        QVERIFY(note);
        QVERIFY(tether);
    }

    void layoutDeterministic()
    {
        const QString src = QStringLiteral(
            "stateDiagram-v2\n"
            "  [*] --> A\n  A --> B : go\n  B --> C\n  C --> A : loop\n");
        const Scene s1 = layoutStateDiagram(parse(src).stateDiagram, opts());
        const Scene s2 = layoutStateDiagram(parse(src).stateDiagram, opts());
        QCOMPARE(s1.shapes.size(), s2.shapes.size());
        for (int i = 0; i < s1.shapes.size(); ++i) {
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.x() + 1,
                                  s2.shapes.at(i).rect.x() + 1));
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.y() + 1,
                                  s2.shapes.at(i).rect.y() + 1));
        }
    }

    void rendererRendersStateDiagram()
    {
        clearCache();
        const RenderResult r = render(
            QStringLiteral("stateDiagram-v2\n  [*] --> Working"), opts());
        QVERIFY(r.valid);
        QVERIFY(!r.unsupportedFamily);
        QVERIFY(!r.hasError);
    }
};

QTEST_MAIN(TestMermaidState)
#include "test_mermaidstate.moc"
