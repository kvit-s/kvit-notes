// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QRect>
#include <QtMath>
#include <QVector>

#include "mathrenderer.h"
#include "diagrams/diagrambudget.h"

// Render corpus for the MicroTeX seam. These are the first thing built once
// the library is vendored: they prove the resource root resolves (fonts +
// formula/symbol XML load) and that TeX turns into a non-empty raster, and
// they save the three canonical expressions as PNGs so the font-data path can
// be confirmed by eye.
class TestMathRenderer : public QObject
{
    Q_OBJECT

    struct CorpusEntry {
        QString title;
        QString tex;
    };

    struct RenderedCorpusEntry {
        CorpusEntry source;
        QImage png;
    };

    QString shotDir() const
    {
        QString dir = qEnvironmentVariable("KVIT_SHOT_DIR");
        if (dir.isEmpty())
            dir = QDir::currentPath() + QStringLiteral("/screenshots");
        QDir().mkpath(dir);
        return dir;
    }

    bool hasVisiblePixel(const QImage &image) const
    {
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 0)
                    return true;
            }
        }
        return false;
    }

    QString sourceRoot() const
    {
#ifdef KVIT_SOURCE_ROOT
        return QStringLiteral(KVIT_SOURCE_ROOT);
#else
        return QDir(MathRenderer::resourceRoot()).absoluteFilePath(
            QStringLiteral("../../.."));
#endif
    }

    QString newtxReferenceDir() const
    {
        return QDir(sourceRoot()).absoluteFilePath(
            QStringLiteral("docs/math-render-experiments/"
                           "2026-07-09-newtx-charter-reference"));
    }

    QVector<CorpusEntry> newtxCharterReferenceCorpus() const
    {
        return {
            {QStringLiteral("Inline canary"), QStringLiteral("x^2")},
            {QStringLiteral("Basic italic and superscript"),
             QStringLiteral("E = mc^2")},
            {QStringLiteral("Subscripts and powers"),
             QStringLiteral("A_i^2 + B_i^2 = C_i^2")},
            {QStringLiteral("Fraction"), QStringLiteral("\\frac{a+b}{c+d}")},
            {QStringLiteral("Nested fraction"),
             QStringLiteral("\\frac{1}{1+\\frac{x}{2}}")},
            {QStringLiteral("Radical"), QStringLiteral("\\sqrt{1 + x^2}")},
            {QStringLiteral("Integral with limits"),
             QStringLiteral("\\int_0^\\infty e^{-x^2}\\,dx")},
            {QStringLiteral("Sum with limits"),
             QStringLiteral("\\sum_{n=1}^{\\infty} \\frac{1}{n^2}")},
            {QStringLiteral("Scaled delimiters"),
             QStringLiteral("\\left(\\frac{a}{b}\\right)")},
            {QStringLiteral("Greek variants"),
             QStringLiteral("\\alpha\\beta\\Gamma\\Delta\\theta\\vartheta"
                            "\\phi\\varphi")},
            {QStringLiteral("Relations and sets"),
             QStringLiteral("\\leq \\geq \\neq \\approx \\in \\notin "
                            "\\subseteq \\cup \\cap")},
            {QStringLiteral("Operators"),
             QStringLiteral("\\partial\\quad\\nabla\\quad\\infty")},
            {QStringLiteral("Math alphabets"),
             QStringLiteral("\\mathcal{F}\\quad\\mathscr{L}\\quad\\mathbb{R}"
                            "\\quad\\mathfrak{g}")},
            // Stress rows mirroring the layout-geometry pixel assertions:
            // script/scriptscript symbols exercise the ntxsy7/ntxsy5 optical
            // masters, and roman-next-to-italic exercises the text-to-math
            // relative scale.
            {QStringLiteral("Script sizes and optical masters"),
             QStringLiteral("x^{\\in A}\\quad y_{\\oplus}\\quad "
                            "e^{x^{\\leq 2}}")},
            {QStringLiteral("Text style against math italic"),
             QStringLiteral("\\mathrm{x}x\\quad\\mathrm{d}x\\quad"
                            "\\sin(x) + \\log(y)")},
        };
    }

    // Mirrors the engine default: the vendored NewTX/XCharter charter port
    // is active unless KVIT_MATH_FONT=cm opts back into Computer Modern.
    bool newtxCharterMode() const
    {
        return QString::fromUtf8(qgetenv("KVIT_MATH_FONT")).trimmed()
               != QStringLiteral("cm");
    }

    QString newtxCorpusArtifactStem() const
    {
        if (newtxCharterMode())
            return QStringLiteral("newtx_charter_generated_prototype");
        return QStringLiteral("newtx_charter_cm");
    }

    QString newtxComparisonArtifactName() const
    {
        if (newtxCharterMode())
            return QStringLiteral("newtx_charter_reference_vs_generated_prototype.png");
        return QStringLiteral("newtx_charter_reference_vs_current_cm.png");
    }

    QString newtxCorpusModeLabel() const
    {
        if (newtxCharterMode())
            return QStringLiteral("vendored NewTX/XCharter");
        return QStringLiteral("Computer Modern MicroTeX");
    }

    int pngVerticalPadding(int textSize) const
    {
        return qMax(2, qCeil(textSize * 0.12));
    }

    bool hasVisiblePixelOnHorizontalEdge(const QImage &image) const
    {
        if (image.isNull())
            return false;
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, 0)) > 0
                || qAlpha(image.pixel(x, image.height() - 1)) > 0) {
                return true;
            }
        }
        return false;
    }

    QRect visibleBounds(const QImage &image) const
    {
        QRect bounds;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 0) {
                    const QRect pixelRect(x, y, 1, 1);
                    bounds = bounds.isNull() ? pixelRect : bounds.united(pixelRect);
                }
            }
        }
        return bounds;
    }

    // Length in pixels of the longest run of contiguous ink pixels in any single
    // row within [yStart, yEnd). Solid horizontal rules that MicroTeX draws — a
    // fraction bar or a radical vinculum — surface here as the widest run, which
    // lets the geometry tests assert those rules actually span their content
    // rather than only checking the metric numbers.
    int widestHorizontalInkRun(const QImage &image, int yStart = -1,
                               int yEnd = -1) const
    {
        if (image.isNull())
            return 0;
        const int y0 = yStart < 0 ? 0 : qBound(0, yStart, image.height());
        const int y1 = yEnd < 0 ? image.height() : qBound(0, yEnd, image.height());
        int widest = 0;
        for (int y = y0; y < y1; ++y) {
            int run = 0;
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 0) {
                    ++run;
                    widest = qMax(widest, run);
                } else {
                    run = 0;
                }
            }
        }
        return widest;
    }

    // Thickness in pixels of the widest solid horizontal rule: the longest
    // streak of consecutive rows whose widest ink run reaches at least 90% of
    // the image's overall widest run. Edge rows that antialiasing leaves
    // mostly transparent are excluded by the alpha threshold.
    int solidRuleThickness(const QImage &image, int alphaThreshold = 128) const
    {
        if (image.isNull())
            return 0;
        QVector<int> runs(image.height(), 0);
        int globalWidest = 0;
        for (int y = 0; y < image.height(); ++y) {
            int run = 0;
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) >= alphaThreshold) {
                    ++run;
                    runs[y] = qMax(runs[y], run);
                } else {
                    run = 0;
                }
            }
            globalWidest = qMax(globalWidest, runs[y]);
        }
        const int ruleWidth = qRound(globalWidest * 0.9);
        int thickness = 0;
        int streak = 0;
        for (int y = 0; y < image.height(); ++y) {
            if (runs[y] >= ruleWidth) {
                ++streak;
                thickness = qMax(thickness, streak);
            } else {
                streak = 0;
            }
        }
        return thickness;
    }

    QImage composeCorpusSheet(const QVector<RenderedCorpusEntry> &entries,
                              const QString &title) const
    {
        QFont titleFont;
        titleFont.setPointSize(18);
        titleFont.setBold(true);
        QFont labelFont;
        labelFont.setPointSize(10);
        QFont texFont(QStringLiteral("monospace"));
        texFont.setStyleHint(QFont::Monospace);
        texFont.setPointSize(9);

        const QFontMetrics titleMetrics(titleFont);
        const QFontMetrics labelMetrics(labelFont);
        const QFontMetrics texMetrics(texFont);
        const int margin = 24;
        const int gutter = 24;
        const int labelWidth = 430;
        const int rowGap = 18;
        const int titleGap = 20;
        int formulaWidth = 1;
        int totalHeight = margin + titleMetrics.height() + titleGap + margin;

        for (const RenderedCorpusEntry &entry : entries) {
            const QImage &image = entry.png;
            formulaWidth = qMax(formulaWidth, image.width());
            const int textHeight = labelMetrics.height() + texMetrics.height() + 6;
            totalHeight += qMax(image.height(), textHeight) + rowGap;
        }

        const int width = qMax(900, margin * 2 + labelWidth + gutter
                                        + formulaWidth);
        QImage sheet(width, totalHeight, QImage::Format_ARGB32);
        sheet.fill(Qt::white);

        QPainter painter(&sheet);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        int y = margin;
        painter.setPen(Qt::black);
        painter.setFont(titleFont);
        painter.drawText(QRect(margin, y, width - margin * 2,
                               titleMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, title);
        y += titleMetrics.height() + titleGap;

        for (int i = 0; i < entries.size(); ++i) {
            const RenderedCorpusEntry &entry = entries.at(i);
            const QImage &image = entry.png;
            const int textHeight = labelMetrics.height() + texMetrics.height() + 6;
            const int rowHeight = qMax(image.height(), textHeight);
            const QRect labelRect(margin, y, labelWidth, labelMetrics.height());
            const QRect texRect(margin, y + labelMetrics.height() + 6,
                                labelWidth, texMetrics.height());

            painter.setFont(labelFont);
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("%1. %2")
                                 .arg(i + 1)
                                 .arg(entry.source.title));
            painter.setFont(texFont);
            painter.drawText(texRect, Qt::AlignLeft | Qt::AlignVCenter,
                             texMetrics.elidedText(entry.source.tex,
                                                   Qt::ElideRight, labelWidth));

            const int imageX = margin + labelWidth + gutter;
            const int imageY = y + (rowHeight - image.height()) / 2;
            painter.drawImage(QPoint(imageX, imageY), image);
            y += rowHeight + rowGap;
        }

        return sheet;
    }

    QImage combinedReferencePages() const
    {
        const QDir dir(newtxReferenceDir());
        const QImage page1(dir.absoluteFilePath(QStringLiteral("refs-1.png")));
        const QImage page2(dir.absoluteFilePath(QStringLiteral("refs-2.png")));
        if (page1.isNull() || page2.isNull())
            return QImage();

        const int gap = 24;
        const int width = qMax(page1.width(), page2.width());
        const int height = page1.height() + gap + page2.height();
        QImage combined(width, height, QImage::Format_ARGB32);
        combined.fill(Qt::white);

        QPainter painter(&combined);
        painter.drawImage(QPoint((width - page1.width()) / 2, 0), page1);
        painter.drawImage(QPoint((width - page2.width()) / 2,
                                 page1.height() + gap),
                          page2);
        return combined;
    }

    QImage composeReferenceComparison(const QImage &reference,
                                      const QImage &cmPng,
                                      const QString &modeLabel) const
    {
        if (reference.isNull() || cmPng.isNull())
            return QImage();

        const int panelHeight = 1800;
        const QImage refScaled = reference.scaledToHeight(
            panelHeight, Qt::SmoothTransformation);
        const QImage pngScaled = cmPng.scaledToHeight(
            panelHeight, Qt::SmoothTransformation);

        QFont headerFont;
        headerFont.setPointSize(12);
        headerFont.setBold(true);
        const QFontMetrics headerMetrics(headerFont);
        const int margin = 24;
        const int gutter = 24;
        const int headerHeight = headerMetrics.height();
        const int width = margin * 2 + refScaled.width() + pngScaled.width()
                          + gutter;
        const int height = margin * 2 + headerHeight + 12 + panelHeight;

        QImage comparison(width, height, QImage::Format_ARGB32);
        comparison.fill(Qt::white);

        QPainter painter(&comparison);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setFont(headerFont);
        painter.setPen(Qt::black);

        int x = margin;
        const int y = margin + headerHeight + 12;
        const auto drawPanel = [&](const QString &header, const QImage &image) {
            painter.drawText(QRect(x, margin, image.width(), headerHeight),
                             Qt::AlignLeft | Qt::AlignVCenter, header);
            painter.drawImage(QPoint(x, y), image);
            x += image.width() + gutter;
        };

        drawPanel(QStringLiteral("LaTeX NewTX/XCharter reference"), refScaled);
        drawPanel(modeLabel + QStringLiteral(" PNG"), pngScaled);

        return comparison;
    }

private slots:
    // MicroTeX sizes a formula to its advance width, which is not where its
    // glyphs stop painting: the italic `f` carries its tail left of the origin
    // and its hook right of the advance, and a script-size `f` overhangs by a
    // fifth of the text size. Rendered into a raster exactly the advance wide,
    // those parts were cut off — `$f$` in a note lost the ends of the letter.
    // The side-bearing margin is what keeps them inside the image.
    void sideBearingMarginKeepsGlyphsInsideTheRaster()
    {
        const QStringList corpus = {
            QStringLiteral("f"), QStringLiteral("f(x)"), QStringLiteral("A_f"),
            QStringLiteral("x^2"), QStringLiteral("g"), QStringLiteral("y"),
            QStringLiteral("\\frac{f}{g}"),
            QStringLiteral("\\int_0^\\infty x^2 dx"),
        };
        for (const QString &tex : corpus) {
            for (int size : {15, 17, 20, 32}) {
                const int pad = MathRenderer::sideBearingPaddingPx(size);
                QVERIFY(pad > 0);
                QString error;
                const QImage image = MathRenderer::render(
                    tex, size, QColor(Qt::black), 1.0, &error, 2, pad);
                QVERIFY2(!image.isNull(),
                         qPrintable(QStringLiteral("render failed for '%1': %2")
                                        .arg(tex, error)));
                int leftInk = 0;
                int rightInk = 0;
                for (int y = 0; y < image.height(); ++y) {
                    if (qAlpha(image.pixel(0, y)) > 0)
                        ++leftInk;
                    if (qAlpha(image.pixel(image.width() - 1, y)) > 0)
                        ++rightInk;
                }
                QVERIFY2(leftInk == 0 && rightInk == 0,
                         qPrintable(QStringLiteral(
                             "'%1' at %2px paints on its own edge "
                             "(left %3, right %4 pixels)")
                                        .arg(tex).arg(size)
                                        .arg(leftInk).arg(rightInk)));
            }
        }
    }

    // The margin is transparent padding, not a re-layout: the formula keeps
    // its measured width and gains the margin on each side, so a caller that
    // shifts the image left by the padding puts it back where the unpadded
    // one sat.
    void sideBearingMarginOnlyWidensTheRaster()
    {
        const QString tex = QStringLiteral("\\int_0^\\infty x^2 dx");
        const int size = 20;
        const int pad = MathRenderer::sideBearingPaddingPx(size);
        const QImage bare = MathRenderer::render(tex, size, QColor(Qt::black),
                                                 1.0, nullptr, 2, 0);
        const QImage padded = MathRenderer::render(tex, size, QColor(Qt::black),
                                                   1.0, nullptr, 2, pad);
        QVERIFY(!bare.isNull() && !padded.isNull());
        QCOMPARE(padded.width(), bare.width() + 2 * pad);
        QCOMPARE(padded.height(), bare.height());
        // The formula itself is rasterized identically, `pad` further right:
        // every pixel of the unpadded image reappears at the same offset.
        // (Only "reappears" — the unpadded raster is the one losing the
        // overhang at its edges, which is the defect this margin exists for.)
        for (int y = 0; y < bare.height(); ++y) {
            for (int x = 0; x < bare.width(); ++x)
                QCOMPARE(padded.pixel(x + pad, y), bare.pixel(x, y));
        }
    }

    void overlongFormulaIsRefusedNotRasterized()
    {
        QElapsedTimer t;
        t.start();
        QString error;
        const QImage image = MathRenderer::render(
            QStringLiteral("x").repeated(400000), 20, QColor(Qt::black), 1.0,
            &error, 0);
        const qint64 ms = t.elapsed();
        QVERIFY2(image.isNull(), "an over-long formula must not rasterize");
        QVERIFY2(!error.isEmpty(), "the refusal must say why");
        QVERIFY2(ms < 1000, qPrintable(QStringLiteral("took %1 ms").arg(ms)));
        // The same source is refused by the validity check, so the block shows
        // an error rather than silently rendering nothing.
        QVERIFY(!MathRenderer::errorFor(
                     QStringLiteral("x").repeated(400000)).isEmpty());
    }

    void formulaAtTheLengthLimitStillRenders()
    {
        QString error;
        const QImage image = MathRenderer::render(
            QStringLiteral("x").repeated(4000), 20, QColor(Qt::black), 1.0,
            &error, 0);
        QVERIFY2(!image.isNull(), qPrintable(error));
    }

    // Text size and device pixel ratio multiply into the same backing store.
    void hugeDevicePixelRatioIsBounded()
    {
        QString error;
        const QImage image = MathRenderer::render(QStringLiteral("x^2"), 20,
                                                  QColor(Qt::black), 1000.0,
                                                  &error, 0);
        const qint64 pixels = qint64(image.width()) * image.height();
        QVERIFY2(pixels <= Diagram::kMaxRasterPixels,
                 qPrintable(QStringLiteral("raster %1 x %2 exceeds the budget")
                                .arg(image.width()).arg(image.height())));
    }

    void hugeTextSizeIsBounded()
    {
        QString error;
        const QImage image = MathRenderer::render(QStringLiteral("x^2"), 100000,
                                                  QColor(Qt::black), 1.0,
                                                  &error, 0);
        const qint64 pixels = qint64(image.width()) * image.height();
        QVERIFY2(pixels <= Diagram::kMaxRasterPixels,
                 qPrintable(QStringLiteral("raster %1 x %2 exceeds the budget")
                                .arg(image.width()).arg(image.height())));
    }

    void availableCommandsEnumerates()
    {
        // The math-command menu's completion corpus: symbols, builtin
        // macros, and the NewTX additions all enumerate; internal registry
        // names (matrix@@env and friends) never do.
        const QStringList commands = MathRenderer::availableCommands();
        QVERIFY2(commands.size() > 300,
                 qPrintable(QStringLiteral("suspiciously small corpus: %1")
                                .arg(commands.size())));
        QVERIFY(commands.contains(QStringLiteral("frac")));
        QVERIFY(commands.contains(QStringLiteral("alpha")));
        QVERIFY(commands.contains(QStringLiteral("sum")));
        QVERIFY(commands.contains(QStringLiteral("vv")));  // NewTX macro
        for (const QString &name : commands)
            QVERIFY2(!name.contains(QLatin1Char('@')),
                     qPrintable(QStringLiteral("internal name leaked: %1")
                                    .arg(name)));
        // Sorted and duplicate-free (a stable corpus for the model's merge).
        QVERIFY(std::is_sorted(commands.cbegin(), commands.cend()));
        QCOMPARE(QSet<QString>(commands.cbegin(), commands.cend()).size(),
                 commands.size());
    }

    void renderRecoversAfterParseError()
    {
        // Session trap seen on Windows 2026-07-19: after a failed parse of
        // an invalid intermediate string (typing "\e..." mid-completion), a
        // previously fine expression stopped rendering for the rest of the
        // session, surviving note switches (the block Images set cache:
        // false) and healing only on restart. Pin the recovery contract:
        // a parse error must not poison later renders of valid input.
        QString error;
        const QString good = QStringLiteral("\\intop_0^\\infty");
        QImage before = MathRenderer::render(good, 20, Qt::black, 1.0,
                                             &error, 2);
        QVERIFY2(!before.isNull(), qPrintable(error));

        // The user's actual intermediate strings while "\exp" was being
        // typed under the completion menu, then a few classic invalids.
        const QStringList invalids = {
            QStringLiteral("\\intop_0^\\infty \\e"),
            QStringLiteral("\\intop_0^\\infty \\ex"),
            QStringLiteral("\\intop_0^\\infty \\exp"),
            QStringLiteral("\\frac{1}"),
            QStringLiteral("{unbalanced"),
            QStringLiteral("\\definitelynotacommand"),
        };
        for (const QString &bad : invalids) {
            QString badError;
            MathRenderer::render(bad, 20, Qt::black, 1.0, &badError, 2);
            // Whether each fails is MicroTeX's business; what matters is
            // the good expression must still render afterwards.
            QImage after = MathRenderer::render(good, 20, Qt::black, 1.0,
                                                &error, 2);
            QVERIFY2(!after.isNull(),
                     qPrintable(QStringLiteral("'%1' poisoned the engine: %2")
                                    .arg(bad, error)));
            QCOMPARE(after.size(), before.size());
        }
    }

    void resourceRootResolves()
    {
        // The path the engine initializes against must exist and hold the
        // resource marker; a wrong path is the spike's flagged failure mode.
        const QString root = MathRenderer::resourceRoot();
        QVERIFY(!root.isEmpty());
        QVERIFY2(QDir(root).exists(),
                 qPrintable(QStringLiteral("res root missing: %1").arg(root)));
    }

    void rendersCanonicalExpressions_data()
    {
        QTest::addColumn<QString>("tex");
        QTest::addColumn<QString>("file");
        QTest::newRow("power")    << "x^2"              << "math_render_01_power.png";
        QTest::newRow("fraction") << "\\frac{a}{b}"     << "math_render_02_fraction.png";
        QTest::newRow("integral") << "\\int_0^1 x\\,dx" << "math_render_03_integral.png";
        QTest::newRow("group")    << "\\overgroup{AB}"  << "math_render_04_group.png";
    }

    void rendersCanonicalExpressions()
    {
        QFETCH(QString, tex);
        QFETCH(QString, file);
        QString error;
        const QImage image = MathRenderer::render(tex, 48, QColor(Qt::black),
                                                  1.0, &error);
        QVERIFY2(!image.isNull(),
                 qPrintable(QStringLiteral("render failed for '%1': %2")
                                .arg(tex, error)));
        QVERIFY(image.width() > 4);
        QVERIFY(image.height() > 4);
        QVERIFY(error.isEmpty());
        QVERIFY(image.save(shotDir() + QLatin1Char('/') + file));

        // A white-background, upscaled copy so the black-on-transparent glyphs
        // are legible when the frames are reviewed by eye.
        const int scale = qMax(1, 200 / qMax(1, image.height()));
        QImage review(image.width() * scale, image.height() * scale,
                      QImage::Format_ARGB32);
        review.fill(Qt::white);
        {
            QPainter p(&review);
            p.drawImage(QRect(0, 0, review.width(), review.height()), image);
        }
        review.save(shotDir() + QStringLiteral("/white_") + file);
    }

    void foregroundColorApplies()
    {
        // Rendering the same expression in two colors must differ pixel-wise.
        const QImage red = MathRenderer::render("x", 48, QColor(Qt::red));
        const QImage blue = MathRenderer::render("x", 48, QColor(Qt::blue));
        QVERIFY(!red.isNull());
        QVERIFY(!blue.isNull());
        QCOMPARE(red.size(), blue.size());
        QVERIFY(red != blue);
    }

    void dprRenderUsesPhysicalPixels()
    {
        QString error1;
        const QImage one = MathRenderer::render("x^2", 18, QColor(Qt::black),
                                                1.0, &error1);
        QString error2;
        const QImage two = MathRenderer::render("x^2", 18, QColor(Qt::black),
                                                2.0, &error2);
        QVERIFY2(!one.isNull(), qPrintable(error1));
        QVERIFY2(!two.isNull(), qPrintable(error2));
        QCOMPARE(one.devicePixelRatio(), 1.0);
        QCOMPARE(two.devicePixelRatio(), 2.0);
        QCOMPARE(two.size(), QSize(one.width() * 2, one.height() * 2));
        QCOMPARE(two.deviceIndependentSize(), QSizeF(one.size()));

        // The DPR-2 image must contain the same drawing at 2x, not a
        // magnified crop: QPainter already applies the image's device pixel
        // ratio, so an extra painter scale would clip the formula to its
        // top-left quadrant (the bug behind broken exported math PNGs).
        const QRect ink1 = visibleBounds(one);
        const QRect ink2 = visibleBounds(two);
        const int tolerance = 3;
        QVERIFY2(qAbs(ink2.width() - ink1.width() * 2) <= tolerance,
                 qPrintable(QStringLiteral("dpr2 ink width %1 vs dpr1 %2")
                                .arg(ink2.width()).arg(ink1.width())));
        QVERIFY2(qAbs(ink2.height() - ink1.height() * 2) <= tolerance,
                 qPrintable(QStringLiteral("dpr2 ink height %1 vs dpr1 %2")
                                .arg(ink2.height()).arg(ink1.height())));
        QVERIFY2(qAbs(ink2.left() - ink1.left() * 2) <= tolerance
                     && qAbs(ink2.top() - ink1.top() * 2) <= tolerance,
                 qPrintable(QStringLiteral("dpr2 ink origin %1,%2 vs dpr1 %3,%4")
                                .arg(ink2.left()).arg(ink2.top())
                                .arg(ink1.left()).arg(ink1.top())));
    }

    void verticalPaddingAddsTransparentLogicalHeight()
    {
        QString error1;
        const QImage plain = MathRenderer::render("x^2", 18, QColor(Qt::black),
                                                  1.0, &error1);
        QString error2;
        const QImage padded = MathRenderer::render("x^2", 18, QColor(Qt::black),
                                                   1.0, &error2, 3);
        QVERIFY2(!plain.isNull(), qPrintable(error1));
        QVERIFY2(!padded.isNull(), qPrintable(error2));
        QCOMPARE(padded.width(), plain.width());
        QCOMPARE(padded.height(), plain.height() + 6);
        QCOMPARE(padded.deviceIndependentSize(),
                 QSizeF(plain.width(), plain.height() + 6));
    }

    void directPaintProducesVisiblePixels()
    {
        const MathRenderer::Metrics metrics = MathRenderer::measure("x^2", 18);
        QVERIFY(metrics.valid);
        QImage image(qCeil(metrics.width) + 2, qCeil(metrics.height) + 2,
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QString error;
        {
            QPainter painter(&image);
            QVERIFY2(MathRenderer::paint(&painter, "x^2", 18, QColor(Qt::black),
                                         QPointF(1, 1), &error),
                     qPrintable(error));
        }

        QVERIFY(error.isEmpty());
        QVERIFY(hasVisiblePixel(image));
    }

    void directPaintReportsErrors()
    {
        QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QString error;
        {
            QPainter painter(&image);
            QVERIFY(!MathRenderer::paint(&painter, "}", 18, QColor(Qt::black),
                                         QPointF(), &error));
        }
        QVERIFY(!error.isEmpty());
        QVERIFY(!hasVisiblePixel(image));
    }

    void corpusDimensions_data()
    {
        QTest::addColumn<QString>("tex");
        QTest::addColumn<int>("textSize");

        const QStringList corpus{
            QStringLiteral("x^2"),
            QStringLiteral("E = mc^2"),
            QStringLiteral("a_i^2 + b_i^2"),
            QStringLiteral("\\frac{a}{b}"),
            QStringLiteral("\\frac{1}{1+\\frac{x}{2}}"),
            QStringLiteral("\\int_0^1 x\\,dx"),
            QStringLiteral("\\sum_{i=0}^n i^2"),
            QStringLiteral("\\sqrt{x^2 + y^2}"),
            QStringLiteral("\\alpha + \\beta \\leq \\gamma"),
            QStringLiteral("\\left(\\sum_{i=0}^{n} x_i\\right)^2"),
        };
        const QList<int> sizes{15, 18, 26, 48};
        for (const int size : sizes) {
            for (const QString &tex : corpus) {
                const QString name = QStringLiteral("size%1:%2")
                    .arg(size)
                    .arg(tex.left(36));
                QTest::newRow(qPrintable(name)) << tex << size;
            }
        }
    }

    void corpusDimensions()
    {
        QFETCH(QString, tex);
        QFETCH(int, textSize);

        const MathRenderer::Metrics metrics = MathRenderer::measure(tex, textSize);
        QVERIFY2(metrics.valid,
                 qPrintable(QStringLiteral("measure failed for '%1': %2")
                                .arg(tex, metrics.error)));
        QVERIFY(metrics.error.isEmpty());
        QVERIFY(metrics.width > 0);
        QVERIFY(metrics.height > 0);
        QVERIFY(metrics.baseline > 0);
        QVERIFY(metrics.baseline <= metrics.height);
        QVERIFY(metrics.ascent > 0);
        QVERIFY(metrics.descent >= 0);
        QVERIFY(metrics.depth >= 0);
        QVERIFY(metrics.depth <= metrics.height);
        QVERIFY(qAbs((metrics.ascent + metrics.descent) - metrics.height) <= 0.5);

        QString error;
        const QImage image = MathRenderer::render(tex, textSize, QColor(Qt::black),
                                                  1.0, &error);
        QVERIFY2(!image.isNull(),
                 qPrintable(QStringLiteral("render failed for '%1': %2")
                                .arg(tex, error)));
        QVERIFY(qAbs(metrics.width - image.width()) <= 1.0);
        QVERIFY(qAbs(metrics.height - image.height()) <= 1.0);

        qInfo().noquote()
            << QStringLiteral("MATH_CORPUS_DIM size=%1 tex=\"%2\" image=%3x%4 dpr=%5 baseline=%6 depth=%7")
                   .arg(textSize)
                   .arg(tex)
                   .arg(image.width())
                   .arg(image.height())
                   .arg(image.devicePixelRatio())
                   .arg(metrics.baseline, 0, 'f', 2)
                   .arg(metrics.depth, 0, 'f', 2);
    }

    void tallDelimiterAndRadicalGeometry()
    {
        const int textSize = 42;
        const QString content = QStringLiteral("\\frac{1}{1+\\frac{x}{2}}");
        const QString fenced = QStringLiteral("\\left(%1\\right)").arg(content);
        const QString radical = QStringLiteral("\\sqrt{%1}").arg(content);

        const MathRenderer::Metrics contentMetrics =
            MathRenderer::measure(content, textSize);
        const MathRenderer::Metrics smallFencedMetrics =
            MathRenderer::measure(QStringLiteral("\\left(x\\right)"), textSize);
        const MathRenderer::Metrics fencedMetrics =
            MathRenderer::measure(fenced, textSize);
        const MathRenderer::Metrics radicalMetrics =
            MathRenderer::measure(radical, textSize);

        QVERIFY2(contentMetrics.valid, qPrintable(contentMetrics.error));
        QVERIFY2(smallFencedMetrics.valid, qPrintable(smallFencedMetrics.error));
        QVERIFY2(fencedMetrics.valid, qPrintable(fencedMetrics.error));
        QVERIFY2(radicalMetrics.valid, qPrintable(radicalMetrics.error));
        QVERIFY(fencedMetrics.height >= contentMetrics.height);
        QVERIFY(fencedMetrics.width > contentMetrics.width);
        QVERIFY(fencedMetrics.height > smallFencedMetrics.height * 1.25);
        QVERIFY(radicalMetrics.height >= contentMetrics.height);
        QVERIFY(radicalMetrics.width > contentMetrics.width);

        const int padding = pngVerticalPadding(textSize);
        QString fencedError;
        const QImage fencedImage = MathRenderer::render(
            fenced, textSize, QColor(Qt::black), 1.0, &fencedError, padding);
        QVERIFY2(!fencedImage.isNull(), qPrintable(fencedError));
        QVERIFY(hasVisiblePixel(fencedImage));
        QVERIFY(!hasVisiblePixelOnHorizontalEdge(fencedImage));
        QVERIFY(visibleBounds(fencedImage).height()
                >= qRound(contentMetrics.height * 0.85));

        QString radicalError;
        const QImage radicalImage = MathRenderer::render(
            radical, textSize, QColor(Qt::black), 1.0, &radicalError, padding);
        QVERIFY2(!radicalImage.isNull(), qPrintable(radicalError));
        QVERIFY(hasVisiblePixel(radicalImage));
        QVERIFY(!hasVisiblePixelOnHorizontalEdge(radicalImage));
        QVERIFY(visibleBounds(radicalImage).height()
                >= qRound(contentMetrics.height * 0.85));
    }

    // The fraction rule must be at least as wide as
    // the wider of numerator and denominator. The earlier metric checks compare
    // reported box sizes; this asserts the drawn bar's actual ink extent, so a
    // regression that shortens or drops the rule is caught by eye-independent
    // pixels. Runs in every font mode (no skip) — the rule geometry is shared.
    void fractionBarSpansWiderRow()
    {
        const int textSize = 48;
        const int padding = pngVerticalPadding(textSize);

        // Numerator deliberately far wider than the denominator, so the bar
        // width is dictated by the numerator side.
        const QString numerator = QStringLiteral("x+y+z+w");
        const QString denominator = QStringLiteral("k");
        const QString fraction =
            QStringLiteral("\\frac{%1}{%2}").arg(numerator, denominator);

        QString numError, denError, fracError;
        const QImage numImage = MathRenderer::render(
            numerator, textSize, QColor(Qt::black), 1.0, &numError, padding);
        const QImage denImage = MathRenderer::render(
            denominator, textSize, QColor(Qt::black), 1.0, &denError, padding);
        const QImage fracImage = MathRenderer::render(
            fraction, textSize, QColor(Qt::black), 1.0, &fracError, padding);
        QVERIFY2(!numImage.isNull(), qPrintable(numError));
        QVERIFY2(!denImage.isNull(), qPrintable(denError));
        QVERIFY2(!fracImage.isNull(), qPrintable(fracError));

        const int numInk = visibleBounds(numImage).width();
        const int denInk = visibleBounds(denImage).width();
        const int barRun = widestHorizontalInkRun(fracImage);

        qInfo().noquote()
            << QStringLiteral("FRACTION_BAR num=%1 den=%2 bar=%3 img=%4")
                   .arg(numInk).arg(denInk).arg(barRun).arg(fracImage.width());

        // The solid rule is the widest contiguous horizontal ink run, and it
        // spans at least the wider side's ink.
        QVERIFY2(barRun >= qMax(numInk, denInk),
                 qPrintable(QStringLiteral("bar run %1 < wider side %2")
                                .arg(barRun).arg(qMax(numInk, denInk))));
        // It cannot exceed the rendered fraction's own pixel width.
        QVERIFY(barRun <= fracImage.width());
    }

    // The bounding box of a radical must contain its
    // radicand, i.e. the radical sign widens it on the left and the vinculum
    // adds a horizontal rule across the top of the radicand. Asserting the drawn
    // vinculum covers the radicand width catches a dropped/short overbar that
    // the metric-only checks would miss. Runs in every font mode (no skip).
    void radicalVinculumCoversRadicand()
    {
        const int textSize = 48;
        const int padding = pngVerticalPadding(textSize);
        const QString radicand = QStringLiteral("x^2 + y^2");
        const QString radical = QStringLiteral("\\sqrt{%1}").arg(radicand);

        QString radicandError, radicalError;
        const QImage radicandImage = MathRenderer::render(
            radicand, textSize, QColor(Qt::black), 1.0, &radicandError, padding);
        const QImage radicalImage = MathRenderer::render(
            radical, textSize, QColor(Qt::black), 1.0, &radicalError, padding);
        QVERIFY2(!radicandImage.isNull(), qPrintable(radicandError));
        QVERIFY2(!radicalImage.isNull(), qPrintable(radicalError));

        const QRect radicandBounds = visibleBounds(radicandImage);
        const QRect radicalBounds = visibleBounds(radicalImage);

        // Radical sign on the left plus vinculum on top make it wider and taller
        // than the bare radicand.
        QVERIFY(radicalBounds.width() > radicandBounds.width());
        QVERIFY(radicalBounds.height() > radicandBounds.height());

        // The vinculum is a solid rule across the top of the radical; look for
        // it in the top band of the radical ink.
        const int band = qMax(1, qRound(radicalBounds.height() * 0.30));
        const int vinculumRun = widestHorizontalInkRun(
            radicalImage, radicalBounds.top(), radicalBounds.top() + band);

        qInfo().noquote()
            << QStringLiteral("RADICAL rad=%1x%2 sqrt=%3x%4 vinculum=%5 band=%6")
                   .arg(radicandBounds.width()).arg(radicandBounds.height())
                   .arg(radicalBounds.width()).arg(radicalBounds.height())
                   .arg(vinculumRun).arg(band);

        QVERIFY2(vinculumRun >= qRound(radicandBounds.width() * 0.9),
                 qPrintable(QStringLiteral("vinculum run %1 < 0.9*radicand %2")
                                .arg(vinculumRun)
                                .arg(qRound(radicandBounds.width() * 0.9))));
    }

    // TeX takes the math layout constants from
    // fontdimens of the symbol and extension fonts, so the generated NewTX
    // mode must use ntxexx's defaultrulethickness (0.056em) instead of
    // Computer Modern's 0.040em. The fraction bar is the most direct pixel
    // witness: its drawn thickness must track the active font family's rule
    // thickness. Runs in every mode with a mode-specific expectation.
    void fractionRuleThicknessTracksFontConstants()
    {
        const int textSize = 192;
        const int padding = pngVerticalPadding(textSize);
        const QString fraction = QStringLiteral("\\frac{x+y+z+w}{k}");

        QString error;
        const QImage image = MathRenderer::render(
            fraction, textSize, QColor(Qt::black), 1.0, &error, padding);
        QVERIFY2(!image.isNull(), qPrintable(error));

        // MicroTeX keeps fonts at 1pt and scales by the formula text size with
        // PIXELS_PER_POINT == 1, so one em is textSize pixels and the rule is
        // defaultrulethickness * textSize pixels.
        const float ruleThicknessEm =
            newtxCharterMode() ? 0.056f : 0.04f;
        const float expected = ruleThicknessEm * textSize;
        const int measured = solidRuleThickness(image);

        qInfo().noquote()
            << QStringLiteral("FRACTION_RULE thickness=%1 expected=%2")
                   .arg(measured).arg(expected, 0, 'f', 2);

        QVERIFY2(qAbs(measured - expected) <= 2.0f,
                 qPrintable(QStringLiteral("rule thickness %1px, expected %2px")
                                .arg(measured).arg(expected, 0, 'f', 2)));
    }

    // Optical sizes: lmsntxsy.fd gives the symbols
    // family real optical script masters — ntxsy7 for script and ntxsy5 for
    // scriptscript at a 10pt base. Those masters are drawn wider than a
    // linearly scaled ntxsy: \in is 0.556em in ntxsy but 0.596em in both
    // ntxsy7 and ntxsy5 (7.2% wider). Asserting the rendered script-size ink
    // is wider than the plainly scaled text-size ink proves the generated
    // mode actually swaps masters instead of only scaling.
    void generatedOpticalScriptMastersWidenScriptSymbols()
    {
        if (!newtxCharterMode())
            QSKIP("NewTX charter mode disabled (KVIT_MATH_FONT=cm)");

        const int textSize = 128;

        // Reported metric widths come straight from the active font tables,
        // so they witness the master swap without antialiasing noise: pure
        // scaling of ntxsy would give exactly 0.70 and 0.50 of the text-style
        // width, while the ntxsy7/ntxsy5 masters predict 0.70 * 1.072 = 0.750
        // and 0.50 * 1.072 = 0.536.
        const MathRenderer::Metrics textMetrics = MathRenderer::measure(
            QStringLiteral("\\in"), textSize);
        const MathRenderer::Metrics scriptMetrics = MathRenderer::measure(
            QStringLiteral("{\\scriptstyle\\in}"), textSize);
        const MathRenderer::Metrics scriptScriptMetrics = MathRenderer::measure(
            QStringLiteral("{\\scriptscriptstyle\\in}"), textSize);
        QVERIFY2(textMetrics.valid, qPrintable(textMetrics.error));
        QVERIFY2(scriptMetrics.valid, qPrintable(scriptMetrics.error));
        QVERIFY2(scriptScriptMetrics.valid,
                 qPrintable(scriptScriptMetrics.error));
        QVERIFY(textMetrics.width > 0);

        const qreal scriptRatio = scriptMetrics.width / textMetrics.width;
        const qreal scriptScriptRatio =
            scriptScriptMetrics.width / textMetrics.width;

        qInfo().noquote()
            << QStringLiteral("OPTICAL_IN text=%1 script=%2 (%3) ss=%4 (%5)")
                   .arg(textMetrics.width, 0, 'f', 2)
                   .arg(scriptMetrics.width, 0, 'f', 2)
                   .arg(scriptRatio, 0, 'f', 4)
                   .arg(scriptScriptMetrics.width, 0, 'f', 2)
                   .arg(scriptScriptRatio, 0, 'f', 4);

        QVERIFY2(qAbs(scriptRatio - 0.750) <= 0.01,
                 qPrintable(QStringLiteral("script \\in width ratio %1, "
                                           "ntxsy7 predicts 0.750")
                                .arg(scriptRatio, 0, 'f', 4)));
        QVERIFY2(qAbs(scriptScriptRatio - 0.536) <= 0.01,
                 qPrintable(QStringLiteral("scriptscript \\in width ratio %1, "
                                           "ntxsy5 predicts 0.536")
                                .arg(scriptScriptRatio, 0, 'f', 4)));
    }

    // Qt cannot draw several codepoints below 33: NUL is truncated during
    // wstring conversion and 9/10/12/13 shape as inkless whitespace, so TeX
    // slots such as \Gamma (zchmia 0), minus (ntxsy 0), \beta (zchmi 12), and
    // \Psi (zchmi 9) silently vanished. The generated fonts alias those slots
    // at U+E000+slot and the draw boundary remaps them; this asserts each
    // affected glyph actually leaves ink.
    void generatedLowSlotGlyphsProduceInk()
    {
        if (!newtxCharterMode())
            QSKIP("NewTX charter mode disabled (KVIT_MATH_FONT=cm)");

        const int textSize = 48;
        const int padding = pngVerticalPadding(textSize);
        const QStringList low = {
            QStringLiteral("\\Gamma"),        // zchmia slot 0
            QStringLiteral("\\beta"),         // zchmi slot 12
            QStringLiteral("\\gamma"),        // zchmi slot 13
            QStringLiteral("\\Psi"),          // zchmia slot 9
            QStringLiteral("\\Omega"),        // zchmia slot 10
            QStringLiteral("{\\scriptstyle-1}"),  // ntxsy7 slot 0 via scripts
        };
        for (const QString &tex : low) {
            QString error;
            const QImage image = MathRenderer::render(
                tex, textSize, QColor(Qt::black), 1.0, &error, padding);
            QVERIFY2(!image.isNull(),
                     qPrintable(QStringLiteral("render failed for '%1': %2")
                                    .arg(tex, error)));
            QVERIFY2(hasVisiblePixel(image),
                     qPrintable(QStringLiteral("no ink for '%1' — low-slot "
                                               "glyph dropped by Qt")
                                    .arg(tex)));
        }

        // The exponent minus is the reference-corpus witness: e^{-x^2} must
        // be wider than e^{x^2} by the drawn script-size minus.
        QString withError, withoutError;
        const QImage with = MathRenderer::render(
            QStringLiteral("e^{-x^2}"), textSize, QColor(Qt::black), 1.0,
            &withError, padding);
        const QImage without = MathRenderer::render(
            QStringLiteral("e^{x^2}"), textSize, QColor(Qt::black), 1.0,
            &withoutError, padding);
        QVERIFY2(!with.isNull(), qPrintable(withError));
        QVERIFY2(!without.isNull(), qPrintable(withoutError));
        QVERIFY2(visibleBounds(with).width() > visibleBounds(without).width(),
                 "script-style minus leaves no ink in e^{-x^2}");
    }

    // Scale: tracing the reference preamble shows
    // every family loads at natural size — operators come from T1/XCharter-TLF
    // (rmdefaultB == rmdefault, so minxcharter's 0.98 factor is never
    // selected) and zchmi/ntxsy/ntxexx load with an empty \ntxmath@scaled. The
    // baked scale is therefore 1.0, and the witness is the relative size of an
    // XCharter roman glyph against a zchmi math italic glyph: TFM heights at
    // scale 1.0 predict ink_height(x_zchmi) / ink_height(x_XCharter) =
    // 0.4470 / 0.4865 = 0.919. A missing or extra scale factor between the
    // text and math families would move this ratio.
    void generatedTextToMathRelativeScaleMatchesTfm()
    {
        if (!newtxCharterMode())
            QSKIP("NewTX charter mode disabled (KVIT_MATH_FONT=cm)");

        const int textSize = 160;
        const int padding = pngVerticalPadding(textSize);

        QString italicError, romanError;
        const QImage italicImage = MathRenderer::render(
            QStringLiteral("x"), textSize, QColor(Qt::black), 1.0,
            &italicError, padding);
        const QImage romanImage = MathRenderer::render(
            QStringLiteral("\\mathrm{x}"), textSize, QColor(Qt::black), 1.0,
            &romanError, padding);
        QVERIFY2(!italicImage.isNull(), qPrintable(italicError));
        QVERIFY2(!romanImage.isNull(), qPrintable(romanError));

        const int italicInk = visibleBounds(italicImage).height();
        const int romanInk = visibleBounds(romanImage).height();
        QVERIFY(romanInk > 0);

        const qreal ratio = qreal(italicInk) / romanInk;
        qInfo().noquote()
            << QStringLiteral("TEXT_MATH_SCALE zchmi=%1 xcharter=%2 ratio=%3")
                   .arg(italicInk).arg(romanInk).arg(ratio, 0, 'f', 4);

        QVERIFY2(qAbs(ratio - 0.919) <= 0.035,
                 qPrintable(QStringLiteral(
                                "zchmi/XCharter x-height ratio %1, TFM predicts "
                                "0.919 at scale 1.0 — a hidden text-to-math "
                                "scale factor moved the families apart")
                                .arg(ratio, 0, 'f', 4)));
    }

    void generatedNewtxSupplementalSymbolsRender_data()
    {
        QTest::addColumn<QString>("tex");

        QTest::newRow("active-relation-defaults")
            << QStringLiteral("\\notin\\quad\\neq\\quad\\coloneq\\quad"
                              "\\eqcolon\\quad\\Perp\\quad\\nPerp");
        QTest::newRow("direct-relation-aliases")
            << QStringLiteral("\\ne\\quad\\neq\\quad\\notin\\quad"
                              "\\colonequals\\quad\\equalscolon");
        QTest::newRow("direct-colon-aliases")
            << QStringLiteral("\\colonapprox\\quad\\coloncolonsim\\quad"
                              "\\coloncolonequals\\quad\\equalscoloncolon");
        QTest::newRow("enhanced-letter-symbols")
            << QStringLiteral("\\hslash\\quad\\hbar\\quad\\lambdaslash"
                              "\\quad\\lambdabar\\quad\\transp\\quad"
                              "\\hermtransp");
        QTest::newRow("newtx-let-aliases")
            << QStringLiteral("\\circledplus\\quad\\circledminus\\quad"
                              "\\circledtimes\\quad\\circledslash\\quad"
                              "\\circleddot");
        QTest::newRow("symbols-c-arrows")
            << QStringLiteral("\\mappedfromchar\\quad\\Mapstochar\\quad"
                              "\\Mmapstochar\\quad\\dashleftarrow\\quad"
                              "\\dashrightarrow");
        QTest::newRow("symbols-c-arrow-aliases")
            << QStringLiteral("\\mapsfrom\\quad\\Mapsfrom\\quad"
                              "\\dasharrow\\quad\\lrJoin");
        QTest::newRow("symbols-c-newtx-mapped-arrows")
            << QStringLiteral("\\mappedfrom\\quad\\Mappedfrom\\quad"
                              "\\mmapsto\\quad\\mmappedfrom\\quad"
                              "\\Mmapsto\\quad\\Mmappedfrom");
        QTest::newRow("symbols-c-newtx-long-mapped-arrows")
            << QStringLiteral("\\longmappedfrom\\quad\\Longmappedfrom\\quad"
                              "\\longmmapsto\\quad\\longmmappedfrom\\quad"
                              "\\Longmmapsto\\quad\\Longmmappedfrom");
        QTest::newRow("symbols-c-delimiters")
            << QStringLiteral("\\left\\lbag\\frac{a}{b}\\right\\rbag"
                              "\\quad\\Lbag\\quad\\Rbag");
        QTest::newRow("symbols-c-operators")
            << QStringLiteral("\\circledless\\quad\\circledgtr\\quad"
                              "\\sqcupplus\\quad\\sqcapplus\\quad\\boxright");
        QTest::newRow("newtx-letterA-declarations")
            << QStringLiteral("\\Zbar\\quad\\Angstrom\\quad\\Euler");
        QTest::newRow("newtx-direct-style-declarations")
            << QStringLiteral("\\frakdotlessi\\quad\\jmathfrak\\quad"
                              "\\bbdotlessi\\quad\\jmathbb\\quad"
                              "\\imathup\\quad\\jmathup");
        QTest::newRow("newtx-upright-greek-aliases")
            << QStringLiteral("\\Gammaup\\quad\\upDelta\\quad\\alphaup"
                              "\\quad\\upbeta\\quad\\upvartheta\\quad"
                              "\\upvarphi\\quad\\upvarkappa\\quad"
                              "\\uppartial");
        QTest::newRow("newtx-italic-greek-aliases")
            << QStringLiteral("\\Gammait\\quad\\itDelta\\quad\\alphait"
                              "\\quad\\itbeta\\quad\\itvartheta\\quad"
                              "\\itvarphi\\quad\\varkappait\\quad"
                              "\\itvarkappa");
        QTest::newRow("newtx-oml-letter-symbols")
            << QStringLiteral("\\leftharpoonup\\quad\\rightharpoondown\\quad"
                              "\\lhook\\quad\\rhook\\quad\\triangleleft"
                              "\\quad\\triangleright\\quad\\flat\\quad"
                              "\\natural\\quad\\sharp\\quad\\smile\\quad"
                              "\\frown\\quad\\ell\\quad\\wp\\quad\\star");
        QTest::newRow("newtx-alt-ordinary-symbols")
            << QStringLiteral("\\forallAlt\\quad\\existsAlt\\quad\\nexists"
                              "\\quad\\nexistsAlt\\quad\\emptysetAlt\\quad"
                              "\\varnothing\\quad\\varg\\quad\\vary\\quad"
                              "\\upvarkappa\\quad\\itvarkappa");
        QTest::newRow("newtx-ams-let-and-hexbox-aliases")
            << QStringLiteral("\\Join\\quad\\Box\\quad\\checkmark\\quad"
                              "\\circledR\\quad\\maltese\\quad\\nni");
        QTest::newRow("newtx-ams-class-helpers")
            << QStringLiteral("\\textsquare\\quad\\openbox\\quad"
                              "\\widebar{AB}");
        QTest::newRow("newtx-ams-lower-declarations")
            << QStringLiteral("\\lvertneqq\\quad\\nleq\\quad\\nparallel"
                              "\\quad\\nleftarrow\\quad\\divideontimes\\quad"
                              "\\Finv\\quad\\mho\\quad\\Bbbk\\quad\\daleth"
                              "\\quad\\ltimes\\quad\\rtimes\\quad"
                              "\\backepsilon\\quad\\triangleq");
        QTest::newRow("newtx-double-bracket-aliases")
            << QStringLiteral("\\left\\lBrack\\frac{a}{b}\\right\\rBrack"
                              "\\quad"
                              "\\left\\dlb\\frac{c}{d}\\right\\drb");
        QTest::newRow("newtx-small-brace-delimiters")
            << QStringLiteral("\\left\\smlbrace\\frac{a}{b}\\right\\smrbrace"
                              "\\quad\\smlbrace x\\smrbrace");
        QTest::newRow("newtx-large-integral-operators")
            << QStringLiteral("\\iint\\quad\\iiint\\quad\\iiiint\\quad"
                              "\\oiint\\quad\\oiiint");
        QTest::newRow("newtx-integral-operator-internals")
            << QStringLiteral("\\intop\\quad\\iintop\\quad\\iiintop\\quad"
                              "\\iiiintop\\quad\\ointop\\quad\\oiintop\\quad"
                              "\\oiiintop");
        QTest::newRow("newtx-integral-variants")
            << QStringLiteral("\\sumint\\quad\\fint\\quad\\sqint\\quad"
                              "\\varointclockwise\\quad\\ointctrclockwise");
        QTest::newRow("newtx-integral-style-internals")
            << QStringLiteral("\\intslop\\quad\\iintslop\\quad\\iiiintslop"
                              "\\quad\\varointclockwiseslop\\quad"
                              "\\intupop\\quad\\iintupop\\quad"
                              "\\iiiintupop\\quad\\varointclockwiseupop");
        QTest::newRow("newtx-small-operators")
            << QStringLiteral("\\smallint\\quad\\smalliint\\quad"
                              "\\smalliiint\\quad\\smallprod\\quad"
                              "\\smallsum\\quad\\smallcoprod");
        QTest::newRow("newtx-large-operator-aliases")
            << QStringLiteral("\\bigcupdot\\quad\\bignplus\\quad"
                              "\\bigcapplus\\quad\\bigsqcupplus\\quad"
                              "\\bigsqcapplus\\quad\\bigtimes\\quad"
                              "\\varprod");
        QTest::newRow("newtx-large-operator-internals")
            << QStringLiteral("\\sumop\\quad\\prodop\\quad\\coprodop\\quad"
                              "\\bigcupop\\quad\\bigcapop\\quad"
                              "\\bigwedgeop\\quad\\bigveeop\\quad"
                              "\\bigcapplusop\\quad\\bigsqcupplusop\\quad"
                              "\\bigsqcapplusop\\quad\\bigtimesop");
        QTest::newRow("newtx-generated-accents")
            << QStringLiteral("\\dddot{x}\\quad\\ddddot{x}\\quad"
                              "\\lvec{AB}\\quad\\lrvec{AB}\\quad"
                              "\\harpoonacc{x}\\quad\\widearc{AB}\\quad"
                              "\\wideOarc{AB}\\quad\\barhat{x}\\quad"
                              "\\hathat{x}");
        QTest::newRow("newtx-adaptive-vector")
            << QStringLiteral("\\vv{AB}\\quad\\txvec{xyz}\\quad"
                              "\\vv*{v}{i}\\quad\\vv*{AB}{n+1}");
        QTest::newRow("newtx-widering-and-small-marks")
            << QStringLiteral("\\widering{ABC}\\quad\\cdotB\\quad\\cdotBB"
                              "\\quad\\circS\\quad\\bulletSSS\\quad"
                              "\\bulletSS\\quad\\bulletS\\quad\\primeS");
        QTest::newRow("newtx-group-macros")
            << QStringLiteral("\\overgroup{AB}\\quad\\undergroup{AB}\\quad"
                              "\\overgroupra{AB}\\quad\\undergroupra{AB}\\quad"
                              "\\overgroupla{AB}\\quad\\undergroupla{AB}\\quad"
                              "\\groupld\\quad\\grouprd\\quad\\grouplua"
                              "\\quad\\grouprua");
        QTest::newRow("newtx-over-under-brace")
            << QStringLiteral("\\overbrace{AB}^{n}\\quad\\underbrace{xy}_{m}");
        QTest::newRow("newtx-brace-fill-pieces")
            << QStringLiteral("\\braceld\\quad\\bracerd\\quad\\bracelu"
                              "\\quad\\braceru\\quad"
                              "\\makeatletter\\br@cext\\makeatother");
        QTest::newRow("newtx-script-and-variant-blackboard")
            << QStringLiteral("\\mathscr{FLx}\\quad\\scrdotlessi\\quad"
                              "\\jmathscr\\quad\\vmathbb{R2z}\\quad"
                              "\\vmathbb{\\Gamma}\\quad\\vmathbb{\\pi}\\quad"
                              "\\vvmathbb{R2z}\\quad\\vvmathbb{\\Pi}\\quad"
                              "\\vvmathbb{\\gamma}");
        QTest::newRow("newtx-upright-script-command")
            << QStringLiteral("\\mathuscr{FLx}\\quad\\mathslscr{FLx}\\quad"
                              "\\mathscr{FLx}");
        QTest::newRow("generated-text-styles")
            << QStringLiteral("\\mathrm{sin}\\quad\\mathit{Rate}\\quad"
                              "\\mathbf{Ab1}\\quad"
                              "\\boldsymbol{x+\\alpha\\leq\\sum}\\quad"
                              "\\mathscr{Lx}");
    }

    void generatedNewtxSupplementalSymbolsRender()
    {
        if (!newtxCharterMode())
            QSKIP("NewTX charter mode disabled (KVIT_MATH_FONT=cm)");

        QFETCH(QString, tex);
        QString error;
        const QImage image = MathRenderer::render(tex, 32, QColor(Qt::black),
                                                  1.0, &error,
                                                  pngVerticalPadding(32));
        QVERIFY2(!image.isNull(),
                 qPrintable(QStringLiteral("render failed for '%1': %2")
                                .arg(tex, error)));
        QVERIFY(hasVisiblePixel(image));
        QVERIFY(!hasVisiblePixelOnHorizontalEdge(image));
    }

    void newtxCharterReferenceCorpusArtifacts()
    {
        const int textSize = 26;
        QVector<RenderedCorpusEntry> rendered;
        rendered.reserve(newtxCharterReferenceCorpus().size());

        for (const CorpusEntry &entry : newtxCharterReferenceCorpus()) {
            QString pngError;
            const QImage png = MathRenderer::render(entry.tex, textSize,
                                                    QColor(Qt::black),
                                                    1.0, &pngError,
                                                    pngVerticalPadding(textSize));
            QVERIFY2(!png.isNull(),
                     qPrintable(QStringLiteral("PNG render failed for '%1': %2")
                                    .arg(entry.tex, pngError)));
            QVERIFY(hasVisiblePixel(png));
            QVERIFY2(!hasVisiblePixelOnHorizontalEdge(png),
                     qPrintable(QStringLiteral("PNG render touches top/bottom edge for '%1'")
                                    .arg(entry.tex)));

            rendered.push_back({entry, png});
        }

        const QString modeLabel = newtxCorpusModeLabel();
        const QString artifactStem = newtxCorpusArtifactStem();
        const QImage pngSheet = composeCorpusSheet(
            rendered,
            QStringLiteral("NewTX/XCharter reference corpus - %1 PNG")
                .arg(modeLabel));
        QVERIFY(!pngSheet.isNull());
        QVERIFY(pngSheet.save(shotDir()
                              + QLatin1Char('/')
                              + artifactStem
                              + QStringLiteral("_corpus_png.png")));

        const QImage reference = combinedReferencePages();
        if (reference.isNull()) {
            QSKIP(qPrintable(
                QStringLiteral("optional NewTX/XCharter reference PNGs are not "
                               "installed in %1")
                    .arg(newtxReferenceDir())));
        }
        const QImage comparison = composeReferenceComparison(reference, pngSheet,
                                                            modeLabel);
        QVERIFY(!comparison.isNull());
        QVERIFY(comparison.save(
            shotDir()
            + QLatin1Char('/')
            + newtxComparisonArtifactName()));
    }

    void malformedExpressionMetricsReportError()
    {
        QCOMPARE(MathRenderer::measure("   ", 18).valid, true);
        const MathRenderer::Metrics unmatched = MathRenderer::measure("}", 18);
        QVERIFY(!unmatched.valid);
        QVERIFY(!unmatched.error.isEmpty());
        const MathRenderer::Metrics amp = MathRenderer::measure("a & b", 18);
        QVERIFY(!amp.valid);
        QVERIFY(!amp.error.isEmpty());
    }

    void validExpressionHasNoError()
    {
        QCOMPARE(MathRenderer::errorFor("x^2"), QString());
        QCOMPARE(MathRenderer::errorFor("\\sum_{i=0}^n i^2"), QString());
        // A blank expression is not an error; it renders nothing.
        QCOMPARE(MathRenderer::errorFor("   "), QString());
    }

    void unicodeSpacesNormalizeBeforeRender()
    {
        // Renderer layer: U+202F, U+00A0, U+2009, U+200A become ASCII
        // spaces before MicroTeX sees the input — defense in depth under
        // the parse-time normalizer,
        // covering math typed or edited directly in the editor.
        const QString unicode = QStringLiteral("x + y - z");
        const QString ascii = QStringLiteral("x + y - z");
        QCOMPARE(MathRenderer::errorFor(unicode), QString());
        const MathRenderer::Metrics u = MathRenderer::measure(unicode, 18);
        const MathRenderer::Metrics a = MathRenderer::measure(ascii, 18);
        QVERIFY(u.valid);
        QCOMPARE(u.width, a.width);
        QCOMPARE(u.height, a.height);
        // Only-unicode-space input is blank, not an error.
        QCOMPARE(MathRenderer::errorFor(QStringLiteral("  ")),
                 QString());
    }

    void malformedExpressionReportsError()
    {
        // MicroTeX is lenient (unknown commands and unterminated groups render
        // as text), but structurally invalid tokens do raise a named parse
        // error rather than rendering nothing — which is what the delegate's
        // error state surfaces (source + message, never blank).
        const QString unmatched = MathRenderer::errorFor("}");
        QVERIFY2(!unmatched.isEmpty(),
                 "a closing brace with no opener should report an error");
        const QString amp = MathRenderer::errorFor("a & b");
        QVERIFY2(!amp.isEmpty(),
                 "an alignment '&' outside array mode should report an error");
    }
};

QTEST_MAIN(TestMathRenderer)
#include "test_mathrenderer.moc"
