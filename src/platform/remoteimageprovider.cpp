// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "remoteimageprovider.h"

#include <QImage>
#include <QBuffer>
#include <QImageReader>
#include <QQuickTextureFactory>
#include <QUrl>

#include "egressfetcher.h"

namespace {

// One in-flight image. QML owns it and deletes it after finished().
class RemoteImageResponse : public QQuickImageResponse
{
    Q_OBJECT

public:
    RemoteImageResponse(EgressFetcher *fetcher, const QString &url, const QSize &size)
        : m_requestedSize(size)
    {
        // The provider is called from the QML image-loading thread, while the
        // fetcher and its QNetworkAccessManager live on the GUI thread. Hop
        // there before touching either.
        QMetaObject::invokeMethod(fetcher, [this, fetcher, url]() {
            fetcher->request(QUrl(url), EgressFetcher::Purpose::RemoteImage,
                             [this](bool ok, const QByteArray &body, const QString &) {
                                 if (ok)
                                     m_image = RemoteImageProvider::decodeForDisplay(
                                         body, m_requestedSize);
                                 finished();
                             });
        }, Qt::QueuedConnection);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        QImage image = m_image;
        return QQuickTextureFactory::textureFactoryForImage(image);
    }

private:
    QImage m_image;
    QSize m_requestedSize;
};

} // namespace

RemoteImageProvider::RemoteImageProvider(EgressFetcher *fetcher)
    : m_fetcher(fetcher)
{
}

QImage RemoteImageProvider::decodeForDisplay(const QByteArray &body,
                                             const QSize &requestedSize)
{
    QBuffer buffer;
    buffer.setData(body);
    if (!buffer.open(QIODevice::ReadOnly))
        return QImage();

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.isEmpty()
        || sourceSize.width() > MaxDimension
        || sourceSize.height() > MaxDimension
        || qint64(sourceSize.width()) * sourceSize.height()
            > MaxDecodedPixels) {
        return QImage();
    }

    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        QSize decodeSize = sourceSize;
        decodeSize.scale(requestedSize, Qt::KeepAspectRatio);
        if (decodeSize.width() < sourceSize.width()
            || decodeSize.height() < sourceSize.height())
            reader.setScaledSize(decodeSize);
    }
    return reader.read();
}

QQuickImageResponse *RemoteImageProvider::requestImageResponse(
    const QString &id, const QSize &requestedSize)
{
    // QML hands back the id with one round of percent-decoding already done
    // for the URL path, so decode what remains rather than assuming either.
    const QString url = QUrl::fromPercentEncoding(id.toUtf8());
    return new RemoteImageResponse(m_fetcher, url, requestedSize);
}

#include "remoteimageprovider.moc"
