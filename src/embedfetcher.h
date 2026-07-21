// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EMBEDFETCHER_H
#define EMBEDFETCHER_H

#include <QString>
#include <functional>

// The network seam: fetch a page's HTML. The app wires EgressFetcher, which
// applies the egress policy; tests wire a fake that returns canned HTML (or a
// canned failure), so the suite is hermetic and never touches the network.
// `done(success, html)` may be called synchronously (the fake) or
// asynchronously (the real fetcher).
//
// The interface lives on its own so the implementation and its caller do not
// have to include each other: EgressFetcher implements it, EmbedMetadata
// calls it, and neither is a dependency of the other.
class EmbedFetcher
{
public:
    virtual ~EmbedFetcher() = default;
    virtual void fetch(const QString &url,
                       std::function<void(bool, const QString &)> done) = 0;
};

#endif // EMBEDFETCHER_H
