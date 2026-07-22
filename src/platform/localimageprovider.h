// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef LOCALIMAGEPROVIDER_H
#define LOCALIMAGEPROVIDER_H

#include <QQuickImageProvider>

// Images loaded from the filesystem:
//
//     image://local/<percent-encoded absolute file path>
//
// A note is untrusted input, and so is what it points at. A remote image
// already went through RemoteImageProvider, which refuses anything whose
// header says it would decode larger than the allocation budget. A local
// file went straight to QML's own loader with no such check, so a note
// naming a 20000x20000 PNG could take the process down while it decoded.
//
// Image.sourceSize is not an answer on its own: it bounds the pixmap that is
// kept and the texture uploaded from it, but Qt's PNG handler still allocates
// the image at full size and scales afterwards. Measured on an 8000x8000 PNG,
// peak resident memory was the same ~287 MB with and without sourceSize; only
// refusing the file before read() avoids it. So the header is inspected first
// and oversized images resolve to a null image, which the delegate shows as
// its broken-image placeholder.
//
// The budget is RemoteImageProvider's, deliberately: where an image came from
// says nothing about what decoding it costs.
//
// Registered on the QML engine by AppContext::installContextProperties.
class LocalImageProvider : public QQuickImageProvider
{
public:
    LocalImageProvider();

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;

    // Header-first decoder, shared with the regression tests. Refuses a file
    // whose stored dimensions exceed the budget before a plugin is asked to
    // allocate its pixel buffer, and asks for a scaled read when the caller
    // wants less than the file holds.
    static QImage decodeFile(const QString &path,
                             const QSize &requestedSize = QSize());
};

#endif // LOCALIMAGEPROVIDER_H
