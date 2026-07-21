// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "textcanvas.h"

#include "diagrambudget.h"

#include <algorithm>

namespace {

// Existing glyph → arm set, so lines drawn over earlier lines merge into
// the right junction regardless of drawing order. Only the light set this
// canvas emits needs recognizing.
int armsForChar(QChar c)
{
    switch (c.unicode()) {
    case u'─': return 4 | 8;                 // L R
    case u'│': case u'║': return 1 | 2;      // U D
    case u'┌': return 2 | 8;
    case u'┐': return 2 | 4;
    case u'└': return 1 | 8;
    case u'┘': return 1 | 4;
    case u'├': return 1 | 2 | 8;
    case u'┤': return 1 | 2 | 4;
    case u'┬': return 2 | 4 | 8;
    case u'┴': return 1 | 4 | 8;
    case u'┼': return 1 | 2 | 4 | 8;
    default:   return 0;
    }
}

} // namespace

int TextCanvas::cols() const
{
    int width = 0;
    for (const QString &line : m_lines)
        width = std::max(width, int(line.size()));
    return width;
}

QChar TextCanvas::at(int row, int col) const
{
    if (row < 0 || row >= m_lines.size())
        return QChar();
    const QString &line = m_lines.at(row);
    if (col < 0 || col >= line.size())
        return QChar();
    return line.at(col);
}

bool TextCanvas::ensure(int row, int col)
{
    if (row < 0 || col < 0)
        return false;
    // Rows and columns come from scene coordinates, so a note-supplied
    // arrangement decides how much storage this grid asks for. Growth past the
    // budget is dropped: the export is clipped rather than unbounded, and
    // put()/at() already treat out-of-range cells as absent.
    if (row >= Diagram::kMaxTextCanvasRows || col >= Diagram::kMaxTextCanvasCols)
        return false;
    // The per-axis caps bound each dimension but not their product: a box
    // spanning both axes grows every row it touches out to the far column.
    // The cell total is what the three dense per-row arrays actually cost, so
    // it is the limit that has to hold.
    const qint64 wanted = qint64(col) + 1 - (row < m_lines.size()
                                             ? m_lines.at(row).size() : 0);
    if (wanted > 0 && m_cells + wanted > Diagram::kMaxTextCanvasCells)
        return false;
    while (m_lines.size() <= row) {
        m_lines.append(QString());
        m_arms.append(QList<quint8>());
        m_doubles.append(QList<bool>());
    }
    QString &line = m_lines[row];
    QList<quint8> &arms = m_arms[row];
    QList<bool> &doubles = m_doubles[row];
    while (line.size() <= col) {
        line.append(QLatin1Char(' '));
        arms.append(0);
        doubles.append(false);
    }
    if (wanted > 0)
        m_cells += wanted;
    return true;
}

void TextCanvas::put(int row, int col, QChar c)
{
    if (row < 0 || col < 0)
        return;
    if (!ensure(row, col))
        return;
    m_lines[row][col] = c;
    m_arms[row][col] = 0;
    m_doubles[row][col] = false;
}

void TextCanvas::drawText(int row, int col, const QString &text)
{
    for (int i = 0; i < text.size(); ++i)
        put(row, col + i, text.at(i));
}

QChar TextCanvas::charForArms(int arms, bool doubleVertical)
{
    switch (arms) {
    case 4: case 8: case 4 | 8:
        return QChar(u'─');
    case 1: case 2: case 1 | 2:
        return doubleVertical ? QChar(u'║') : QChar(u'│');
    case 2 | 8: return QChar(u'┌');
    case 2 | 4: return QChar(u'┐');
    case 1 | 8: return QChar(u'└');
    case 1 | 4: return QChar(u'┘');
    case 1 | 2 | 8: return QChar(u'├');
    case 1 | 2 | 4: return QChar(u'┤');
    case 2 | 4 | 8: return QChar(u'┬');
    case 1 | 4 | 8: return QChar(u'┴');
    case 1 | 2 | 4 | 8: return QChar(u'┼');
    default: return QChar(u' ');
    }
}

bool TextCanvas::mergeArms(int row, int col, int arms, bool doubleVertical)
{
    if (row < 0 || col < 0)
        return false;
    if (!ensure(row, col))
        return false;
    const QChar existing = m_lines.at(row).at(col);
    int current = m_arms.at(row).at(col);
    if (current == 0)
        current = armsForChar(existing); // adopt pre-drawn glyphs
    // Lines never eat text: a non-line, non-space cell stays as it is.
    if (current == 0 && existing != QLatin1Char(' '))
        return true;
    const int merged = current | arms;
    const bool dbl = m_doubles.at(row).at(col) || doubleVertical;
    m_lines[row][col] = charForArms(merged, dbl);
    m_arms[row][col] = quint8(merged);
    m_doubles[row][col] = dbl;
    return true;
}

void TextCanvas::drawHLine(int row, int col1, int col2)
{
    const int from = std::min(col1, col2);
    const int to = std::max(col1, col2);
    if (from == to) {
        mergeArms(row, from, ArmLeft | ArmRight);
        return;
    }
    if (!mergeArms(row, from, ArmRight))
        return;
    for (int col = from + 1; col < to; ++col) {
        if (!mergeArms(row, col, ArmLeft | ArmRight))
            return;   // the rest of this row is outside the budget too
    }
    mergeArms(row, to, ArmLeft);
}

void TextCanvas::drawVLine(int col, int row1, int row2)
{
    const int from = std::min(row1, row2);
    const int to = std::max(row1, row2);
    if (from == to) {
        mergeArms(from, col, ArmUp | ArmDown);
        return;
    }
    mergeArms(from, col, ArmDown);
    for (int row = from + 1; row < to; ++row)
        mergeArms(row, col, ArmUp | ArmDown);
    mergeArms(to, col, ArmUp);
}

void TextCanvas::drawBox(int top, int left, int bottom, int right,
                         bool doubleWalls)
{
    if (bottom < top)
        std::swap(top, bottom);
    if (right < left)
        std::swap(left, right);
    drawHLine(top, left, right);
    if (bottom > top) {
        drawHLine(bottom, left, right);
        mergeArms(top, left, ArmDown);
        mergeArms(top, right, ArmDown);
        for (int row = top + 1; row < bottom; ++row) {
            mergeArms(row, left, ArmUp | ArmDown, doubleWalls);
            mergeArms(row, right, ArmUp | ArmDown, doubleWalls);
        }
        mergeArms(bottom, left, ArmUp);
        mergeArms(bottom, right, ArmUp);
    }
}

void TextCanvas::drawArrowhead(int row, int col, Direction dir)
{
    switch (dir) {
    case Up:    put(row, col, QChar(u'▲')); break;
    case Down:  put(row, col, QChar(u'▼')); break;
    case Left:  put(row, col, QChar(u'◄')); break;
    case Right: put(row, col, QChar(u'►')); break;
    }
}

QString TextCanvas::toString() const
{
    QStringList out;
    out.reserve(m_lines.size());
    for (QString line : m_lines) {
        while (line.endsWith(QLatin1Char(' ')))
            line.chop(1);
        out.append(line);
    }
    // Trailing blank lines carry no drawing.
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out.join(QLatin1Char('\n'));
}
