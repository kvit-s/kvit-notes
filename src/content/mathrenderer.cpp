// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mathrenderer.h"

#include "diagrams/diagrambudget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QFontMetricsF>
#include <QTextStream>
#include <QUrlQuery>
#include <QByteArray>
#include <QtMath>
#include <memory>

#include <QSet>
#include <algorithm>

#include "latex.h"
#include "platform/qt/graphic_qt.h"
#include "core/macro.h"
#include "atom/atom_char.h"

namespace {

// One process-wide lock around the MicroTeX singleton (global static state,
// not thread-safe); the image provider runs on the Qt Quick loader thread.
QMutex g_mathMutex;
bool g_inited = false;

// Initialize the engine once against the resolved resource root. Must be
// called with g_mathMutex held.
bool ensureInited(QString *error)
{
    if (g_inited)
        return true;
    try {
        tex::LaTeX::init(MathRenderer::resourceRoot().toStdString());
        g_inited = true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    } catch (...) {
        if (error)
            *error = QStringLiteral("MicroTeX initialization failed");
        return false;
    }
    return true;
}

// MicroTeX color is 0xAARRGGBB.
tex::color toTexColor(const QColor &c)
{
    return (static_cast<tex::color>(c.alpha()) << 24)
         | (static_cast<tex::color>(c.red()) << 16)
         | (static_cast<tex::color>(c.green()) << 8)
         | static_cast<tex::color>(c.blue());
}

QString styledTex(const QString &trimmed, bool displayStyle)
{
    if (displayStyle)
        return trimmed;
    return QStringLiteral("\\textstyle{") + trimmed + QLatin1Char('}');
}

// ChatGPT emits U+202F (narrow no-break), U+00A0, U+2009, U+200A around
// operators; MicroTeX chokes on them. The LLM normalizer rewrites them at
// parse time; this is defense in depth for math typed or edited directly
// in the editor.
QString normalizedTex(const QString &tex)
{
    QString out = tex;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        if (c == QChar(0x202F) || c == QChar(0x00A0)
            || c == QChar(0x2009) || c == QChar(0x200A))
            out[i] = QLatin1Char(' ');
    }
    return out.trimmed();
}

enum class TextDrawingMode {
    // Kept for diagnostics. MicroTeX paints 1pt fonts under a painter scale;
    // Qt native text can rasterize those tiny glyphs before scaling.
    NativeText,
    OutlinedText,
};

QColor texQColor(tex::color c)
{
    return QColor(tex::color_r(c), tex::color_g(c), tex::color_b(c),
                  tex::color_a(c));
}

class OutlinedTextGraphics2D : public tex::Graphics2D_qt
{
public:
    explicit OutlinedTextGraphics2D(QPainter *painter)
        : tex::Graphics2D_qt(painter)
    {
    }

    void drawChar(wchar_t c, float x, float y) override
    {
        drawText(std::wstring(1, tex::kvit_remap_generated_low_slot(c)), x, y);
    }

    void drawText(const std::wstring &t, float x, float y) override
    {
        const auto *font = dynamic_cast<const tex::Font_qt *>(getFont());
        if (!font) {
            tex::Graphics2D_qt::drawText(t, x, y);
            return;
        }

        const QString text = tex::wstring_to_QString(t);
        if (text.isEmpty())
            return;

        QPainterPath path;
        path.addText(QPointF(x, y), font->getQFont(), text);

        QPainter *painter = getQPainter();
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(texQColor(getColor()));
        painter->drawPath(path);
        painter->restore();
    }
};

std::unique_ptr<tex::TeXRender> createRender(const QString &trimmed,
                                             float size,
                                             const QColor &fg,
                                             bool displayStyle,
                                             QString *error)
{
    tex::TeXRender *render = nullptr;
    try {
        // A very wide wrap width so a single expression never line-breaks.
        render = tex::LaTeX::parse(styledTex(trimmed, displayStyle).toStdWString(),
                                   1 << 16, size, size / 3.0f,
                                   toTexColor(fg));
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return nullptr;
    } catch (...) {
        if (error)
            *error = QStringLiteral("Unrenderable expression");
        return nullptr;
    }
    if (!render && error)
        *error = QStringLiteral("Unrenderable expression");
    return std::unique_ptr<tex::TeXRender>(render);
}

void drawRender(tex::TeXRender *render, QPainter *painter,
                const QPointF &origin, int verticalPaddingPx,
                TextDrawingMode textMode = TextDrawingMode::NativeText)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->translate(origin);
    if (textMode == TextDrawingMode::OutlinedText) {
        OutlinedTextGraphics2D g2(painter);
        render->draw(g2, 0, qMax(0, verticalPaddingPx));
    } else {
        tex::Graphics2D_qt g2(painter);
        render->draw(g2, 0, qMax(0, verticalPaddingPx));
    }
    painter->restore();
}

} // namespace

namespace MathRenderer {

QString resourceRoot()
{
    // Resolution order (installed packages must not depend on the build
    // machine's source tree):
    //   1. KVIT_MATH_RES env var — explicit override, taken verbatim.
    //   2. A path relative to the executable, per platform layout.
    //   3. The compiled-in source path — the dev/test fallback.
    const QByteArray env = qgetenv("KVIT_MATH_RES");
    if (!env.isEmpty())
        return QString::fromUtf8(env);

    // A candidate counts as a resource root only if it holds the fonts/
    // subtree LaTeX::init() reads; a bare directory match must not shadow
    // the working fallback.
    const auto validRoot = [](const QString &root) {
        return QFileInfo(root + QStringLiteral("/fonts")).isDir();
    };
    if (QCoreApplication::instance()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString candidates[] = {
            // Windows portable/installed: math-res beside the executable.
            appDir + QStringLiteral("/math-res"),
            // Linux FHS install: bin/../share/kvit-notes/math-res.
            appDir + QStringLiteral("/../share/kvit-notes/math-res"),
            // macOS bundle: Contents/MacOS/../Resources/math-res.
            appDir + QStringLiteral("/../Resources/math-res"),
        };
        for (const QString &candidate : candidates) {
            if (validRoot(candidate))
                return QDir::cleanPath(candidate);
        }
    }

#ifdef KVIT_MATH_RES_ROOT
    return QStringLiteral(KVIT_MATH_RES_ROOT);
#else
    return QStringLiteral("res");
#endif
}

int runSelfTest()
{
    // Packaging probe (`kvit-notes --math-selftest`): renders one formula
    // against the resolved resource root and reports it, so the B1.3
    // relocatability acceptance — install, delete the build tree, math still
    // renders — is a scriptable check on every platform rather than a manual
    // GUI inspection. Runs headless; must be called before any QApplication
    // exists.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    static int argc = 1;
    static char arg0[] = "kvit-notes";
    static char *argv[] = {arg0, nullptr};
    QGuiApplication app(argc, argv);

    const QString root = resourceRoot();
    // KVIT_MATH_SELFTEST_TEX overrides the probe expression, so packaging
    // QA can check specific coverage (e.g. alphabets) from the shipped
    // binary without a GUI session.
    QString tex = QString::fromUtf8(qgetenv("KVIT_MATH_SELFTEST_TEX"));
    if (tex.isEmpty())
        tex = QStringLiteral("\\frac{a}{b} + \\sqrt{x^2}");
    QString error;
    const QImage image = render(tex, 20, QColor(Qt::black), 1.0, &error, 2);
    const bool ok = !image.isNull() && error.isEmpty();
    QTextStream out(stdout);
    out << "math-res: " << root << "\n";
    if (ok)
        out << "selftest: OK (" << image.width() << "x" << image.height()
            << ")\n";
    else
        out << "selftest: FAIL ("
            << (error.isEmpty() ? QStringLiteral("null image") : error)
            << ")\n";
    return ok ? 0 : 1;
}

QImage render(const QString &tex, int textSizePx, const QColor &fg,
              qreal dpr, QString *error, int verticalPaddingPx)
{
    if (error)
        error->clear();
    const QString trimmed = normalizedTex(tex);
    if (trimmed.isEmpty())
        return QImage();
    // Layout cost and raster extent both grow with the source, and a note can
    // carry a formula of any length. Past the budget it is an error the block
    // reports, not a buffer the process tries to serve.
    if (trimmed.size() > Diagram::kMaxTexChars) {
        if (error)
            *error = QStringLiteral("Formula is too long (%1 characters; "
                                    "the limit is %2)")
                         .arg(trimmed.size()).arg(Diagram::kMaxTexChars);
        return QImage();
    }

    QMutexLocker locker(&g_mathMutex);
    if (!ensureInited(error))
        return QImage();

    const float size = static_cast<float>(
        qBound(1, textSizePx > 0 ? textSizePx : 20, Diagram::kMaxTextSizePx));
    const auto render = createRender(trimmed, size, fg, true, error);
    if (!render)
        return QImage();

    const int w = qMax(1, render->getWidth());
    const int h = qMax(1, render->getHeight());
    const int vpad = qMax(0, verticalPaddingPx);
    qreal ratio = dpr > 0 ? qMin(dpr, Diagram::kMaxDevicePixelRatio) : 1.0;
    // Formula extent, text size and device pixel ratio all multiply into the
    // backing store, and a note controls the first of them outright. Shrink
    // the ratio until the raster fits the budget rather than asking for a
    // buffer the process cannot serve.
    const qreal logicalW = w;
    const qreal logicalH = h + 2 * vpad;
    const qreal maxRatioByEdge =
        qMin(Diagram::kMaxRasterEdge / qMax(logicalW, 1.0),
             Diagram::kMaxRasterEdge / qMax(logicalH, 1.0));
    const qreal maxRatioByArea = std::sqrt(
        double(Diagram::kMaxRasterPixels)
        / qMax(logicalW * logicalH, 1.0));
    ratio = qMax(0.01, qMin(ratio, qMin(maxRatioByEdge, maxRatioByArea)));
    QImage image(qRound(logicalW * ratio), qRound(logicalH * ratio),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return QImage();
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    {
        // QPainter reads the image's devicePixelRatio into its initial
        // transform, so logical-coordinate drawing already fills the physical
        // canvas. An extra scale here would double-apply the ratio and crop
        // the formula to its magnified top-left quadrant.
        QPainter painter(&image);
        drawRender(render.get(), &painter, QPointF(), vpad,
                   TextDrawingMode::OutlinedText);
    }
    return image;
}

bool paint(QPainter *painter, const QString &tex, int textSizePx,
           const QColor &fg, const QPointF &origin, QString *error,
           int verticalPaddingPx, bool displayStyle)
{
    if (error)
        error->clear();
    if (!painter) {
        if (error)
            *error = QStringLiteral("Painter is null");
        return false;
    }

    const QString trimmed = normalizedTex(tex);
    if (trimmed.isEmpty())
        return true;

    QMutexLocker locker(&g_mathMutex);
    if (!ensureInited(error))
        return false;

    const float size = textSizePx > 0 ? static_cast<float>(textSizePx) : 20.0f;
    const auto render = createRender(trimmed, size, fg, displayStyle, error);
    if (!render)
        return false;

    drawRender(render.get(), painter, origin, verticalPaddingPx,
               TextDrawingMode::OutlinedText);
    return true;
}

Metrics measure(const QString &tex, int textSizePx, bool displayStyle)
{
    Metrics metrics;
    const QString trimmed = normalizedTex(tex);
    if (trimmed.isEmpty()) {
        metrics.valid = true;
        return metrics;
    }
    if (trimmed.size() > Diagram::kMaxTexChars) {
        metrics.error = QStringLiteral("Formula is too long (%1 characters; "
                                       "the limit is %2)")
                            .arg(trimmed.size()).arg(Diagram::kMaxTexChars);
        return metrics;
    }

    QMutexLocker locker(&g_mathMutex);
    if (!ensureInited(&metrics.error))
        return metrics;

    const float size = textSizePx > 0 ? static_cast<float>(textSizePx) : 20.0f;
    const auto render = createRender(trimmed, size, QColor(Qt::black),
                                     displayStyle, &metrics.error);
    if (!render)
        return metrics;

    metrics.width = qMax(1, render->getWidth());
    metrics.height = qMax(1, render->getHeight());
    const qreal baselineRatio = render->getBaseline();
    metrics.baseline = baselineRatio * metrics.height;
    metrics.ascent = metrics.baseline;
    metrics.descent = qMax<qreal>(0, metrics.height - metrics.baseline);
    metrics.depth = qMax(0, render->getDepth());
    metrics.valid = true;
    metrics.error.clear();
    return metrics;
}

QString errorFor(const QString &tex)
{
    // Validity is a property of parsing and layout, so this measures rather
    // than rasterizes. Rendering here allocated the formula's full backing
    // store and painted it once just to discard it, and the image provider
    // then rendered the same formula again for display — two full rasters per
    // formula, on the path that opens a note.
    const Metrics m = measure(tex, 20);
    if (m.valid)
        return QString();
    // A blank expression is not an error — it just renders nothing.
    if (tex.trimmed().isEmpty())
        return QString();
    return m.error.isEmpty() ? QStringLiteral("Unrenderable expression")
                             : m.error;
}

QStringList availableCommands()
{
    QMutexLocker locker(&g_mathMutex);
    static QStringList cached;
    static bool built = false;
    if (built)
        return cached;
    if (!ensureInited(nullptr))
        return QStringList();
    const auto userTypable = [](const QString &name) {
        return !name.isEmpty() && !name.contains(QLatin1Char('@'));
    };
    QSet<QString> names;
    for (const auto &entry : tex::SymbolAtom::_symbols) {
        const QString name = QString::fromStdString(entry.first);
        if (userTypable(name))
            names.insert(name);
    }
    for (const auto &entry : tex::MacroInfo::_commands) {
        const QString name = QString::fromStdWString(entry.first);
        if (userTypable(name))
            names.insert(name);
    }
    cached = QStringList(names.cbegin(), names.cend());
    std::sort(cached.begin(), cached.end());
    built = true;
    return cached;
}

int opticalMathPixelSize(const QFont &textFont)
{
    const qreal size = textFont.pixelSize() > 0 ? textFont.pixelSize() : 15.0;
    // The TFM height of a lowercase 'x' is the math font's exact x-height.
    // The font mode is fixed after the first MicroTeX initialization, so the
    // per-em value is process-constant.
    static const qreal mathXHeightPerEm = [] {
        const Metrics m = measure(QStringLiteral("x"), 256);
        return (m.valid && m.height > 0) ? m.height / 256.0 : 0.4305;
    }();
    const qreal textXHeight = QFontMetricsF(textFont).xHeight();
    if (mathXHeightPerEm <= 0 || textXHeight <= 0)
        return qRound(size);
    const qreal scale =
        qBound(1.0, (textXHeight / size) / mathXHeightPerEm, 1.5);
    return qRound(size * scale);
}

} // namespace MathRenderer

// ---- QML painted item + image provider + tools ----

MathImageProvider::MathImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QString MathTools::encode(const QString &tex) const
{
    return QString::fromLatin1(
        tex.toUtf8().toBase64(QByteArray::Base64UrlEncoding
                              | QByteArray::OmitTrailingEquals));
}

QString MathTools::errorFor(const QString &tex) const
{
    return MathRenderer::errorFor(tex);
}

int MathTools::opticalMathPixelSize(const QString &family, int pixelSize) const
{
    QFont font;
    if (!family.isEmpty())
        font.setFamily(family);
    font.setPixelSize(pixelSize > 0 ? pixelSize : 15);
    return MathRenderer::opticalMathPixelSize(font);
}

QStringList MathTools::availableCommands() const
{
    return MathRenderer::availableCommands();
}

QVariantMap MathTools::measure(const QString &tex, int textSizePx) const
{
    const MathRenderer::Metrics m = MathRenderer::measure(tex, textSizePx);
    return QVariantMap{
        {QStringLiteral("width"), m.width},
        {QStringLiteral("height"), m.height},
        {QStringLiteral("baseline"), m.baseline},
        {QStringLiteral("ascent"), m.ascent},
        {QStringLiteral("descent"), m.descent},
        {QStringLiteral("depth"), m.depth},
        {QStringLiteral("valid"), m.valid},
        {QStringLiteral("error"), m.error},
    };
}

QImage MathImageProvider::requestImage(const QString &id, QSize *size,
                                       const QSize &requestedSize)
{
    // id is "<base64url-tex>?fg=..&size=..&dpr=..". Split off the query.
    QString encoded = id;
    QString query;
    const int q = id.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        encoded = id.left(q);
        query = id.mid(q + 1);
    }
    const QString tex = QString::fromUtf8(
        QByteArray::fromBase64(encoded.toLatin1(),
                               QByteArray::Base64UrlEncoding));

    QColor fg(Qt::black);
    int textSize = 20;
    int verticalPadding = 0;
    qreal dpr = 1.0;
    const QUrlQuery params(query);
    if (params.hasQueryItem(QStringLiteral("fg"))) {
        bool ok = false;
        const uint argb = params.queryItemValue(QStringLiteral("fg"))
                              .toUInt(&ok, 16);
        if (ok)
            fg = QColor::fromRgba(argb);
    }
    if (params.hasQueryItem(QStringLiteral("size"))) {
        const int s = params.queryItemValue(QStringLiteral("size")).toInt();
        if (s > 0)
            textSize = s;
    }
    if (params.hasQueryItem(QStringLiteral("dpr"))) {
        const double d = params.queryItemValue(QStringLiteral("dpr")).toDouble();
        if (d > 0)
            dpr = d;
    }
    if (params.hasQueryItem(QStringLiteral("vpad"))) {
        const int pad = params.queryItemValue(QStringLiteral("vpad")).toInt();
        if (pad > 0)
            verticalPadding = qMin(pad, textSize);
    }
    Q_UNUSED(requestedSize);

    QImage image = MathRenderer::render(tex, textSize, fg, dpr, nullptr,
                                        verticalPadding);
    if (image.isNull()) {
        // Keep the provider contract (a valid image) even on parse failure;
        // the delegate detects the error via errorFor and shows the source.
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    if (size)
        *size = image.size();
    return image;
}
