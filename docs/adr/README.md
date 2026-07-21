# Architecture decision records

This directory holds the architecture decisions behind Kvit Notes: the small
number of choices that constrain how the rest of the code may be written, along
with the reasoning that produced them. A decision earns a record here when
getting it wrong would cost user data, would leak information off the machine,
or would take a rewrite rather than a patch to reverse.

## What belongs where

Kvit Notes keeps four kinds of writing apart, because they answer different
questions and go stale at different rates.

| Document | Question it answers |
|---|---|
| [usage.md](../../usage.md) | How do I use the application? |
| [features.md](../../features.md) | What behavior is the application specified to have? |
| `docs/adr/` (here) | Why is the code built this way, and what may I not change? |
| [docs/backlog.md](../backlog.md) | What is known to be missing or unfinished? |
| [devel.md](../../devel.md) | How do I build, debug and test it? |

The distinction that matters most is between the last three. A specification
describes intended behavior, a decision record describes a constraint and the
reasoning that justifies it, and a backlog describes work not yet done. Mixing
them means a reader cannot tell whether a paragraph describes what the software
does today, what it is meant to do eventually, or what someone decided it should
never do.

## These records are public, deliberately

This repository is public, so anything added here is published. That is the
intent rather than a side effect.

Most of these decisions exist as invariants a contributor could break without
realizing it. Nothing outside `EgressFetcher` may construct a
`QNetworkAccessManager`; nothing but `DocumentManager` may write to the open
note; a remote URL must never reach a QML `Image.source`. Each of those reads as
an arbitrary restriction until you know what it is defending against, and a
contributor who cannot see the reasoning will either re-litigate it in review or
route around it in good faith.

The security-relevant records describe defenses that are already legible in the
source, against a threat model that README.md and devel.md already state in
public. Publishing the reasoning alongside them adds no information an attacker
could not obtain by reading `src/platform/egresspolicy.cpp`, and it gives a reviewer the
context to judge whether a change weakens the defense.

Records that state an open question do advertise a gap, and ADRs 0004, 0005 and
0006 currently do. That was weighed and accepted: the gaps are equally visible to
anyone reading the code, and a stated open question invites a considered proposal
where silence invites an accidental one. Vulnerabilities should still be reported
privately through [SECURITY.md](../../SECURITY.md) rather than filed as ADR
commentary.

## Format

Each record states its status, the context that forced a decision, the decision
itself, and the consequences that follow — including the ones that are
inconvenient. Statuses in use:

- **Accepted** — decided, implemented, and true of the code today.
- **Open** — the question is live and the record states the options and the
  tradeoff rather than a resolution. An open record is not a placeholder to be
  filled in later with whatever gets built; it is a description of a choice the
  project has not yet made.
- **Superseded** — replaced by a later record, which it names.

A record describes the decision as it stands now. Where the path taken to reach
it explains something a reader would otherwise find puzzling, it goes in a
clearly marked section rather than woven through the reasoning.

## Index

| # | Title | Status |
|---|---|---|
| [0001](0001-files-on-disk-are-authoritative.md) | Files on disk are authoritative; derived state is rebuildable | Accepted |
| [0002](0002-exclusive-write-ownership.md) | One writer owns the open note | Accepted |
| [0003](0003-network-egress-policy.md) | Every outbound request goes through one policy | Accepted |
| [0004](0004-cancellation-and-root-switching.md) | Cancellation and root switching | Accepted, with a named remainder |
| [0005](0005-multi-process-behaviour.md) | Two instances on one notes root | Accepted |
| [0006](0006-extension-trust.md) | What an extension is trusted to do | Open |
| [0007](0007-supported-release-platforms.md) | Which platforms a release supports | Accepted |
| [0008](0008-module-boundary.md) | Seven modules, and the direction between them | Accepted, with a named remainder |
