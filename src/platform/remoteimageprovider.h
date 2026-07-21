// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef REMOTEIMAGEPROVIDER_H
#define REMOTEIMAGEPROVIDER_H

#include <QQuickAsyncImageProvider>
#include <QImage>
#include <QString>

class EgressFetcher;

// The route by which QML loads a remote image.
//
// `Image { source: "https://..." }` would have Qt's own network stack fetch
// the URL, which puts the request outside every check EgressPolicy and
// EgressFetcher make: no consent, no address validation, no redirect
// revalidation, no byte cap. Nothing in qml/ may bind a remote URL to
// `source` for that reason. Instead a delegate builds
//
//     image://remote/<percent-encoded absolute URL>
//
// and the bytes arrive through EgressFetcher like every other request. An
// unapproved origin resolves to a null image, so the delegate shows its
// placeholder and the request never leaves the process.
//
// Registered on the QML engine by AppContext::installContextProperties.
class RemoteImageProvider : public QQuickAsyncImageProvider
{
public:
    static constexpr int MaxDimension = 8192;
    static constexpr qint64 MaxDecodedPixels = 32LL * 1024 * 1024;

    explicit RemoteImageProvider(EgressFetcher *fetcher);

    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;

    // Header-first decoder shared with regression tests. It refuses images
    // whose decoded dimensions exceed the allocation budget before read()
    // asks a plugin to allocate their pixel buffer.
    static QImage decodeForDisplay(const QByteArray &body,
                                   const QSize &requestedSize = QSize());

private:
    EgressFetcher *m_fetcher;
};

#endif // REMOTEIMAGEPROVIDER_H
