// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include "clipboardhelper.h"

// Covers the multi-format clipboard channel (features.md §5.1, §5.3): what
// copying puts on offer, and how pasting decides to read what is there.
class TestClipboardHelper : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void testSetMarkdownWritesEveryFlavor();
    void testInternalPayloadPastesVerbatim();
    void testStructuredHtmlConverts();
    void testUnstructuredHtmlFallsBackToText();
    void testPlainTextPassesThrough();
    void testUrlDetection_data();
    void testUrlDetection();

private:
    ClipboardHelper helper;
};

void TestClipboardHelper::init()
{
    QGuiApplication::clipboard()->clear();
}

void TestClipboardHelper::testSetMarkdownWritesEveryFlavor()
{
    helper.setMarkdown(QStringLiteral("**bold**"),
                       QStringLiteral("<p><b>bold</b></p>"));

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QCOMPARE(mime->text(), QString("**bold**"));
    QVERIFY(mime->hasHtml());
    QVERIFY(mime->hasFormat(QString::fromLatin1(
        ClipboardHelper::internalMimeType())));
    QVERIFY(helper.hasInternalMarkdown());
}

void TestClipboardHelper::testInternalPayloadPastesVerbatim()
{
    // Kvit's own copy carries both markdown and HTML. Pasting it back must
    // use the markdown as-is; running the HTML arm would double-escape the
    // syntax (the "**" would come back as literal asterisks).
    helper.setMarkdown(QStringLiteral("# Title\n\n- **item**"),
                       QStringLiteral("<h1>Title</h1><ul><li><b>item</b></li></ul>"));
    QCOMPARE(helper.markdown(), QString("# Title\n\n- **item**"));
}

void TestClipboardHelper::testStructuredHtmlConverts()
{
    // A browser copy: HTML carries the structure, and the plain-text flavor
    // has already lost it. The HTML arm must win.
    auto *mime = new QMimeData;
    mime->setText(QStringLiteral("Title item"));
    mime->setHtml(QStringLiteral("<h1>Title</h1><ul><li>item</li></ul>"));
    QGuiApplication::clipboard()->setMimeData(mime);

    const QString md = helper.markdown();
    QVERIFY2(md.contains("# Title"), qPrintable(md));
    QVERIFY2(md.contains("- item"), qPrintable(md));
}

void TestClipboardHelper::testUnstructuredHtmlFallsBackToText()
{
    // HTML that is only a wrapper carries nothing the text flavor lacks, so
    // the source's own spacing is kept rather than reflowed by the converter.
    auto *mime = new QMimeData;
    mime->setText(QStringLiteral("line one\nline two"));
    mime->setHtml(QStringLiteral("<html><body>line one\nline two</body></html>"));
    QGuiApplication::clipboard()->setMimeData(mime);

    QCOMPARE(helper.markdown(), QString("line one\nline two"));
}

void TestClipboardHelper::testPlainTextPassesThrough()
{
    QGuiApplication::clipboard()->setText(QStringLiteral("just text"));
    QVERIFY(!helper.hasInternalMarkdown());
    QCOMPARE(helper.markdown(), QString("just text"));
}

void TestClipboardHelper::testUrlDetection_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("isUrl");
    QTest::newRow("https") << "https://example.com/a" << true;
    QTest::newRow("http") << "http://example.com" << true;
    QTest::newRow("www") << "www.example.com" << true;
    QTest::newRow("surrounded by spaces") << "  https://example.com  " << true;
    // Prose containing a URL is text, not a link paste.
    QTest::newRow("prose") << "see https://example.com for more" << false;
    QTest::newRow("two urls") << "https://a.com https://b.com" << false;
    QTest::newRow("plain word") << "example" << false;
    QTest::newRow("empty") << "" << false;
}

void TestClipboardHelper::testUrlDetection()
{
    QFETCH(QString, text);
    QFETCH(bool, isUrl);
    QGuiApplication::clipboard()->setText(text);
    QCOMPARE(helper.hasUrl(), isUrl);
    if (isUrl)
        QCOMPARE(helper.url(), text.trimmed());
}

QTEST_MAIN(TestClipboardHelper)
#include "test_clipboardhelper.moc"
