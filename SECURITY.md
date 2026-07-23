# Security policy

## Reporting a vulnerability

Report security problems privately to **info@kvit.app** rather than in a
public issue. Include the version, platform, and a reproduction; a proof of
concept helps but is not required.

You will get an acknowledgment within 72 hours. Once a fix ships, the issue
is disclosed in the release notes with credit to the reporter (tell us if you
prefer to stay unnamed).

## Scope notes

Kvit Notes is a local desktop application: it opens local markdown files,
makes no network requests except the optional update check (a single GET to
the GitHub Releases API, disabled in Settings), and runs no embedded browser
or scripting engine. The most security-relevant surfaces are the markdown
parser, HTML/PDF export, and the media/embed pipeline; malformed input to
any of these that causes memory corruption or code execution is in scope.
