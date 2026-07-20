# Contributing to Kvit Notes

Thanks for considering a contribution. This page covers building, testing,
what we will and will not merge, and the licensing terms contributions are
accepted under.

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

QML is also linted statically, and that check blocks merges too:
`tools/run-qmllint.sh` runs over `qml/`. It reads every file, including those
no test instantiates, and rejects malformed QML, unresolvable imports and uses
of types that do not exist. Its header comment explains which qmllint
categories are switched off and why — in short, this project reaches C++
through context properties, which a static analyser cannot see at all.

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
