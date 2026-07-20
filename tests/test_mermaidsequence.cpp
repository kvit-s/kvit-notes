// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "diagrams/diagramlayout.h"
#include "diagrams/mermaidparser.h"
#include "diagrams/mermaidrenderer.h"

using namespace Mermaid;
using namespace Diagram;

// Sequence-diagram parser and layout, built grammar-first against the pinned
// mermaid@11.16.0 sequenceDiagram.jison. Several fixtures are lifted from that
// repository's demos/sequence.html (MIT license, (c) Knut Sveidqvist).
class TestMermaidSequence : public QObject
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
    static bool hasWarning(const ParseResult &r, const char *needle)
    {
        for (const Diagnostic &d : r.diagnostics)
            if (d.severity == Diagnostic::Warning
                && d.message.contains(QLatin1String(needle)))
                return true;
        return false;
    }
    static const SeqEvent *firstMessage(const ParseResult &r)
    {
        for (const SeqEvent &e : r.sequence.events)
            if (e.kind == SeqEvent::Message)
                return &e;
        return nullptr;
    }

private slots:
    void familyIsSupported()
    {
        const ParseResult r = parse("sequenceDiagram\n  Alice->>Bob: Hi");
        QCOMPARE(r.type, DiagramType::Sequence);
        QVERIFY(r.supported);
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.sequence.participants.size(), 2);
        QCOMPARE(r.sequence.messageCount(), 1);
    }

    void headerIsCaseInsensitive()
    {
        const ParseResult r = parse("sequencediagram\n  A->>B: x");
        QCOMPARE(r.type, DiagramType::Sequence);
        QVERIFY(r.supported);
    }

    void participantsAliasesAndActors()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  participant A as Alice the First\n"
            "  actor B as Bob\n"
            "  participant 3\n"
            "  A->>B: hi\n"
            "  3->>A: yo\n"));
        QVERIFY(!r.hasErrors());
        const SequenceAst &a = r.sequence;
        QCOMPARE(a.participants.size(), 3);
        QCOMPARE(a.participants.at(0).id, QString("A"));
        QCOMPARE(a.participants.at(0).label, QString("Alice the First"));
        QVERIFY(!a.participants.at(0).actorFigure);
        QVERIFY(a.participants.at(1).actorFigure);
        QCOMPARE(a.participants.at(1).label, QString("Bob"));
        QCOMPARE(a.participants.at(2).id, QString("3"));
    }

    void autoDeclaredParticipantsKeepFirstUseOrder()
    {
        const ParseResult r = parse(
            "sequenceDiagram\n  Zed->>Amy: one\n  Amy->>Bob: two");
        const SequenceAst &a = r.sequence;
        QCOMPARE(a.participants.size(), 3);
        QCOMPARE(a.participants.at(0).id, QString("Zed"));
        QCOMPARE(a.participants.at(1).id, QString("Amy"));
        QCOMPARE(a.participants.at(2).id, QString("Bob"));
    }

    void arrowFormsMapToLineAndHead()
    {
        struct Case { const char *arrow; SeqLine line; SeqHead head; bool bidir; };
        const Case cases[] = {
            { "->", SeqLine::Solid, SeqHead::Open, false },
            { "-->", SeqLine::Dotted, SeqHead::Open, false },
            { "->>", SeqLine::Solid, SeqHead::Filled, false },
            { "-->>", SeqLine::Dotted, SeqHead::Filled, false },
            { "<<->>", SeqLine::Solid, SeqHead::Filled, true },
            { "<<-->>", SeqLine::Dotted, SeqHead::Filled, true },
            { "-x", SeqLine::Solid, SeqHead::Cross, false },
            { "--x", SeqLine::Dotted, SeqHead::Cross, false },
            { "-)", SeqLine::Solid, SeqHead::Point, false },
            { "--)", SeqLine::Dotted, SeqHead::Point, false },
        };
        for (const Case &c : cases) {
            const ParseResult r = parse(
                QStringLiteral("sequenceDiagram\n  A %1 B: msg")
                    .arg(QLatin1String(c.arrow)));
            QVERIFY2(!r.hasErrors(), c.arrow);
            const SeqEvent *m = firstMessage(r);
            QVERIFY2(m, c.arrow);
            QCOMPARE(m->line, c.line);
            QCOMPARE(m->head, c.head);
            QCOMPARE(m->bidirectional, c.bidir);
            QCOMPARE(m->text, QString("msg"));
        }
    }

    void exoticPinnedArrowsParseWithoutError()
    {
        // The pinned grammar's half-head and reverse forms must not become
        // unrecognized-token failures (§9 grammar alignment).
        for (const char *arrow : { "-|\\", "-|/", "-\\\\", "-//",
                                   "--|\\", "--|/", "--\\\\", "--//" }) {
            const ParseResult r = parse(
                QStringLiteral("sequenceDiagram\n  A %1 B: msg")
                    .arg(QLatin1String(arrow)));
            QVERIFY2(!r.hasErrors(), arrow);
            QVERIFY2(firstMessage(r), arrow);
        }
    }

    void activationShorthandAndStatements()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  Alice->>+John: up\n"
            "  John-->>-Alice: down\n"
            "  activate Alice\n"
            "  deactivate Alice\n"));
        QVERIFY(!r.hasErrors());
        const SequenceAst &a = r.sequence;
        QCOMPARE(a.events.size(), 4);
        QVERIFY(a.events.at(0).activateTarget);
        QVERIFY(a.events.at(1).deactivateSource);
        QCOMPARE(a.events.at(2).kind, SeqEvent::Activate);
        QCOMPARE(a.events.at(3).kind, SeqEvent::Deactivate);
    }

    void notesAllPlacements()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  participant A\n"
            "  participant B\n"
            "  Note left of A: to the left\n"
            "  note right of B: to the right\n"
            "  NOTE over A: on top\n"
            "  Note over A,B: spanning\n"));
        QVERIFY(!r.hasErrors());
        int notes = 0;
        for (const SeqEvent &e : r.sequence.events) {
            if (e.kind != SeqEvent::Note)
                continue;
            ++notes;
            if (e.text == QLatin1String("spanning")) {
                QCOMPARE(e.placement, SeqEvent::Over);
                QCOMPARE(e.from, QString("A"));
                QCOMPARE(e.to, QString("B"));
            }
        }
        QCOMPARE(notes, 4);
    }

    void blocksAndDividers()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  loop every minute\n"
            "    A->>B: tick\n"
            "  end\n"
            "  alt success\n"
            "    A->>B: ok\n"
            "  else failure\n"
            "    A->>B: err\n"
            "  end\n"
            "  opt maybe\n"
            "    A->>B: hm\n"
            "  end\n"
            "  par first\n"
            "    A->>B: p1\n"
            "  and second\n"
            "    A->>B: p2\n"
            "  end\n"
            "  critical mount\n"
            "    A->>B: c\n"
            "  option timeout\n"
            "    A->>B: t\n"
            "  end\n"
            "  break when boom\n"
            "    A->>B: boom\n"
            "  end\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        int starts = 0, dividers = 0, ends = 0;
        for (const SeqEvent &e : r.sequence.events) {
            if (e.kind == SeqEvent::BlockStart) ++starts;
            if (e.kind == SeqEvent::BlockDivider) ++dividers;
            if (e.kind == SeqEvent::BlockEnd) ++ends;
        }
        QCOMPARE(starts, 6);
        QCOMPARE(dividers, 3);
        QCOMPARE(ends, 6);
    }

    void dividerOutsideItsBlockIsAnError()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  loop x\n"
            "    else nope\n"
            "  end\n"));
        QVERIFY(r.hasErrors());
        QVERIFY(r.firstError().message.contains("else"));
    }

    void missingEndIsAnError()
    {
        const ParseResult r = parse("sequenceDiagram\n  loop forever\n  A->>B: x");
        QVERIFY(r.hasErrors());
        QVERIFY(r.firstError().message.contains("end"));
    }

    void boxesGroupParticipants()
    {
        // Lifted from demos/sequence.html (mermaid, MIT).
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  box lightgreen Alice & John\n"
            "  participant A\n"
            "  participant J\n"
            "  end\n"
            "  box Another Group\n"
            "  participant B\n"
            "  end\n"
            "  A->>J: Hello John, how are you?\n"
            "  J->>A: Great!\n"
            "  A->>B: Hello Bob, how are you ?\n"));
        QVERIFY(!r.hasErrors());
        const SequenceAst &a = r.sequence;
        QCOMPARE(a.boxes.size(), 2);
        QCOMPARE(a.boxes.at(0).title, QString("Alice & John"));
        QVERIFY(a.boxes.at(0).color.isValid());
        QVERIFY(!a.boxes.at(1).color.isValid());
        QCOMPARE(a.participants.at(0).boxIndex, 0);
        QCOMPARE(a.participants.at(1).boxIndex, 0);
        QCOMPARE(a.participants.at(2).boxIndex, 1);
    }

    void autonumberVariants()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  autonumber\n"
            "  A->>B: one\n"
            "  autonumber 50 10\n"
            "  A->>B: two\n"
            "  autonumber off\n"
            "  A->>B: three\n"));
        QVERIFY(!r.hasErrors());
        QList<SeqEvent> autos;
        for (const SeqEvent &e : r.sequence.events)
            if (e.kind == SeqEvent::Autonumber)
                autos.append(e);
        QCOMPARE(autos.size(), 3);
        QVERIFY(autos.at(0).autonumberVisible);
        QCOMPARE(autos.at(1).autonumberStart, 50);
        QCOMPARE(autos.at(1).autonumberStep, 10);
        QVERIFY(!autos.at(2).autonumberVisible);
    }

    void restrictedStatementsWarnNotFail()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  participant Alice\n"
            "  link Alice: Dashboard @ https://dashboard.contoso.com/alice\n"
            "  links Alice: {\"Repo\": \"https://x\"}\n"
            "  create participant D\n"
            "  destroy D\n"
            "  participant C@{ \"type\": \"database\" }\n"
            "  Alice ->> () C: central\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QVERIFY(hasWarning(r, "link"));
        QVERIFY(hasWarning(r, "create"));
        QVERIFY(hasWarning(r, "destroy"));
        QVERIFY(hasWarning(r, "configuration"));
        QVERIFY(hasWarning(r, "Central connections"));
    }

    void entityEscapesDecode()
    {
        const ParseResult r = parse(
            "sequenceDiagram\n  A->>B: 1 #lt; 2 and #35; is a hash");
        const SeqEvent *m = firstMessage(r);
        QVERIFY(m);
        QCOMPARE(m->text, QString("1 < 2 and # is a hash"));
    }

    void titleAndAccessibility()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  title My interaction\n"
            "  accTitle: acc title\n"
            "  accDescr: acc description\n"
            "  A->>B: x\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.sequence.title, QString("My interaction"));
        QCOMPARE(r.sequence.accTitle, QString("acc title"));
        QCOMPARE(r.sequence.accDescr, QString("acc description"));
    }

    void commentsIgnored()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  %% a comment line\n"
            "  # a hash comment line\n"
            "  A->>B: hello %% trailing comment\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.sequence.messageCount(), 1);
        QCOMPARE(firstMessage(r)->text, QString("hello"));
    }

    void messageWithoutColonIsAnError()
    {
        const ParseResult r = parse("sequenceDiagram\n  A->>B no colon");
        QVERIFY(r.hasErrors());
        QVERIFY(r.firstError().message.contains(":"));
    }

    void demoCorpusParsesClean()
    {
        // Condensed from demos/sequence.html of the pinned version (MIT).
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  autonumber\n"
            "  Alice->>John: Hello John,<br>how are you?\n"
            "  autonumber 50 10\n"
            "  Alice->>John: John,<br />can you hear me?\n"
            "  John-->>Alice: Hi Alice,<br />I can hear you!\n"
            "  autonumber off\n"
            "  John-->>Alice: I feel great!\n"
            "  Alice-)John: See you later!\n"
            "  Alice<<->>John: We said that together!\n"
            "  Alice-xJohn: Bye\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QCOMPARE(r.sequence.messageCount(), 7);
    }

    // ---- layout ----

    void layoutProducesLifelinesHeadersAndMessage()
    {
        const ParseResult r = parse(
            "sequenceDiagram\n  Alice->>Bob: Hello Bob");
        const Scene s = layoutSequence(r.sequence, opts());
        QVERIFY(s.bounds.width() > 0 && s.bounds.height() > 0);
        // Two lifelines + one message path.
        QCOMPARE(s.paths.size(), 3);
        // Header boxes top and mirrored bottom.
        int headers = 0;
        for (const Shape &sh : s.shapes)
            if (!sh.nodeId.isEmpty())
                ++headers;
        QCOMPARE(headers, 4);
        QVERIFY(s.summary.contains("2 participants"));
        QVERIFY(s.summary.contains("1 message"));
    }

    void layoutMessageArrowSpansLifelines()
    {
        const ParseResult r = parse(
            "sequenceDiagram\n  A->>B: go\n  B-->>A: back");
        const Scene s = layoutSequence(r.sequence, opts());
        // Message paths are the non-dashed horizontal ones with markers.
        QList<Path> messages;
        for (const Path &p : s.paths)
            if (p.endMarker != Marker::None)
                messages.append(p);
        QCOMPARE(messages.size(), 2);
        QVERIFY(messages.at(0).endPoint.x() > messages.at(0).startPoint.x());
        QVERIFY(messages.at(1).endPoint.x() < messages.at(1).startPoint.x());
        QCOMPARE(messages.at(1).penStyle, Qt::DashLine);
    }

    void layoutActivationBarsAndNotes()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  Alice->>+John: up\n"
            "  Note right of John: thinking\n"
            "  John-->>-Alice: down\n"));
        const Scene s = layoutSequence(r.sequence, opts());
        bool activation = false, note = false;
        for (const Shape &sh : s.shapes) {
            if (sh.fillRole == Role::Activation)
                activation = true;
            if (sh.fillRole == Role::NoteFill)
                note = true;
        }
        QVERIFY(activation);
        QVERIFY(note);
    }

    void layoutSelfMessageLoops()
    {
        const ParseResult r = parse("sequenceDiagram\n  A->>A: think");
        const Scene s = layoutSequence(r.sequence, opts());
        bool loop = false;
        for (const Path &p : s.paths) {
            if (p.endMarker == Marker::None)
                continue;
            if (qAbs(p.endPoint.y() - p.startPoint.y()) > 4)
                loop = true;   // the self-message returns on a lower row
        }
        QVERIFY(loop);
    }

    void layoutFramesForBlocks()
    {
        const ParseResult r = parse(QStringLiteral(
            "sequenceDiagram\n"
            "  alt happy\n"
            "    A->>B: yes\n"
            "  else sad\n"
            "    A->>B: no\n"
            "  end\n"));
        const Scene s = layoutSequence(r.sequence, opts());
        bool chip = false, condition = false, divider = false;
        for (const Text &t : s.texts) {
            if (t.text == QLatin1String("alt"))
                chip = true;
            if (t.text == QLatin1String("[happy]"))
                condition = true;
            if (t.text == QLatin1String("[sad]"))
                divider = true;
        }
        QVERIFY(chip);
        QVERIFY(condition);
        QVERIFY(divider);
    }

    void layoutAutonumberPrefixesLabels()
    {
        const ParseResult r = parse(
            "sequenceDiagram\n  autonumber\n  A->>B: first\n  B->>A: second");
        const Scene s = layoutSequence(r.sequence, opts());
        bool first = false, second = false;
        for (const Text &t : s.texts) {
            if (t.text == QLatin1String("1. first"))
                first = true;
            if (t.text == QLatin1String("2. second"))
                second = true;
        }
        QVERIFY(first);
        QVERIFY(second);
    }

    void layoutDeterministic()
    {
        const QString src = QStringLiteral(
            "sequenceDiagram\n"
            "  participant A\n  participant B\n  participant C\n"
            "  A->>B: one\n  B->>C: two\n  C-->>A: three\n"
            "  Note over A,C: wide note\n");
        const Scene s1 = layoutSequence(parse(src).sequence, opts());
        const Scene s2 = layoutSequence(parse(src).sequence, opts());
        QCOMPARE(s1.shapes.size(), s2.shapes.size());
        QCOMPARE(s1.paths.size(), s2.paths.size());
        for (int i = 0; i < s1.shapes.size(); ++i) {
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.x() + 1,
                                  s2.shapes.at(i).rect.x() + 1));
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.y() + 1,
                                  s2.shapes.at(i).rect.y() + 1));
        }
    }

    void layoutWideLabelExpandsColumns()
    {
        const Scene narrow = layoutSequence(
            parse("sequenceDiagram\n  A->>B: hi").sequence, opts());
        const Scene wide = layoutSequence(
            parse("sequenceDiagram\n  A->>B: a very very very long message "
                  "label that needs room").sequence, opts());
        QVERIFY(wide.bounds.width() > narrow.bounds.width() + 50);
    }

    void rendererRendersSequence()
    {
        clearCache();
        const RenderResult r = render(
            QStringLiteral("sequenceDiagram\n  Alice->>Bob: Hi"), opts());
        QVERIFY(r.valid);
        QVERIFY(!r.unsupportedFamily);
        QVERIFY(!r.hasError);
        QVERIFY(!r.scene.isEmpty());
    }
};

QTEST_MAIN(TestMermaidSequence)
#include "test_mermaidsequence.moc"
