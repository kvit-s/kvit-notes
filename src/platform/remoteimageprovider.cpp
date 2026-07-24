// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "remoteimageprovider.h"

#include <QImage>
#include <QBuffer>
#include <QImageReader>
#include <QPointer>
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
        //
        // The fetcher is process-global (ProcessServices) and outlives the
        // engine, so a request still in flight when the engine tears this
        // response down (a window closing) would call back into a freed
        // object. Both that teardown and the callback run on the GUI thread, so
        // a QPointer guard checked there is sufficient. QQuickImageResponse is
        // not reclaimed until finished(), so a live response is safe to touch.
        QPointer<RemoteImageResponse> self(this);
        QMetaObject::invokeMethod(fetcher, [self, fetcher, url]() {
            if (!self)
                return;
            fetcher->request(QUrl(url), EgressFetcher::Purpose::RemoteImage,
                             [self](bool ok, const QByteArray &body, const QString &) {
                                 if (!self)
                                     return;
                                 if (ok)
                                     self->m_image = RemoteImageProvider::decodeForDisplay(
                                         body, self->m_requestedSize);
                                 self->finished();
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
