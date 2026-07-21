// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef WIKILINKSCANNER_H
#define WIKILINKSCANNER_H

#include <QList>
#include <QString>

namespace WikiLinkScanner {

// Positions use QString's UTF-16 coordinate system, so they can be passed
// directly to QString::replace even when surrounding text contains emoji.
struct Occurrence {
    int start = -1;
    int length = 0;
    int targetStart = -1;
    int targetLength = 0;
    int noteStart = -1;
    int noteLength = 0;
    int headingStart = -1;
    int headingLength = 0;
    int aliasStart = -1;
    int aliasLength = 0;
    QString rawTarget;
    QString note;
    QString heading;
    QString alias;
};

// Match the shared [[target#heading|alias]] grammar at an exact position.
// Opaque Markdown regions are handled by scan(), not by this helper.
bool matchAt(const QString &text, int pos, Occurrence *occurrence = nullptr);

// Scan Markdown while excluding fenced code, variable-length inline
// backticks, inline math, display math, and escaped openers.
QList<Occurrence> scan(const QString &text);

} // namespace WikiLinkScanner

#endif // WIKILINKSCANNER_H
