// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "textfileviewmodel.h"

namespace {

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

} // namespace

class TextFileViewModelTests : public QObject
{
    Q_OBJECT

private slots:
    void utf8BomAndCrLfAreNormalized();
    void sourceSuffixChoosesLanguage();
    void lineAddressIsClampedAndMapped();
    void binaryAndInvalidUtf8AreRefused();
    void oversizedFileOffersTheFallbackState();
};

void TextFileViewModelTests::utf8BomAndCrLfAreNormalized()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("main.rs"));
    writeBytes(path, QByteArray("\xEF\xBB\xBF")
                     + "fn main() {\r\n    println!(\"hi\");\r\n}\r\n");

    TextFileViewModel model;
    QVERIFY(model.open(path));
    QCOMPARE(model.state(), QStringLiteral("ready"));
    QCOMPARE(model.language(), QStringLiteral("rust"));
    QVERIFY(!model.text().startsWith(QChar::ByteOrderMark));
    QVERIFY(!model.text().contains(QLatin1Char('\r')));
    QCOMPARE(model.lineCount(), 4);
}

void TextFileViewModelTests::sourceSuffixChoosesLanguage()
{
    const QList<QPair<QString, QString>> cases = {
        {QStringLiteral("a.go"), QStringLiteral("go")},
        {QStringLiteral("a.rs"), QStringLiteral("rust")},
        {QStringLiteral("a.ts"), QStringLiteral("typescript")},
        {QStringLiteral("a.tsx"), QStringLiteral("typescript")},
        {QStringLiteral("a.cs"), QStringLiteral("csharp")},
        {QStringLiteral("a.qml"), QStringLiteral("qml")},
        {QStringLiteral("a.unknown"), QString()},
    };
    for (const auto &item : cases)
        QCOMPARE(TextFileViewModel::languageForPath(item.first), item.second);
}

void TextFileViewModelTests::lineAddressIsClampedAndMapped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("lines.go"));
    writeBytes(path, "one\ntwo\nthree\n");

    TextFileViewModel model;
    QVERIFY(model.open(path, 3));
    QCOMPARE(model.requestedLine(), 3);
    QCOMPARE(model.positionForLine(1), 0);
    QCOMPARE(model.positionForLine(2), 4);
    QCOMPARE(model.positionForLine(3), 8);
    QCOMPARE(model.positionForLine(99), 14);

    QVERIFY(model.open(path, 42));
    QCOMPARE(model.requestedLine(), 4);
}

void TextFileViewModelTests::binaryAndInvalidUtf8AreRefused()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TextFileViewModel model;

    const QString binary = dir.filePath(QStringLiteral("binary.txt"));
    writeBytes(binary, QByteArray("a\0b", 3));
    QVERIFY(!model.open(binary));
    QCOMPARE(model.state(), QStringLiteral("binary"));
    QVERIFY(model.text().isEmpty());

    const QString invalid = dir.filePath(QStringLiteral("invalid.txt"));
    writeBytes(invalid, QByteArray::fromHex("c328"));
    QVERIFY(!model.open(invalid));
    QCOMPARE(model.state(), QStringLiteral("binary"));
    QVERIFY(model.message().contains(QStringLiteral("UTF-8")));
}

void TextFileViewModelTests::oversizedFileOffersTheFallbackState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("large.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(TextFileViewModel::MaxFileBytes + 1));
    file.close();

    TextFileViewModel model;
    QVERIFY(!model.open(path));
    QCOMPARE(model.state(), QStringLiteral("tooLarge"));
    QVERIFY(model.message().contains(QStringLiteral("desktop")));
    QVERIFY(model.text().isEmpty());
}

QTEST_GUILESS_MAIN(TextFileViewModelTests)
#include "test_textfileviewmodel.moc"
