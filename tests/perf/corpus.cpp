// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "perf/corpus.h"

#include "block.h"
#include "documentserializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

const char *kWords[] = {
    "Well", "Prince", "so", "Genoa", "and", "Lucca", "are", "now",
    "just", "family", "estates", "of", "the", "Buonapartes", "But",
    "I", "warn", "you", "if", "you", "do", "not", "tell", "me",
    "that", "this", "means", "war", "again", "peace", "court",
    "winter", "field", "letter", "memory", "march", "river", "light",
};

QString prose(int words, int seed)
{
    QString out;
    out.reserve(words * 7);
    const int wordCount = int(sizeof(kWords) / sizeof(kWords[0]));
    for (int i = 0; i < words; ++i) {
        if (i)
            out += QLatin1Char(' ');
        out += QLatin1String(kWords[(seed + i) % wordCount]);
    }
    return out;
}

PerfCorpus::DocumentFixture generatedLargeDocument(int blocks,
                                                   int targetWords,
                                                   int headings)
{
    PerfCorpus::DocumentFixture fixture;
    fixture.blocks = blocks;
    fixture.words = targetWords;
    fixture.headings = headings;
    fixture.markdown.reserve(targetWords * 7);

    const int baseWords = targetWords / blocks;
    int extras = targetWords % blocks;
    for (int i = 0; i < blocks; ++i) {
        if (i > 0)
            fixture.markdown += QStringLiteral("\n\n");
        const int words = baseWords + (extras-- > 0 ? 1 : 0);
        if (i < headings) {
            const int level = (i % 4) + 1;
            fixture.markdown += QString(level, QLatin1Char('#'));
            fixture.markdown += QLatin1Char(' ');
        }
        fixture.markdown += prose(words, i);
    }
    fixture.markdown += QLatin1Char('\n');
    return fixture;
}

bool writeText(const QString &path, const QString &text)
{
    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << text;
    return true;
}

} // namespace

namespace PerfCorpus {

DocumentFixture warAndPeace()
{
    return generatedLargeDocument(6241, 562900, 382);
}

DocumentFixture warAndPeaceLiveSized()
{
    return generatedLargeDocument(11752, 562365, 383);
}

DocumentFixture warAndPeaceSynth()
{
    return generatedLargeDocument(6241, 561690, 0);
}

DocumentFixture headings2K()
{
    DocumentFixture fixture;
    fixture.blocks = 4000;
    fixture.headings = 2000;
    fixture.words = 0;
    fixture.markdown.reserve(500000);
    for (int i = 0; i < 2000; ++i) {
        if (!fixture.markdown.isEmpty())
            fixture.markdown += QStringLiteral("\n\n");
        const int level = (i % 4) + 1;
        const QString heading = prose(8, i);
        const QString body = prose(24, i + 17);
        fixture.markdown += QString(level, QLatin1Char('#'));
        fixture.markdown += QLatin1Char(' ');
        fixture.markdown += heading;
        fixture.markdown += QStringLiteral("\n\n");
        fixture.markdown += body;
        fixture.words += 32;
    }
    fixture.markdown += QLatin1Char('\n');
    return fixture;
}

DocumentFixture list5K()
{
    DocumentFixture fixture;
    fixture.blocks = 5000;
    fixture.words = 5000 * 8;
    fixture.headings = 0;
    fixture.markdown.reserve(350000);
    for (int i = 0; i < fixture.blocks; ++i) {
        if (i > 0)
            fixture.markdown += QLatin1Char('\n');
        fixture.markdown += QString::number(i + 1);
        fixture.markdown += QStringLiteral(". ");
        fixture.markdown += prose(8, i);
    }
    fixture.markdown += QLatin1Char('\n');
    return fixture;
}

DocumentFixture mixed100()
{
    DocumentFixture fixture;
    fixture.blocks = 100;
    fixture.words = 0;
    fixture.headings = 10;
    fixture.markdown.reserve(20000);
    for (int i = 0; i < fixture.blocks; ++i) {
        if (i > 0)
            fixture.markdown += QStringLiteral("\n\n");
        switch (i % 10) {
        case 0:
            fixture.markdown += QStringLiteral("# ");
            fixture.markdown += prose(6, i);
            fixture.words += 6;
            break;
        case 1:
            fixture.markdown += QStringLiteral("- ");
            fixture.markdown += prose(9, i);
            fixture.words += 9;
            break;
        case 2:
            fixture.markdown += QStringLiteral("> ");
            fixture.markdown += prose(12, i);
            fixture.words += 12;
            break;
        case 3:
            fixture.markdown += QStringLiteral("```cpp\nint value = ");
            fixture.markdown += QString::number(i);
            fixture.markdown += QStringLiteral(";\n```");
            fixture.words += 3;
            break;
        case 4:
            fixture.markdown += QStringLiteral("- [ ] ");
            fixture.markdown += prose(8, i);
            fixture.words += 8;
            break;
        default:
            fixture.markdown += prose(20, i);
            fixture.words += 20;
            break;
        }
    }
    fixture.markdown += QLatin1Char('\n');
    return fixture;
}

int writeVault10K(const QString &rootPath)
{
    int written = 0;
    for (int i = 0; i < 10000; ++i) {
        const QString folder =
            QStringLiteral("Folder_%1/Sub_%2")
                .arg(i % 100, 2, 10, QLatin1Char('0'))
                .arg(i % 10, 2, 10, QLatin1Char('0'));
        const QString relPath =
            folder + QStringLiteral("/Note_%1.md")
                .arg(i, 5, 10, QLatin1Char('0'));
        QString note;
        note.reserve(1600);
        note += QStringLiteral("---\ntags: [perf, vault]\n---\n");
        note += QStringLiteral("# ");
        note += prose(6, i);
        note += QStringLiteral("\n\n");
        note += prose(194, i + 31);
        written += writeSingleNote(rootPath, relPath, note);
    }

    const DocumentFixture large = warAndPeace();
    written += writeSingleNote(rootPath, QStringLiteral("Large/WarAndPeace.md"),
                               large.markdown);
    written += writeSingleNote(rootPath, QStringLiteral("Large/Headings.md"),
                               headings2K().markdown);
    written += writeSingleNote(rootPath, QStringLiteral("Large/List.md"),
                               list5K().markdown);
    return written;
}

int writeSingleNote(const QString &rootPath,
                    const QString &relPath,
                    const QString &markdown)
{
    return writeText(QDir(rootPath).filePath(relPath), markdown) ? 1 : 0;
}

int countedBlocks(const QString &markdown)
{
    DocumentSerializer serializer;
    return serializer.parse(markdown).size();
}

int countedWords(const QString &markdown)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    DocumentSerializer serializer;
    int words = 0;
    const QList<DocumentSerializer::BlockData> blocks = serializer.parse(markdown);
    for (const DocumentSerializer::BlockData &block : blocks)
        words += block.content.split(whitespace, Qt::SkipEmptyParts).size();
    return words;
}

int countedHeadings(const QString &markdown)
{
    DocumentSerializer serializer;
    int headings = 0;
    const QList<DocumentSerializer::BlockData> blocks = serializer.parse(markdown);
    for (const DocumentSerializer::BlockData &block : blocks) {
        if (block.type == Block::Heading1 || block.type == Block::Heading2
            || block.type == Block::Heading3 || block.type == Block::Heading4) {
            ++headings;
        }
    }
    return headings;
}

} // namespace PerfCorpus
