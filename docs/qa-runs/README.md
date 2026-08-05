# QA run records

One file per platform per release candidate, recording a run of
[../qa-checklist.md](../qa-checklist.md) against a built artifact on real
hardware. The checklist says what to run and what each item should do; the
files here say what happened when someone ran it.

## Naming

`<version>-<platform>.md`, where `<version>` is the tag the artifact was
built from and `<platform>` is one of `windows`, `macos`, `linux`. For
example, `v1.0.0-rc1-macos.md`.

Runs made before any tag exists use `pre-rc-<date>-<platform>.md`. Those
are exploratory passes rather than release evidence: they were made against
artifacts built from an untagged commit, so they cannot be reproduced from a
version string, and they do not satisfy a release gate. They are kept
because the defects they found are the reason several fixes exist.

## What a record holds

- The artifact: what it was, which commit it came from, and its checksum
  where one was published.
- The machine and operating-system version it ran on.
- A checked copy of the checklist sections that were run, or a plain
  statement of which sections were skipped and why.
- Every deviation from an expected result, and where the fix landed if one
  was made.

A run that found defects and got them fixed is still a completed run. What
makes a run incomplete is an unanswered item, not a failed one — so say
which items were not exercised rather than leaving them ambiguous.

## Release gate

`docs/release-rollback.md` requires a green pass on Windows, macOS and one
Linux Wayland desktop session before a stable release is published. A fix
made in response to a run invalidates that run for anything it could have
touched, so the pass that closes the gate has to be against the artifact
that actually ships.
