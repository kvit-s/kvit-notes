# Security policy

## Reporting a vulnerability

Report security problems privately to **info@kvit.app** rather than in a
public issue. Include the version, platform, and a reproduction; a proof of
concept helps but is not required.

You will get an acknowledgment within 72 hours. Once a fix ships, the issue
is disclosed in the release notes with credit to the reporter (tell us if you
prefer to stay unnamed).

## Scope notes

Kvit Notes is a local desktop application: it opens local markdown files and
runs no embedded browser or scripting engine.

It does make network requests, and a note decides most of their addresses,
which is why they are governed rather than merely made. Four things reach the
network: the optional update check (a single GET to the GitHub Releases API,
disabled in Settings), remote images in a note, remote audio and video, and
the preview card for a web embed. Everything but the update check is off
until the reader approves the origin: `network.autoLoadRemoteContent` is off
by default, an approved origin is remembered per origin, and every request
goes through one `EgressPolicy` and one `EgressFetcher`, which resolve the
hostname first, refuse loopback and private, link-local, unique-local,
carrier-grade-NAT and reserved addresses, pin the connection to the address
they checked, re-check each redirect hop from scratch, cap the body size and
enforce a timeout and a content-type check.

The most security-relevant surfaces are the markdown parser, HTML/PDF export,
the media/embed pipeline, and that egress policy; malformed input to any of
them that causes memory corruption or code execution is in scope, and so is
anything that reaches a host or an address the policy is meant to refuse.
