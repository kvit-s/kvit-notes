# Mutation audit of the regression tests

Every finding fixed in the July 2026 review effort came with regression tests,
and all of them passed. That is weaker evidence than it looks: a test that
passes both before and after a fix proves nothing about the fix. Twice during
the effort a test was written that passed against unfixed code, and both times
it was caught only because the author distrusted the green result.

This audit took the guard tests added since `35df537` and, for each one,
broke the thing it guards, confirmed the test failed, checked whether the
failure message identified the real problem, and reverted the change. No
mutation is committed; what is committed is the strengthening this turned up
and this record.

Nineteen audits, chosen for value rather than coverage: the tests guarding
data loss, the ones guarding the network trust boundary, and the drift guards.

## What a mutation audit can conclude

A test that fails under mutation is verified to catch that specific
regression. It says nothing about regressions nobody mutated. A test that
passes under mutation is either aimed at the wrong code, satisfied by a
different mechanism, or asserting something the fix does not change.

One methodological trap showed up immediately and is worth stating: a mutation
that never executes proves nothing. The first attempt at the containment audit
edited `relativePath()`, which those tests do not reach, and the resulting
"the guard does not catch this" verdict was an artifact of the mutation rather
than a property of the test. Confirm the mutated line is on the path before
believing the result.

## Results

### Verified: the guard fails when the fix is removed

| Guard | Mutation | Failure message |
|---|---|---|
| `test_undostack` divergent-edit cases (H3) | `cleanDiscarded` forced false | terse: `'!stack.isClean()' returned FALSE` |
| `test_kanbandata` preservation property (H8) | trailing blanks discarded | good: `iteration 2: blank lines 3 -> 2` |
| `test_kanbandata` label boundary | `^|\s` dropped from the label pattern | adequate (QCOMPARE diff) |
| `test_egresspolicy` address classification | 10.0.0.0/8 no longer refused | adequate (QCOMPARE diff) |
| `test_egresspolicy` redirect revalidation | redirect hop skips the policy re-check | terse: `'!result' returned FALSE. ()` |
| `test_documentmanager` in-flight save (A1) | `cancelPendingWrites()` made a no-op | excellent: names the duplicate and the resurrected note |
| `test_filewatcher` own-write guard | `noteOwnWrite()` made a no-op | adequate (QCOMPARE diff) |
| `test_llmnormalizer` valid-corpus guard (H7) | table-continuation heuristic always true | good: `normalize rewrote a valid document` |
| `test_collectionsearch` generations (M3) | stale generation accepted | good: `a superseded query's results were displayed` |
| `test_blockattributes` round trip (H6) | attribute tag dropped on attach | 27 failures, QCOMPARE diffs |
| `test_vaultlock` cross-process cases (A1b) | contention reported as unavailable | good on 4 of 6; two are bare `QVERIFY` |
| `test_shell` QML-warning guard | unknown context property referenced in the shell | good: names the warning count |
| `test_shell` context-property accounting | a name published that the shell never binds | excellent: `Added: [strayUnusedName]` |
| `test_shell` block-kind coverage | a sixth enumerator added with no delegate | excellent: `a block of that kind would render as an empty row` |

### Not verified, and what was done about it

**H9 containment: six tests, none of which exercise the guard.**
`ensureWithinRoot()` was made to return `true` unconditionally and
`isWithinCanonicalRoot()` to return `true` unconditionally; every containment
test still passed. The reason is that other mechanisms refuse first: the scan
excludes symlinks through `QDir::NoSymLinks`, and the mutating entry points
reject an escaping relative path at name validation (`Name cannot contain
slashes`) or at note/folder lookup (`No such note: ../…`), both of which run
before containment is consulted. Probing every public route confirmed none of
them reaches the check.

Those tests still pin the user-visible outcome, which is worth keeping. But
the canonical-containment machinery was a backstop nothing verified, which
matters because it is the last line of defence if an earlier check is
relaxed. Added `testCanonicalContainmentRejectsLinksOutOfTheRoot`, which goes
through `relativePath()` — the one public surface where containment is the
only thing deciding — and whose discriminating case is a path that is
textually under the root and resolves outside it. It fails both when
containment is disabled and when it is reduced to a textual prefix comparison.

**M10 streaming cap: passed with either enforcement mechanism removed.**
`oversizedResponseIsCutOffWhileReceiving` passed with the streaming abort
removed and again with the declared-length check removed, because each alone
handles a response that declares its size. It proves the response was refused,
not that the transfer was cut off while arriving, which is what its name
claims and what M10 is about. Added
`oversizedResponseWithNoDeclaredLengthIsCutOff`: with no `Content-Length` the
server's claim cannot be consulted, so only streaming enforcement can refuse
it. Removing that enforcement now fails with `server delivered 33554476 of
33554432 bytes: the transfer ran to completion instead of being abandoned`.

**SSRF resolution test: failed for the wrong reason, and would have dialled
the internet.** `nameResolvingToPrivateAddressIsRefused` pointed a resolver at
`10.0.0.5`. With the address check removed it did not assert; it hung until a
connect timeout and reported `the requested timeout (5000 ms) was too short`,
which reads like flakiness and invites raising the timeout, after which it
would pass vacuously, since connecting to `10.0.0.5` fails anyway. Its second
half pointed at `93.184.216.34`, so a regression would have made the suite
contact a real public host. Rewritten to aim the refused address at a live
loopback server, so the refusal is observable as "nothing reached it" and a
regression fails immediately; the multi-address case is now a separate test
using only loopback and a link-local address.

**H12 quick capture: the fix's specific window is not what the test covers,
and looking for it found a bug.** `captureNoteLeavesNothingBehindWhenTheWriteFails`
denies writes to the whole vault, so the note is never created; the two-step
"create empty, then fill" implementation that H12 replaced passes it
unchanged. Reaching the actual window needs a fault that lets the create
succeed and the body write fail, which `FaultInjection::FileSizeLimit`
provides.

Doing that surfaced a defect unrelated to capture: `writeTextFileAtomic()`
streams through `QTextStream` and returns `QSaveFile::commit()` without ever
checking `stream.status()`, while `writeFileBytesAtomic()` beside it does
check its write. Under a 4 KB cap a 64 KB capture returns a path and leaves a
file holding 4096 of 65545 bytes: the user is told the note was captured and
most of it is gone. Recorded as a `QEXPECT_FAIL` in
`captureNoteLeavesNoEmptyNoteWhenOnlyTheBodyFails`, which will report an
unexpected pass once the writer is fixed. Filed separately; not fixed here,
because the audit's remit was tests and that file is being restructured.

## Message quality

Three tests fail with messages that identify the problem well enough to act
on without reading the test: the in-flight save cases, the context-property
accounting guard, and the block-kind coverage guard. Several fail with a bare
`'!x' returned FALSE. ()`, which tells a reader that something is wrong but
not what. The pattern worth copying is `QVERIFY2` with a sentence naming the
user-visible consequence, as in "the abandoned save recreated the note at the
name it no longer has, leaving a duplicate beside the renamed file".

## The saturated gates

`testBenchmark500NoteOpen` and `testQueryPerformanceGate` fail on an idle
machine because their ceilings sit below their own quiet-machine cost. A gate
that is already failing cannot report a regression, so for as long as they are
red they provide no coverage at all, which is the same "passes and proves
nothing" problem in the other direction.

## Doing this again

Mutation-checking a guard costs a couple of minutes: break the thing, build
the one target, run the one test, revert. It is worth doing for any test whose
purpose is catching a specific regression, at the time it is written, while
the author still knows what the fix changed. The driver used here was a
throwaway script that applies an edit, builds, runs, and restores the file on
exit; nothing about it needs to be preserved beyond the habit.
