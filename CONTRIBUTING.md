# Contributing to Kvit Notes

Thanks for considering a contribution. This page covers building, testing,
what we will and will not merge, and the licensing terms contributions are
accepted under.

Before changing anything structural, read [docs/adr/](docs/adr/). Those records
hold the decisions the rest of the code is written against, including several
invariants that look arbitrary until you know what they defend: only one object
may write the open note, only one object may reach the network, and files on
disk are the only authoritative copy of a note. A change that crosses one of
those is not necessarily wrong, but it needs to argue with the record rather
than around it.

## Building

Requirements: Qt 6.10+, CMake 3.21+, and a C++20 compiler (GCC 12+,
MSVC 2022, or a recent AppleClang). Everything else is vendored.

Using presets (all platforms):

```
export QT_ROOT_DIR=/path/to/Qt/6.10.1/gcc_64   # your Qt kit
cmake --preset linux-release        # or windows-msvc-release / macos-release
cmake --build --preset linux-release -j
```

On Linux, `./build.sh` remains the developer convenience wrapper (configure +
build + run the suite; `--run` launches the editor).

## Testing

The suite is heterogeneous, and the labels matter:

- `unit`: deterministic C++ tests, headless-safe. This is the merge gate on
  every platform: `ctest -L unit` (or `ctest --preset unit-linux`).
- `performance`: timing-sensitive; informational on loaded machines.
- `shell`: loads the shipped `resources.qrc` and constructs the real
  application shell. Deterministic offscreen, and a merge gate on every
  platform alongside `unit`: `ctest --preset shell-linux`. These are what
  catch a QML syntax error, a file missing from the resource file, an import
  that does not resolve, or a renamed context property.
- `visual`: Qt Quick suites that need a real, focused display. Headless or
  xvfb runs of these are smoke tests only; they are not evidence a UI change
  is correct, and window-focus failures under a loaded compositor are not
  regressions.

### Performance budgets

Several suites assert that an operation stays inside a budget. Those numbers
are measured in **process CPU time**, not wall-clock, because wall-clock on a
shared machine measures the neighbours rather than the code: the same
unchanged 500-note collection open costs about 190 ms of CPU whether the
machine is idle or at load average 75, while its wall-clock time goes from
217 ms to 1191 ms. Budgets used to be asserted on the wall clock, and they
failed on unchanged code often enough that people learned to re-run instead
of investigate.

Each CPU budget carries two thresholds, because contention still costs a real
1.8-2.4x in CPU terms and that is the same order as a regression worth
catching:

- a **ceiling** that contention alone cannot breach, always enforced;
- a **tight budget** that is the number actually worth holding, enforced when
  the machine is measurably quiet.

When the tight budget is deferred the test says so, with the load and
contention that caused it, so a deferred check never reads as a passing one.
`KVIT_ENFORCE_TIMING_BUDGETS=1` enforces it regardless. The reasoning, the
measurements behind the thresholds, and the two approaches that were tried
and rejected are all in `tests/timingbudget.h`.

Budgets that are genuinely about elapsed time - an async call returning
promptly, a first frame, a cancellation not blocking the GUI thread - stay on
the wall clock and are simply not judged on a busy machine, since no metric
makes those load-proof. Where one of those guards something real, the test
also asserts it structurally: the cancellation tests check that a scan was
genuinely in progress and that the thread pool was genuinely saturated, so
what they prove does not rest on the timing number at all.

Which label a budget sits in matters more than the mechanism. Under
`performance` a false red costs attention. Under `unit` it is a required
check on three platforms, and a required check that fails for reasons
unrelated to the diff teaches reviewers to re-run rather than investigate,
which is the habit you least want. Two budgets sit in `unit` - the 500-note
collection open and the 500-note search query - and both are measured in CPU
time so they can stay there rather than being demoted to an informational
job.

If you change a budget, run `tools/verify-perf-budget.sh`. It injects a
doubled parse into a hot path and requires the budget to fail, then reverts
and requires it to pass. A budget that cannot fail is not a budget.

QML is also linted statically, and that check blocks merges too:
`tools/run-qmllint.sh` runs over the shipping QML in `qml/` and over the test
QML in `tests/`. It reads every file, including those no test instantiates, and
rejects malformed QML, unresolvable imports, uses of types that do not exist,
unqualified names and reads of properties a type does not have. The C++ side is
reached entirely through typed `Kvit` module singletons whose type description
qmltyperegistrar generates at build time, so those last two categories are on;
the only suppressions left are three line-scoped ones for gaps in Qt's own type
descriptions. The script's header comment lists them, and names the three
categories demoted to informational.

Building the `qmllint` target runs the same script, so
`cmake --build build --target qmllint` and `ctest -R QmlLint` both gate on it
without anyone remembering the script's path.

For UI-facing changes, also look at the running app. The automated suites
render on the CPU and cannot see GPU-path rendering problems.

## What gets merged

Bug fixes, performance work, portability fixes, and features aligned with the
editor's direction are welcome. To avoid wasted work, open an issue or a
Discussions thread before building anything sizable.

Some directions are deliberately declined, so that the loudest week-one
requests do not set the roadmap. Please do not open PRs for: mobile apps, a
sync service, an arbitrary CSS/theming engine, a general plugin API
(revisited after the premium tier ships), vim mode, real-time collaboration,
or an embedded web/PDF viewer.

## Licensing of contributions

Kvit Notes is licensed under the Mozilla Public License 2.0. By submitting a
contribution you agree that it is your own work (or that you have the right
to submit it) and that it is provided under MPL-2.0. There is no CLA; the
MPL's file-level terms are the whole agreement. New source files must carry
the standard MPL-2.0 header; `tools/apply-license-headers.sh` adds it, and
`tools/apply-license-headers.sh --check` verifies the tree.

## Dependencies

New third-party dependencies (including vendored code, fonts, and media used
by tests) need an entry in `packaging/sbom.yaml` with name, version, license,
and origin, and must be license-compatible with shipping under MPL-2.0
alongside LGPL Qt. PRs adding a dependency without a manifest entry will be
sent back for one.
