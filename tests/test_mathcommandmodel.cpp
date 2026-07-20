// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "mathcommandmodel.h"
#include "mathrenderer.h"

// The math-command catalog and matcher behind the backslash menu. Browse
// mode reads categories() + itemsForCategory(); completion mode reads
// itemsFor(query). The catalog-integrity tests render every curated entry
// through the real MicroTeX engine, so the menu can never offer TeX the
// app cannot render.
class TestMathCommandModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testCategoriesCanonical();
    void testRecentlyUsedLeadsCategories();
    void testCategoryEntriesCarryTemplates();
    void testCatalogPreviewsRender();
    void testCatalogTemplatesRender();
    void testCursorOffsets();
    void testPrefixBeatsSubstringBeatsSubsequence();
    void testCaseExactBeatsCaseInsensitive();
    void testCuratedRanksAboveEnumerated();
    void testCdotCompletesExactly();
    void testEveryCuratedEntryCompletes();
    void testEnumeratedCommandsComplete();
    void testAliasesMatch();
    void testDoubleBackslashMatches();
    void testNoMatchYieldsEmpty();
    void testRecencyReordersAndCaps();
    void testRecentRoundTrip();
    void testSetRecentDoesNotSignal();

private:
    static QStringList names(const QVariantList &rows)
    {
        QStringList result;
        for (const QVariant &row : rows)
            result.append(row.toMap().value("name").toString());
        return result;
    }

    // Fill a template's empty slots so it parses: {} -> {x}, [] -> [n].
    static QString filled(QString insert)
    {
        insert.replace(QLatin1String("{}"), QLatin1String("{x}"));
        insert.replace(QLatin1String("[]"), QLatin1String("[n]"));
        return insert;
    }

    QList<QVariantMap> allCuratedEntries() const
    {
        QList<QVariantMap> entries;
        const QStringList categories = m_model->categories();
        for (const QString &category : categories) {
            if (category == QLatin1String("Recently used"))
                continue;
            const QVariantList rows = m_model->itemsForCategory(category);
            for (const QVariant &row : rows)
                entries.append(row.toMap());
        }
        return entries;
    }

    MathCommandModel *m_model = nullptr;
};

void TestMathCommandModel::init()
{
    m_model = new MathCommandModel();
}

void TestMathCommandModel::cleanup()
{
    delete m_model;
    m_model = nullptr;
}

void TestMathCommandModel::testCategoriesCanonical()
{
    const QStringList categories = m_model->categories();
    // No recency yet: the canonical list, Greek first (the LyX-toolbar
    // order), no "Recently used".
    QVERIFY(!categories.contains(QStringLiteral("Recently used")));
    QCOMPARE(categories.first(), QStringLiteral("Greek"));
    QVERIFY(categories.contains(QStringLiteral("Arrows")));
    QVERIFY(categories.contains(QStringLiteral("Big operators")));
    QVERIFY(categories.contains(QStringLiteral("Fractions & roots")));
    QVERIFY(categories.contains(QStringLiteral("Structure")));
    QVERIFY(categories.contains(QStringLiteral("Spacing")));
    // Every entry's category is a listed category.
    for (const QVariantMap &entry : allCuratedEntries())
        QVERIFY2(categories.contains(entry.value("category").toString()),
                 qPrintable(entry.value("name").toString()));
}

void TestMathCommandModel::testRecentlyUsedLeadsCategories()
{
    m_model->noteUsed(QStringLiteral("\\frac"));
    const QStringList categories = m_model->categories();
    QCOMPARE(categories.first(), QStringLiteral("Recently used"));
    const QVariantList recent =
        m_model->itemsForCategory(QStringLiteral("Recently used"));
    QCOMPARE(recent.size(), 1);
    QCOMPARE(recent.first().toMap().value("name").toString(),
             QStringLiteral("\\frac"));
}

void TestMathCommandModel::testCategoryEntriesCarryTemplates()
{
    const QVariantList rows =
        m_model->itemsForCategory(QStringLiteral("Fractions & roots"));
    QVERIFY(!rows.isEmpty());
    QVariantMap frac;
    for (const QVariant &row : rows) {
        if (row.toMap().value("name").toString() == QLatin1String("\\frac"))
            frac = row.toMap();
    }
    QCOMPARE(frac.value("insert").toString(), QStringLiteral("\\frac{}{}"));
    QCOMPARE(frac.value("cursorOffset").toInt(), 6);  // inside the first {}
    QCOMPARE(frac.value("preview").toString(), QStringLiteral("\\frac{a}{b}"));
    QVERIFY(frac.value("curated").toBool());
}

void TestMathCommandModel::testCatalogPreviewsRender()
{
    // The pixel-honesty guarantee: everything the menu shows renders
    // through the real engine. Collect all failures so an unsupported
    // command names itself in one run.
    QStringList failures;
    for (const QVariantMap &entry : allCuratedEntries()) {
        const QString preview = entry.value("preview").toString();
        if (preview.isEmpty())
            continue;  // \\ and & render nothing standalone by design
        const QString error = MathRenderer::errorFor(preview);
        if (!error.isEmpty())
            failures.append(entry.value("name").toString()
                            + QStringLiteral(" [") + preview
                            + QStringLiteral("]: ") + error);
    }
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
}

void TestMathCommandModel::testCatalogTemplatesRender()
{
    // Every standalone insertion template, slots filled, must parse — both
    // the inline and the display (multi-line) forms.
    QStringList failures;
    for (const QVariantMap &entry : allCuratedEntries()) {
        if (!entry.value("standalone").toBool())
            continue;
        for (const QString &key : { QStringLiteral("insert"),
                                    QStringLiteral("insertDisplay") }) {
            const QString tmpl = entry.value(key).toString();
            if (tmpl.isEmpty())
                continue;
            const QString error = MathRenderer::errorFor(filled(tmpl));
            if (!error.isEmpty())
                failures.append(entry.value("name").toString()
                                + QStringLiteral(" [") + filled(tmpl)
                                + QStringLiteral("]: ") + error);
        }
    }
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
}

void TestMathCommandModel::testCursorOffsets()
{
    for (const QVariantMap &entry : allCuratedEntries()) {
        const QString insert = entry.value("insert").toString();
        const int offset = entry.value("cursorOffset").toInt();
        QVERIFY2(offset >= -1 && offset <= insert.length(),
                 qPrintable(entry.value("name").toString()));
        // A template with an empty slot must put the caret inside one.
        if (insert.contains(QLatin1String("{}"))
            || insert.contains(QLatin1String("[]"))) {
            QVERIFY2(offset > 0, qPrintable(insert));
            const QChar before = insert.at(offset - 1);
            const QChar after = insert.at(offset);
            QVERIFY2((before == QLatin1Char('{') && after == QLatin1Char('}'))
                     || (before == QLatin1Char('[') && after == QLatin1Char(']')),
                     qPrintable(insert));
        }
    }
}

void TestMathCommandModel::testPrefixBeatsSubstringBeatsSubsequence()
{
    // "in": \in (prefix) must precede \sin (substring, curated) and any
    // subsequence-only match like \iint.
    const QStringList result = names(m_model->itemsFor(QStringLiteral("in")));
    QVERIFY(result.contains(QStringLiteral("\\in")));
    QVERIFY(result.contains(QStringLiteral("\\sin")));
    QVERIFY(result.indexOf(QStringLiteral("\\in"))
            < result.indexOf(QStringLiteral("\\sin")));
    // \int is also a prefix match and must precede substring matches too.
    QVERIFY(result.indexOf(QStringLiteral("\\int"))
            < result.indexOf(QStringLiteral("\\sin")));
}

void TestMathCommandModel::testCaseExactBeatsCaseInsensitive()
{
    // TeX is case-sensitive: both \omega and \Omega surface for either
    // query, exact case first.
    const QStringList lower = names(m_model->itemsFor(QStringLiteral("ome")));
    QVERIFY(lower.contains(QStringLiteral("\\omega")));
    QVERIFY(lower.contains(QStringLiteral("\\Omega")));
    QVERIFY(lower.indexOf(QStringLiteral("\\omega"))
            < lower.indexOf(QStringLiteral("\\Omega")));

    const QStringList upper = names(m_model->itemsFor(QStringLiteral("Ome")));
    QVERIFY(upper.contains(QStringLiteral("\\omega")));
    QVERIFY(upper.contains(QStringLiteral("\\Omega")));
    QVERIFY(upper.indexOf(QStringLiteral("\\Omega"))
            < upper.indexOf(QStringLiteral("\\omega")));
}

void TestMathCommandModel::testCuratedRanksAboveEnumerated()
{
    // "frac" matches the curated \frac and enumerated engine names like
    // \cfrac; the curated entry leads.
    const QVariantList rows = m_model->itemsFor(QStringLiteral("frac"));
    QVERIFY(!rows.isEmpty());
    const QVariantMap first = rows.first().toMap();
    QCOMPARE(first.value("name").toString(), QStringLiteral("\\frac"));
    QVERIFY(first.value("curated").toBool());
}

void TestMathCommandModel::testCdotCompletesExactly()
{
    // \cdot has longer engine commands such as \cdotB and \cdotBB as
    // neighbors. The ordinary binary operator must still lead.
    const QVariantList rows = m_model->itemsFor(QStringLiteral("cdot"));
    QVERIFY(!rows.isEmpty());
    QCOMPARE(rows.first().toMap().value("name").toString(),
             QStringLiteral("\\cdot"));
    QCOMPARE(rows.first().toMap().value("insert").toString(),
             QStringLiteral("\\cdot"));
}

void TestMathCommandModel::testEveryCuratedEntryCompletes()
{
    QStringList failures;
    const QHash<QString, QString> fragmentQueries{
        { QStringLiteral("^{}"), QStringLiteral("sup") },
        { QStringLiteral("_{}"), QStringLiteral("sub") },
        { QStringLiteral("_{}^{}"), QStringLiteral("subsup") },
        { QStringLiteral("&"), QStringLiteral("cell") },
    };
    for (const QVariantMap &entry : allCuratedEntries()) {
        const QString name = entry.value("name").toString();
        QString query;
        if (!name.startsWith(QLatin1Char('\\'))) {
            query = fragmentQueries.value(name);
            if (query.isEmpty()) {
                failures.append(name + QStringLiteral(" has no audit query"));
                continue;
            }
        } else if (name.size() >= 2 && name.at(1).isLetter()) {
            int end = 2;
            while (end < name.size() && name.at(end).isLetter())
                ++end;
            query = name.mid(1, end - 1);
        } else if (name.size() >= 2) {
            // TeX control symbols are one character; \\ is the menu's
            // special two-backslash query.
            query = name.at(1) == QLatin1Char('\\')
                ? QStringLiteral("\\") : name.mid(1, 1);
        } else {
            failures.append(name + QStringLiteral(" has no audit query"));
            continue;
        }

        const QStringList result = names(m_model->itemsFor(query));
        if (!result.contains(name))
            failures.append(name + QStringLiteral(" via '") + query
                            + QStringLiteral("'"));
    }
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral(", "))));
}

void TestMathCommandModel::testEnumeratedCommandsComplete()
{
    // The NewTX-port macro \vv is nowhere in the curated table; the
    // engine enumeration must surface it.
    const QStringList result = names(m_model->itemsFor(QStringLiteral("vv")));
    QVERIFY2(result.contains(QStringLiteral("\\vv")),
             qPrintable(result.join(QStringLiteral(", "))));
}

void TestMathCommandModel::testAliasesMatch()
{
    QVERIFY(names(m_model->itemsFor(QStringLiteral("choose")))
                .contains(QStringLiteral("\\binom")));
    QVERIFY(names(m_model->itemsFor(QStringLiteral("root")))
                .contains(QStringLiteral("\\sqrt")));
    QVERIFY(names(m_model->itemsFor(QStringLiteral("iff")))
                .contains(QStringLiteral("\\Leftrightarrow")));
    QVERIFY(names(m_model->itemsFor(QStringLiteral("infinity")))
                .contains(QStringLiteral("\\infty")));
}

void TestMathCommandModel::testDoubleBackslashMatches()
{
    // A second backslash right after the trigger queries "\": the row
    // separator must lead so \\ inserts through the menu inside matrices.
    const QStringList result = names(m_model->itemsFor(QStringLiteral("\\")));
    QVERIFY(!result.isEmpty());
    QCOMPARE(result.first(), QStringLiteral("\\\\"));
}

void TestMathCommandModel::testNoMatchYieldsEmpty()
{
    QVERIFY(m_model->itemsFor(QStringLiteral("zzqqxy")).isEmpty());
}

void TestMathCommandModel::testRecencyReordersAndCaps()
{
    for (int i = 0; i < MathCommandModel::MaxRecent + 3; ++i)
        m_model->noteUsed(QStringLiteral("\\cmd%1").arg(i));
    QCOMPARE(m_model->recentCommands().size(), MathCommandModel::MaxRecent);
    // Re-using moves to the front without duplicating.
    m_model->noteUsed(QStringLiteral("\\cmd5"));
    const QVariantList recent = m_model->recentCommands();
    QCOMPARE(recent.size(), MathCommandModel::MaxRecent);
    QCOMPARE(recent.first().toString(), QStringLiteral("\\cmd5"));
}

void TestMathCommandModel::testRecentRoundTrip()
{
    m_model->noteUsed(QStringLiteral("\\alpha"));
    m_model->noteUsed(QStringLiteral("\\frac"));
    const QVariantList saved = m_model->recentCommands();

    MathCommandModel restored;
    restored.setRecentCommands(saved);
    QCOMPARE(restored.recentCommands(), saved);

    // The restored recency renders as browse rows: curated entries resolve
    // with their templates.
    const QVariantList rows =
        restored.itemsForCategory(QStringLiteral("Recently used"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.first().toMap().value("name").toString(),
             QStringLiteral("\\frac"));
    QCOMPARE(rows.first().toMap().value("insert").toString(),
             QStringLiteral("\\frac{}{}"));
}

void TestMathCommandModel::testSetRecentDoesNotSignal()
{
    QSignalSpy spy(m_model, &MathCommandModel::recentChanged);
    m_model->setRecentCommands({ QStringLiteral("\\frac") });
    QCOMPARE(spy.count(), 0);  // loading persisted state must not re-save
    m_model->noteUsed(QStringLiteral("\\frac"));
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestMathCommandModel)
#include "test_mathcommandmodel.moc"
