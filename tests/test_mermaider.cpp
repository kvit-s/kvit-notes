// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "diagrams/diagramlayout.h"
#include "diagrams/mermaidparser.h"
#include "diagrams/mermaidrenderer.h"

using namespace Mermaid;
using namespace Diagram;

// ER-diagram parser and layout, built grammar-first against the pinned
// mermaid@11.16.0 erDiagram.jison. Some fixtures follow that repository's
// demos/er.html (MIT license, (c) Knut Sveidqvist).
class TestMermaidEr : public QObject
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
    static const ErEntity *entity(const ErAst &a, const QString &id)
    {
        const int i = a.indexOfEntity(id);
        return i >= 0 ? &a.entities.at(i) : nullptr;
    }

private slots:
    void familyIsSupported()
    {
        const ParseResult r = parse(
            "erDiagram\n  CUSTOMER ||--o{ ORDER : places");
        QCOMPARE(r.type, DiagramType::Er);
        QVERIFY(r.supported);
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.er.entities.size(), 2);
        QCOMPARE(r.er.relationships.size(), 1);
        QCOMPARE(r.er.relationships.first().label, QString("places"));
    }

    void symbolicCardinalities()
    {
        struct Case {
            const char *op;
            ErCardinality fromCard;
            ErCardinality toCard;
            bool identifying;
        };
        const Case cases[] = {
            { "||--o{", ErCardinality::OnlyOne, ErCardinality::ZeroOrMore, true },
            { "|o--||", ErCardinality::ZeroOrOne, ErCardinality::OnlyOne, true },
            { "}o--o|", ErCardinality::ZeroOrMore, ErCardinality::ZeroOrOne, true },
            { "}|..|{", ErCardinality::OneOrMore, ErCardinality::OneOrMore, false },
            { "||.-||", ErCardinality::OnlyOne, ErCardinality::OnlyOne, false },
            { "||-.||", ErCardinality::OnlyOne, ErCardinality::OnlyOne, false },
        };
        for (const Case &c : cases) {
            const ParseResult r = parse(
                QStringLiteral("erDiagram\n  A %1 B : rel")
                    .arg(QLatin1String(c.op)));
            QVERIFY2(!r.hasErrors(), c.op);
            QCOMPARE(r.er.relationships.size(), 1);
            const ErRelationship &rel = r.er.relationships.first();
            QVERIFY2(rel.fromCard == c.fromCard, c.op);
            QVERIFY2(rel.toCard == c.toCard, c.op);
            QVERIFY2(rel.identifying == c.identifying, c.op);
        }
    }

    void verboseCardinalities()
    {
        const ParseResult r = parse(QStringLiteral(
            "erDiagram\n"
            "  CAR one or more to zero or more PERSON : driver\n"
            "  HOUSE one or zero optionally to many ROOM : contains\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QCOMPARE(r.er.relationships.size(), 2);
        QCOMPARE(r.er.relationships.at(0).fromCard, ErCardinality::OneOrMore);
        QCOMPARE(r.er.relationships.at(0).toCard, ErCardinality::ZeroOrMore);
        QVERIFY(r.er.relationships.at(0).identifying);
        QCOMPARE(r.er.relationships.at(1).fromCard, ErCardinality::ZeroOrOne);
        QCOMPARE(r.er.relationships.at(1).toCard, ErCardinality::ZeroOrMore);
        QVERIFY(!r.er.relationships.at(1).identifying);
    }

    void attributeBlocks()
    {
        const ParseResult r = parse(QStringLiteral(
            "erDiagram\n"
            "  CUSTOMER {\n"
            "    string name PK \"customer name\"\n"
            "    int custNumber PK, FK\n"
            "    string sector\n"
            "  }\n"
            "  CUSTOMER ||--o{ ORDER : places\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        const ErEntity *e = entity(r.er, "CUSTOMER");
        QVERIFY(e);
        QCOMPARE(e->attributes.size(), 3);
        QCOMPARE(e->attributes.at(0).type, QString("string"));
        QCOMPARE(e->attributes.at(0).name, QString("name"));
        QCOMPARE(e->attributes.at(0).keys, QStringList{ "PK" });
        QCOMPARE(e->attributes.at(0).comment, QString("customer name"));
        QCOMPARE(e->attributes.at(1).keys, (QStringList{ "PK", "FK" }));
        QVERIFY(e->attributes.at(2).keys.isEmpty());
    }

    void quotedEntityNamesAndAliases()
    {
        const ParseResult r = parse(QStringLiteral(
            "erDiagram\n"
            "  \"Person Entity\" }|..|{ \"Delivery Address\" : uses\n"
            "  p[Person] {\n"
            "    string firstName\n"
            "  }\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QVERIFY(entity(r.er, "Person Entity"));
        QVERIFY(entity(r.er, "Delivery Address"));
        const ErEntity *p = entity(r.er, "p");
        QVERIFY(p);
        QCOMPARE(p->label, QString("Person"));
        QCOMPARE(p->attributes.size(), 1);
    }

    void quotedRoleAndGenericTypes()
    {
        const ParseResult r = parse(QStringLiteral(
            "erDiagram\n"
            "  A ||--|| B : \"has a long role\"\n"
            "  BOX {\n"
            "    type~T~ contents\n"
            "  }\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.er.relationships.first().label, QString("has a long role"));
        QCOMPARE(entity(r.er, "BOX")->attributes.first().type,
                 QString("type~T~"));
    }

    void stylingAndDirection()
    {
        const ParseResult r = parse(QStringLiteral(
            "erDiagram\n"
            "  direction LR\n"
            "  classDef important fill:#f00\n"
            "  CUSTOMER:::important ||--o{ ORDER : places\n"
            "  class ORDER important\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QCOMPARE(r.er.direction, Direction::LR);
        QVERIFY(entity(r.er, "CUSTOMER")->cssClasses.contains("important"));
        QVERIFY(entity(r.er, "ORDER")->cssClasses.contains("important"));
    }

    void missingCardinalityOrRoleIsAnError()
    {
        QVERIFY(parse("erDiagram\n  A -- B : r").hasErrors());
        QVERIFY(parse("erDiagram\n  A ||--o{ B").hasErrors());
    }

    void bareEntityDeclarations()
    {
        const ParseResult r = parse("erDiagram\n  CUSTOMER\n  ORDER\n");
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.er.entities.size(), 2);
    }

    // ---- layout ----

    void layoutEntityTables()
    {
        const ParseResult r = parse(QStringLiteral(
            "erDiagram\n"
            "  CUSTOMER {\n"
            "    string name PK\n"
            "    int age\n"
            "  }\n"
            "  CUSTOMER ||--o{ ORDER : places\n"));
        const Scene s = layoutErDiagram(r.er, opts());
        QCOMPARE(s.shapes.size(), 2);
        bool typeCell = false, boldTitle = false;
        for (const Text &t : s.texts) {
            if (t.text == QLatin1String("string"))
                typeCell = true;
            if (t.text == QLatin1String("CUSTOMER") && t.bold)
                boldTitle = true;
        }
        QVERIFY(typeCell);
        QVERIFY(boldTitle);
        QVERIFY(s.summary.contains("2 entities"));
    }

    void layoutCrowsFootMarkers()
    {
        const ParseResult r = parse(
            "erDiagram\n  CUSTOMER ||--o{ ORDER : places");
        const Scene s = layoutErDiagram(r.er, opts());
        bool one = false, zeroMany = false;
        for (const Path &p : s.paths) {
            if (p.startMarker == Marker::ErOne)
                one = true;
            if (p.endMarker == Marker::ErZeroMany)
                zeroMany = true;
        }
        QVERIFY(one);
        QVERIFY(zeroMany);
    }

    void layoutNonIdentifyingIsDashed()
    {
        const ParseResult r = parse("erDiagram\n  A ||..o{ B : maybe");
        const Scene s = layoutErDiagram(r.er, opts());
        bool dashed = false;
        for (const Path &p : s.paths)
            if (p.penStyle == Qt::DashLine && p.endMarker != Marker::None)
                dashed = true;
        QVERIFY(dashed);
    }

    void layoutDeterministic()
    {
        const QString src = QStringLiteral(
            "erDiagram\n"
            "  CUSTOMER ||--o{ ORDER : places\n"
            "  ORDER ||--|{ LINE-ITEM : contains\n"
            "  CUSTOMER }|..|{ DELIVERY-ADDRESS : uses\n");
        const Scene s1 = layoutErDiagram(parse(src).er, opts());
        const Scene s2 = layoutErDiagram(parse(src).er, opts());
        QCOMPARE(s1.shapes.size(), s2.shapes.size());
        for (int i = 0; i < s1.shapes.size(); ++i) {
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.x() + 1,
                                  s2.shapes.at(i).rect.x() + 1));
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.y() + 1,
                                  s2.shapes.at(i).rect.y() + 1));
        }
    }

    void demoCorpusParsesClean()
    {
        // Lifted from demos/er.html of the pinned version (MIT).
        const ParseResult r = parse(QStringLiteral(
            "erDiagram\n"
            "  CUSTOMER }|..|{ DELIVERY-ADDRESS : has\n"
            "  CUSTOMER ||--o{ ORDER : places\n"
            "  CUSTOMER ||--o{ INVOICE : \"liable for\"\n"
            "  DELIVERY-ADDRESS ||--o{ ORDER : receives\n"
            "  INVOICE ||--|{ ORDER : covers\n"
            "  ORDER ||--|{ ORDER-ITEM : includes\n"
            "  PRODUCT-CATEGORY ||--|{ PRODUCT : contains\n"
            "  PRODUCT ||--o{ ORDER-ITEM : \"ordered in\"\n"));
        QVERIFY2(!r.hasErrors(),
                 qPrintable(r.hasErrors() ? r.firstError().message : ""));
        QCOMPARE(r.er.relationships.size(), 8);
        QCOMPARE(r.er.entities.size(), 7);
    }

    void rendererRendersErDiagram()
    {
        clearCache();
        const RenderResult r = render(
            QStringLiteral("erDiagram\n  CUSTOMER ||--o{ ORDER : places"),
            opts());
        QVERIFY(r.valid);
        QVERIFY(!r.unsupportedFamily);
        QVERIFY(!r.hasError);
    }
};

QTEST_MAIN(TestMermaidEr)
#include "test_mermaider.moc"
