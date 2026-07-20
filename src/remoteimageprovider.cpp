// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "remoteimageprovider.h"

#include <QImage>
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
                                     m_image.loadFromData(body);
                                 finished();
                             });
        }, Qt::QueuedConnection);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        QImage image = m_image;
        if (!image.isNull() && m_requestedSize.isValid() && !m_requestedSize.isEmpty()) {
            image = image.scaled(m_requestedSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }
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

QQuickImageResponse *RemoteImageProvider::requestImageResponse(
    const QString &id, const QSize &requestedSize)
{
    // QML hands back the id with one round of percent-decoding already done
    // for the URL path, so decode what remains rather than assuming either.
    const QString url = QUrl::fromPercentEncoding(id.toUtf8());
    return new RemoteImageResponse(m_fetcher, url, requestedSize);
}

#include "remoteimageprovider.moc"
