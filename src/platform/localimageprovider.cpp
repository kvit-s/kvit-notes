// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "localimageprovider.h"

#include "remoteimageprovider.h"

#include <QFileInfo>
#include <QImageReader>
#include <QUrl>

LocalImageProvider::LocalImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage LocalImageProvider::decodeFile(const QString &path,
                                      const QSize &requestedSize)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return QImage();

    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);

    // What the file says it is, before anything allocates on its word.
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.isEmpty()
        || sourceSize.width() > RemoteImageProvider::MaxDimension
        || sourceSize.height() > RemoteImageProvider::MaxDimension
        || qint64(sourceSize.width()) * sourceSize.height()
            > RemoteImageProvider::MaxDecodedPixels) {
        return QImage();
    }

    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        QSize decodeSize = sourceSize;
        decodeSize.scale(requestedSize, Qt::KeepAspectRatio);
        // Only ever downwards. A request larger than the file is a display
        // size, not an instruction to upscale the decode.
        if (decodeSize.width() < sourceSize.width()
            || decodeSize.height() < sourceSize.height())
            reader.setScaledSize(decodeSize);
    }
    return reader.read();
}

QImage LocalImageProvider::requestImage(const QString &id, QSize *size,
                                        const QSize &requestedSize)
{
    // QML hands the id back with one round of percent-decoding already done
    // for the URL path, so decode what remains rather than assuming either.
    const QString path = QUrl::fromPercentEncoding(id.toUtf8());
    const QImage image = decodeFile(path, requestedSize);
    if (size)
        *size = image.size();
    return image;
}
