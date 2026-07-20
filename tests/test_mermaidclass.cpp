// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "diagrams/diagramlayout.h"
#include "diagrams/mermaidparser.h"
#include "diagrams/mermaidrenderer.h"

using namespace Mermaid;
using namespace Diagram;

// Class-diagram parser and layout (diagrams-prd.md §9, §15), built
// grammar-first against the pinned mermaid@11.16.0 classDiagram.jison. Some
// fixtures are lifted from that repository's demos/classchart.html (MIT
// license, (c) Knut Sveidqvist).
class TestMermaidClass : public QObject
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
    static const ClassNode *node(const ClassAst &a, const QString &id)
    {
        const int i = a.indexOfClass(id);
        return i >= 0 ? &a.classes.at(i) : nullptr;
    }
    static const ClassRelation *relation(const ClassAst &a, const QString &from,
                                         const QString &to)
    {
        for (const ClassRelation &r : a.relations)
            if (r.from == from && r.to == to)
                return &r;
        return nullptr;
    }

private slots:
    void familyIsSupported()
    {
        for (const char *header : { "classDiagram", "classDiagram-v2" }) {
            const ParseResult r = parse(
                QStringLiteral("%1\n  Animal <|-- Duck")
                    .arg(QLatin1String(header)));
            QCOMPARE(r.type, DiagramType::Class);
            QVERIFY(r.supported);
            QVERIFY(!r.hasErrors());
            QCOMPARE(r.classDiagram.classes.size(), 2);
            QCOMPARE(r.classDiagram.relations.size(), 1);
        }
    }

    void relationEndsAndLines()
    {
        struct Case {
            const char *op;
            ClassRelEnd fromEnd;
            ClassRelEnd toEnd;
            bool dotted;
        };
        const Case cases[] = {
            { "<|--", ClassRelEnd::Extension, ClassRelEnd::None, false },
            { "--|>", ClassRelEnd::None, ClassRelEnd::Extension, false },
            { "<|..", ClassRelEnd::Extension, ClassRelEnd::None, true },
            { "..|>", ClassRelEnd::None, ClassRelEnd::Extension, true },
            { "*--", ClassRelEnd::Composition, ClassRelEnd::None, false },
            { "--*", ClassRelEnd::None, ClassRelEnd::Composition, false },
            { "o--", ClassRelEnd::Aggregation, ClassRelEnd::None, false },
            { "--o", ClassRelEnd::None, ClassRelEnd::Aggregation, false },
            { "-->", ClassRelEnd::None, ClassRelEnd::Dependency, false },
            { "<--", ClassRelEnd::Dependency, ClassRelEnd::None, false },
            { "..>", ClassRelEnd::None, ClassRelEnd::Dependency, true },
            { "()--", ClassRelEnd::Lollipop, ClassRelEnd::None, false },
            { "--", ClassRelEnd::None, ClassRelEnd::None, false },
            { "..", ClassRelEnd::None, ClassRelEnd::None, true },
            { "<-->", ClassRelEnd::Dependency, ClassRelEnd::Dependency, false },
        };
        for (const Case &c : cases) {
            const ParseResult r = parse(
                QStringLiteral("classDiagram\n  A %1 B")
                    .arg(QLatin1String(c.op)));
            QVERIFY2(!r.hasErrors(), c.op);
            QCOMPARE(r.classDiagram.relations.size(), 1);
            const ClassRelation &rel = r.classDiagram.relations.first();
            QVERIFY2(rel.fromEnd == c.fromEnd, c.op);
            QVERIFY2(rel.toEnd == c.toEnd, c.op);
            QVERIFY2(rel.dotted == c.dotted, c.op);
        }
    }

    void cardinalitiesAndLabels()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  Class03 \"0\" *-- \"0..n\" Class04\n"
            "  Class09 \"many\" --> \"1\" C2 : Where am i?\n"));
        QVERIFY(!r.hasErrors());
        const ClassRelation *r1 = relation(r.classDiagram, "Class03", "Class04");
        QVERIFY(r1);
        QCOMPARE(r1->fromCard, QString("0"));
        QCOMPARE(r1->toCard, QString("0..n"));
        QCOMPARE(r1->fromEnd, ClassRelEnd::Composition);
        const ClassRelation *r2 = relation(r.classDiagram, "Class09", "C2");
        QVERIFY(r2);
        QCOMPARE(r2->fromCard, QString("many"));
        QCOMPARE(r2->toCard, QString("1"));
        QCOMPARE(r2->label, QString("Where am i?"));
    }

    void membersViaColonAndBody()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  Animal : +int age\n"
            "  Animal : +isMammal()\n"
            "  class Duck{\n"
            "    +String beakColor\n"
            "    +swim()\n"
            "    +quack()\n"
            "  }\n"));
        QVERIFY(!r.hasErrors());
        const ClassNode *animal = node(r.classDiagram, "Animal");
        QVERIFY(animal);
        QCOMPARE(animal->attributes, QStringList{ "+int age" });
        QCOMPARE(animal->methods, QStringList{ "+isMammal()" });
        const ClassNode *duck = node(r.classDiagram, "Duck");
        QVERIFY(duck);
        QCOMPARE(duck->attributes.size(), 1);
        QCOMPARE(duck->methods.size(), 2);
    }

    void annotationsInlineStandaloneAndInBody()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  class Shape <<interface>>\n"
            "  <<abstract>> Animal\n"
            "  class Svc {\n"
            "    <<service>>\n"
            "    int id\n"
            "  }\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(node(r.classDiagram, "Shape")->annotation, QString("interface"));
        QCOMPARE(node(r.classDiagram, "Animal")->annotation, QString("abstract"));
        QCOMPARE(node(r.classDiagram, "Svc")->annotation, QString("service"));
        QCOMPARE(node(r.classDiagram, "Svc")->attributes,
                 QStringList{ "int id" });
    }

    void genericsAndLabelsAndBackticks()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  class Squirrel~T~\n"
            "  class Animal[\"Animal with a label\"]\n"
            "  class `Bank Account`\n"
            "  `Bank Account` <|-- Squirrel~T~\n"));
        QVERIFY(!r.hasErrors());
        const ClassNode *sq = node(r.classDiagram, "Squirrel~T~");
        QVERIFY(sq);
        QCOMPARE(sq->label, QString("Squirrel<T>"));
        QCOMPARE(node(r.classDiagram, "Animal")->label,
                 QString("Animal with a label"));
        QVERIFY(node(r.classDiagram, "Bank Account"));
        QCOMPARE(r.classDiagram.relations.size(), 1);
    }

    void namespacesGroupClasses()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  namespace BaseShapes {\n"
            "    class Triangle\n"
            "    class Rectangle {\n"
            "      double width\n"
            "    }\n"
            "  }\n"
            "  class Free\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QCOMPARE(r.classDiagram.namespaces.size(), 1);
        QCOMPARE(r.classDiagram.namespaces.first().classIds,
                 (QStringList{ "Triangle", "Rectangle" }));
        QCOMPARE(node(r.classDiagram, "Free")->namespaceIndex, -1);
        QCOMPARE(node(r.classDiagram, "Triangle")->namespaceIndex, 0);
    }

    void notesFreeAndForClass()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  note \"This is a general note\"\n"
            "  note for Cat \"should have no members area\"\n"
            "  Animal ()-- Cat\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.classDiagram.notes.size(), 2);
        QCOMPARE(r.classDiagram.notes.at(0).forClass, QString());
        QCOMPARE(r.classDiagram.notes.at(1).forClass, QString("Cat"));
    }

    void directionAndStyling()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  direction LR\n"
            "  classDef pink fill:#f9f\n"
            "  class A:::pink\n"
            "  cssClass \"B,C\" pink\n"
            "  style A fill:#ccf\n"
            "  A --> B\n"
            "  B --> C\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.classDiagram.direction, Direction::LR);
        QVERIFY(r.classDiagram.classDefs.contains("pink"));
        QVERIFY(node(r.classDiagram, "A")->cssClasses.contains("pink"));
        QVERIFY(node(r.classDiagram, "B")->cssClasses.contains("pink"));
        QVERIFY(node(r.classDiagram, "A")->cssClasses.contains("__style_A"));
    }

    void interactivityWarnsNotFails()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  class Shape\n"
            "  click Shape href \"https://example.com\"\n"
            "  callback Shape \"cb\"\n"
            "  link Shape \"https://example.com\"\n"));
        QVERIFY(!r.hasErrors());
        int warnings = 0;
        for (const Diagnostic &d : r.diagnostics)
            if (d.severity == Diagnostic::Warning)
                ++warnings;
        QCOMPARE(warnings, 3);
    }

    void memberTextWithDashesStaysAMember()
    {
        const ParseResult r = parse(
            "classDiagram\n  A : --strange-flag");
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.classDiagram.relations.size(), 0);
        QCOMPARE(node(r.classDiagram, "A")->attributes,
                 QStringList{ "--strange-flag" });
    }

    void demoCorpusParsesClean()
    {
        // Lifted from demos/classchart.html of the pinned version (MIT).
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  Class01 <|-- AveryLongClass : Cool\n"
            "  <<interface>> Class01\n"
            "  Class03 \"0\" *-- \"0..n\" Class04\n"
            "  Class05 \"1\" o-- \"many\" Class06\n"
            "  Class07 .. Class08\n"
            "  Class09 \"many\" --> \"1\" C2 : Where am i?\n"
            "  Class09 \"0\" --* \"1..n\" C3\n"
            "  Class09 --|> Class07\n"
            "  Class07 : equals()\n"
            "  Class07 : Object[] elementData\n"
            "  Class01 : #size()\n"
            "  Class01 : -int chimp\n"
            "  Class01 : +int gorilla\n"
            "  Class08 <--> C2: Cool label\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QCOMPARE(r.classDiagram.relations.size(), 8);
        QCOMPARE(node(r.classDiagram, "Class01")->annotation,
                 QString("interface"));
        QCOMPARE(node(r.classDiagram, "Class01")->methods,
                 QStringList{ "#size()" });
        QCOMPARE(node(r.classDiagram, "Class01")->attributes.size(), 2);
    }

    // ---- layout ----

    void layoutCompartmentBoxes()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  class Animal {\n"
            "    +int age\n"
            "    +isMammal()\n"
            "  }\n"
            "  Animal <|-- Duck\n"));
        const Scene s = layoutClassDiagram(r.classDiagram, opts());
        QCOMPARE(s.shapes.size(), 2);
        // Each class box carries two compartment separators, plus the relation.
        QCOMPARE(s.paths.size(), 5);
        bool titleBold = false, member = false;
        for (const Text &t : s.texts) {
            if (t.text == QLatin1String("Animal") && t.bold)
                titleBold = true;
            if (t.text == QLatin1String("+int age")
                && (t.align & Qt::AlignLeft))
                member = true;
        }
        QVERIFY(titleBold);
        QVERIFY(member);
        QVERIFY(s.summary.contains("2 classes"));
    }

    void layoutExtensionMarker()
    {
        const ParseResult r = parse("classDiagram\n  Animal <|-- Duck");
        const Scene s = layoutClassDiagram(r.classDiagram, opts());
        bool triangle = false;
        for (const Path &p : s.paths)
            if (p.startMarker == Marker::TriangleOpen)
                triangle = true;
        QVERIFY(triangle);
    }

    void layoutNamespaceGroup()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  namespace Shapes {\n"
            "    class A\n"
            "    class B\n"
            "  }\n"
            "  A --> B\n"));
        const Scene s = layoutClassDiagram(r.classDiagram, opts());
        QCOMPARE(s.groups.size(), 1);
        QCOMPARE(s.groups.first().title, QString("Shapes"));
    }

    void layoutNoteBoxes()
    {
        const ParseResult r = parse(QStringLiteral(
            "classDiagram\n"
            "  class Cat\n"
            "  note for Cat \"a note\"\n"));
        const Scene s = layoutClassDiagram(r.classDiagram, opts());
        bool note = false;
        for (const Shape &sh : s.shapes)
            if (sh.fillRole == Role::NoteFill)
                note = true;
        QVERIFY(note);
    }

    void layoutDeterministic()
    {
        const QString src = QStringLiteral(
            "classDiagram\n  A <|-- B\n  A <|-- C\n  B --> D\n  C --> D\n");
        const Scene s1 = layoutClassDiagram(parse(src).classDiagram, opts());
        const Scene s2 = layoutClassDiagram(parse(src).classDiagram, opts());
        QCOMPARE(s1.shapes.size(), s2.shapes.size());
        for (int i = 0; i < s1.shapes.size(); ++i) {
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.x() + 1,
                                  s2.shapes.at(i).rect.x() + 1));
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.y() + 1,
                                  s2.shapes.at(i).rect.y() + 1));
        }
    }

    void rendererRendersClassDiagram()
    {
        clearCache();
        const RenderResult r = render(
            QStringLiteral("classDiagram\n  Animal <|-- Duck"), opts());
        QVERIFY(r.valid);
        QVERIFY(!r.unsupportedFamily);
        QVERIFY(!r.hasError);
    }
};

QTEST_MAIN(TestMermaidClass)
#include "test_mermaidclass.moc"
