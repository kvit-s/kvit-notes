// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMGLYPHS_H
#define DIAGRAMGLYPHS_H

#include <QChar>
#include <QString>

// The shared character-diagram glyph vocabulary:
// the corner/wall/fill/junction/arrow sets DiagramRepair recognizes when
// straightening pasted diagrams, lifted here so TextCanvas EMITS exactly
// the vocabulary repair ACCEPTS — the "exporter output is a repair fixed
// point" property both test suites pin. Pure predicates, no state.
namespace DiagramGlyphs {

inline bool isTopLeft(QChar c)
{
    static const QString s = QStringLiteral("┌┏╔╭");
    return s.contains(c);
}
inline bool isTopRight(QChar c)
{
    static const QString s = QStringLiteral("┐┓╗╮");
    return s.contains(c);
}
inline bool isBottomLeft(QChar c)
{
    static const QString s = QStringLiteral("└┗╚╰");
    return s.contains(c);
}
inline bool isBottomRight(QChar c)
{
    static const QString s = QStringLiteral("┘┛╝╯");
    return s.contains(c);
}
inline bool isHFill(QChar c)
{
    static const QString s = QStringLiteral("─━═┄┅┈┉╌╍-=");
    return s.contains(c);
}
// Junctions and arrowheads that may sit inside a horizontal edge. They
// never terminate an edge and are never trimmed over; connector runs may
// slide them along the edge.
inline bool isEdgeJunction(QChar c)
{
    static const QString s = QStringLiteral("┬┳╦╤╥┴┻╩╧╨┼╋▼▲+");
    return s.contains(c);
}
inline bool isWall(QChar c)
{
    static const QString s = QStringLiteral("│┃║╎╏┆┇┊┋├┤┣┫╠╣╞╡╟╢|");
    return s.contains(c);
}
// Free-standing connector cells between boxes.
inline bool isConnector(QChar c)
{
    static const QString s = QStringLiteral("│┃║▼▲|");
    return s.contains(c);
}

} // namespace DiagramGlyphs

#endif // DIAGRAMGLYPHS_H
