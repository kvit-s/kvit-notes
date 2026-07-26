// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramtext.h"

#include "../mathrenderer.h"

#include <QRegularExpression>

namespace Diagram {

QString mathLabel(const QString &label)
{
    // The same normalization every layout applies before measuring: a label
    // carries its line breaks as `<br>` or as an escaped `\n`.
    static const QRegularExpression br(QStringLiteral("<br\\s*/?>"),
                                       QRegularExpression::CaseInsensitiveOption);
    QString s = label;
    s.replace(br, QStringLiteral("\n"));
    s.replace(QLatin1String("\\n"), QStringLiteral("\n"));
    s = s.trimmed();

    // "$$x$$" is the shortest expression that has any content.
    if (s.size() < 5 || !s.startsWith(QLatin1String("$$"))
        || !s.endsWith(QLatin1String("$$")))
        return QString();

    const QString inner = s.mid(2, s.size() - 4);
    // Two expressions side by side are not one label's worth of mathematics,
    // and the delimiters would pair the wrong way round.
    if (inner.contains(QLatin1String("$$")) || inner.trimmed().isEmpty())
        return QString();
    return inner;
}

int mathLabelPixelSize(const QFont &font)
{
    return MathRenderer::opticalMathPixelSize(font);
}

QSizeF mathLabelSize(const QString &tex, const QFont &font)
{
    if (tex.isEmpty())
        return QSizeF();
    const MathRenderer::Metrics m =
        MathRenderer::measure(tex, mathLabelPixelSize(font), true /* display */);
    if (!m.valid || m.width <= 0 || m.height <= 0)
        return QSizeF();
    return QSizeF(m.width, m.height);
}

} // namespace Diagram
