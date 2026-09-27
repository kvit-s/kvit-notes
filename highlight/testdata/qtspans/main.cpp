// qtspans writes what the Qt app's code highlighter
// (~/kvit-notes/src/content/codelanguages.cpp) makes of every sample in a
// corpus file, in every language it knows, as TestQtSpans reads it from
// ../qt-spans.txt. It is kept so the reference can be made again while the Qt
// app still exists; the Go tests do not need it.
//
// Build it against the Qt repository's scanner and run it from this directory:
//
//   QT=~/Qt/6.10.1/gcc_64
//   g++ -std=c++20 -fPIC -I$QT/include -I$QT/include/QtCore \
//       -I ~/kvit-notes/src/content main.cpp \
//       ~/kvit-notes/src/content/codelanguages.cpp \
//       -L$QT/lib -lQt6Core -Wl,-rpath,$QT/lib -o /tmp/qtspans
//   /tmp/qtspans ../corpus.txt > ../qt-spans.txt
//
// A sample in the corpus starts after a line "==== name" and runs to the
// next such line, its lines joined with newlines. Each output line is the
// sample's name, the language, and its spans as start-end:class in code
// point offsets (Qt counts UTF-16 units; the Go port counts runes), with
// K, T, S, C and N for keyword, type, string, comment and number.
#include "codelanguages.h"

#include <QFile>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdio>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: qtspans corpus.txt\n");
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QList<QPair<QString, QString>> samples;
    QString name;
    QStringList body;
    bool open = false;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("==== "))) {
            if (open)
                samples.append({name, body.join(QLatin1Char('\n'))});
            name = line.mid(5);
            body.clear();
            open = true;
        } else if (open) {
            body.append(line);
        }
    }
    if (open)
        samples.append({name, body.join(QLatin1Char('\n'))});

    QStringList languages = CodeLanguages::supportedLanguages();
    languages.append(QStringLiteral("mermaid"));

    for (const auto &sample : samples) {
        const QString &text = sample.second;
        // The code point offset of each UTF-16 offset.
        QVector<int> cp(text.size() + 1);
        int k = 0;
        for (int i = 0; i < text.size(); ++i) {
            cp[i] = k;
            if (!(text[i].isLowSurrogate() && i > 0 && text[i - 1].isHighSurrogate()))
                ++k;
        }
        cp[text.size()] = k;

        for (const QString &lang : languages) {
            QString out = sample.first + QLatin1Char('\t') + lang + QLatin1Char('\t');
            bool first = true;
            for (const CodeLanguages::Span &s : CodeLanguages::highlightSpans(lang, text)) {
                const char cls = "PKTSCN"[static_cast<int>(s.token)];
                if (!first)
                    out += QLatin1Char(' ');
                first = false;
                out += QString::number(cp[s.start]) + QLatin1Char('-')
                       + QString::number(cp[s.start + s.length]) + QLatin1Char(':')
                       + QLatin1Char(cls);
            }
            std::printf("%s\n", out.toUtf8().constData());
        }
    }
    return 0;
}
