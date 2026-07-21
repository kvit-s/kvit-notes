# 0003. Every outbound request goes through one policy

**Status:** Accepted

## Context

A note is untrusted input. Anyone who can hand a reader a `.md` file can put a
URL in it, and the file arrives by import, by sync, by clone, or by an agent
writing markdown. Kvit Notes has several features that would naturally fetch
such a URL: embed preview cards, remote images, remote audio and video, and
favicons for previews.

Fetching any of them on sight has two distinct consequences. The first is
disclosure: a request tells whoever chose that URL the reader's address, user
agent and the moment the note was opened, which turns a note into a read
receipt. The second is that the editor becomes a request generator aimed at
whatever the URL names, including hosts only the reader's machine can reach.
A note naming `http://192.168.1.1/...` or the cloud metadata endpoint at
`169.254.169.254` makes the editor probe them from inside the reader's network.

An application-wide "offline mode" setting does not solve this, because the
dangerous requests are the ones made on the note's behalf without the reader
having asked for anything. Neither does trusting Qt's own loaders: a remote URL
bound to a QML `Image.source` is fetched by Qt's network stack directly, outside
whatever checks the application implements.

## Decision

One object decides whether the application may talk to a remote host, and one
object performs the request. Nothing else in the tree may construct a
`QNetworkAccessManager` or bind a QML `source` to a remote URL.

**`EgressPolicy` decides.** It holds the master switch
`network.autoLoadRemoteContent`, which is off by default; the set of origins the
reader has approved, stored as `network.allowedOrigins`; the scheme and
credential rules; and the address classification that rejects loopback, RFC1918
private ranges, IPv6 unique-local, link-local, multicast and reserved addresses.
Consent is granted per origin rather than per URL, because a preview page and
its thumbnail differ only in path, and a reader approving a preview means the
site rather than one image.

**`EgressFetcher` executes**, and is the only `QNetworkAccessManager` in the
tree. It resolves the hostname first and checks every address returned, pins the
connection to the address it checked while keeping the original hostname for the
`Host` header and TLS verification, refuses to let Qt follow redirects so each
hop is re-checked from scratch, caps the read buffer so an oversized body is
abandoned rather than buffered, and enforces a timeout and a content-type check.

The governing rule is that **opening a note is not consent**. Automatic loading
is off by default, remote content renders as an inert card with a Load button,
and the origin the reader approves is remembered.

Two requests do not need per-site approval, and both are disclosed in the
README's privacy section. The update check, if the reader leaves it on, is one
GET to the GitHub Releases API at startup at most once per calendar day, with no
telemetry and no auto-download. Content from an origin approved earlier loads
without asking again.

## Consequences

Auditing the application's network behavior means reading two files. A reviewer
asking "can this build phone home?" has a bounded answer, and a new feature that
wants the network has one way in.

Three rules are easy to break without noticing, which is the main ongoing cost:

- **Never bind a remote URL to a QML `Image.source`.** Delegates call
  `egressPolicy.imageSourceFor(url)`, which passes local paths through and turns
  an approved http(s) URL into an `image://remote/...` id served by
  `RemoteImageProvider` over the fetcher.
- **`isAllowed()` and `allowedOrigins()` are function calls, not properties.** A
  QML binding over them never re-evaluates on its own, so a binding must read
  `egressPolicy.revision`, which is bumped on every decision-affecting change.
- **Loopback is blocked, which a hermetic test needs to undo.**
  `EgressPolicy::setLoopbackAllowedForTests()` is the only way, and it is
  deliberately neither `Q_INVOKABLE` nor backed by a setting.

Remote media follows the same boundary. `MediaBlock.qml` never hands an http(s)
URL to `MediaPlayer`; `RemoteMediaCache` downloads approved media through
`EgressFetcher` with per-hop validation, a 64 MiB cap, a 30-second timeout, and
media content-type checks. QtMultimedia receives only the resulting temporary
local file URL, and the cache removes the file when the application composition
is destroyed. This buffers a bounded file, but preserves the decision that no
document-controlled URL reaches a second networking stack.

## Evidence in the tree

- `src/platform/egresspolicy.h`, `src/platform/egresspolicy.cpp`: the decision object, consent model, address classification
- `src/platform/egressfetcher.h`, `src/platform/egressfetcher.cpp`: the sole network access manager, DNS pinning, redirect re-checks, caps
- `src/platform/remotemediacache.h`, `src/platform/remotemediacache.cpp`: bounded media download and local-only playback handoff
- `src/platform/updatechecker.h`: the disclosed opt-out update check
- `devel.md`, "Network egress goes through one policy": the working rules
- `README.md`, "Privacy": the user-facing statement of the same behavior
- `tests/test_egresspolicy.cpp`: drives a loopback `QTcpServer` rather than the real internet, covering refusals, redirect re-checks and the streaming cap
