// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MATHRENDERER_H
#define MATHRENDERER_H

#include <QObject>
#include <QQuickImageProvider>
#include <QImage>
#include <QColor>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QPainter;

// The single seam over the vendored MicroTeX engine. Everything the app knows
// about LaTeX rendering passes through here: nothing else includes the
// MicroTeX headers. A TeX string becomes a laid-out QImage, themed to a
// foreground color; parse failures are reported as a message instead of
// throwing, so the delegates can show "source + named error — never nothing".
//
// MicroTeX keeps global static state and is not thread-safe, so every call
// into it is serialized by one process-wide mutex, and the resource root
// (fonts + formula/symbol XML) is initialized exactly once, lazily, on first
// use.
namespace MathRenderer {

struct Metrics {
    qreal width = 0;
    qreal height = 0;
    qreal baseline = 0;
    qreal ascent = 0;
    qreal descent = 0;
    qreal depth = 0;
    bool valid = false;
    QString error;
};

// Render `tex` at the given pixel text size in the foreground color. Returns a
// null QImage if the expression does not parse; `error` (when non-null)
// receives the reason. `dpr` sets the image's device pixel ratio so the
// result stays crisp on high-DPI displays. `verticalPaddingPx` adds transparent
// logical pixels above and below the formula; inline overlays use this to keep
// antialiasing away from the image edge without changing display math.
QImage render(const QString &tex, int textSizePx, const QColor &fg,
              qreal dpr = 1.0, QString *error = nullptr,
              int verticalPaddingPx = 0);

// Paint directly into an existing QPainter, keeping MicroTeX setup and
// locking centralized. Retained as a diagnostics/embedding seam; the rejected
// QQuickPaintedItem display prototype that used it showed no visual gain over
// the image provider.
bool paint(QPainter *painter, const QString &tex, int textSizePx,
           const QColor &fg, const QPointF &origin = QPointF(),
           QString *error = nullptr, int verticalPaddingPx = 0,
           bool displayStyle = true);

// Return MicroTeX's logical layout metrics at `textSizePx`, without applying
// device-pixel ratio. Blank TeX is valid with zero dimensions; invalid TeX sets
// `error` and leaves `valid` false.
Metrics measure(const QString &tex, int textSizePx, bool displayStyle = true);

// The pixel size at which math should render next to text set in `textFont`
// so both share an optical size: the math font's x-height (Charter math
// 0.442em, Computer Modern 0.4305em — well below UI sans fonts' ~0.5em) is
// matched to the text font's x-height, like LaTeX's mathscale mechanism when
// pairing math with an unrelated text face. Never shrinks below the nominal
// size; upscale is clamped at 1.5x.
int opticalMathPixelSize(const QFont &textFont);

// Parse-only check: empty string when `tex` renders, else the error message.
QString errorFor(const QString &tex);

// Every command name a user can type, enumerated from the MicroTeX
// registries (symbols and macros — including \newcommand definitions and
// the NewTX additions) after engine initialization. Internal names
// (anything containing '@', e.g. the matrix@@env helpers) are filtered
// out. Names come without the leading backslash, sorted; cached after the
// first call. The math-command menu's completion corpus.
QStringList availableCommands();

// Resolved resource root: KVIT_MATH_RES env override, else a per-platform
// path relative to the executable (math-res beside it, ../share/kvit-notes/
// math-res, or the bundle's Resources/math-res), else the vendored source
// path as the dev/test fallback.
QString resourceRoot();

// Headless packaging probe behind `kvit-notes --math-selftest`: constructs
// an offscreen QGuiApplication, renders one formula against the resolved
// root, prints the root and the outcome, and returns a process exit code.
// Call only from main(), before any other Q(Gui)Application exists.
int runSelfTest();

} // namespace MathRenderer

// The image provider registered under "math". A MathBlock or inline-math span
// sets an Image source to
//   image://math/<base64url(tex)>?fg=<aarrggbb>&size=<px>&dpr=<n>
// and the provider renders it. The query string carries the theme color and
// size so a theme or zoom change reloads the image. Kept a pure
// QQuickImageProvider (no QObject) so the QML-facing methods live on the
// separate MathTools below — multiply-inheriting QObject here is ambiguous.
class MathImageProvider : public QQuickImageProvider
{
public:
    MathImageProvider();

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;
};

// QML context property (`mathRenderer`): the parse-check that drives the error
// state, the base64url encoder for the image://math/ path, and measurement
// helpers.
class MathTools : public QObject
{
    Q_OBJECT

public:
    explicit MathTools(QObject *parent = nullptr) : QObject(parent) {}

    // Encode a TeX string for the image://math/ path (base64url, no padding).
    Q_INVOKABLE QString encode(const QString &tex) const;

    // "" when `tex` renders, else the parse error — drives the error state.
    Q_INVOKABLE QString errorFor(const QString &tex) const;

    // Optically matched math pixel size for text set in `family` at
    // `pixelSize` (see MathRenderer::opticalMathPixelSize). Display-math
    // delegates use this; inline math gets the engine's mathFontPixelSize.
    Q_INVOKABLE int opticalMathPixelSize(const QString &family,
                                         int pixelSize) const;

    // MicroTeX layout metrics for inline placement experiments.
    Q_INVOKABLE QVariantMap measure(const QString &tex, int textSizePx) const;

    // The enumerated command corpus (MathRenderer::availableCommands).
    Q_INVOKABLE QStringList availableCommands() const;
};

#endif // MATHRENDERER_H
