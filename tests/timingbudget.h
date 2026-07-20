// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once

#include <QtCore/QtGlobal>

// Where wall-clock budgets are enforced.
//
// A handful of suites measure elapsed time against a budget calibrated on
// a developer machine. Hosted CI runners are shared, throttled, and an
// order of magnitude slower on these paths: the 1000-note query budget of
// 25 ms measures 176 ms on a two-core GitHub Linux runner, repeatably, from
// the same commit that measures well inside budget locally. Enforcing there
// means either a permanently red merge gate or a ladder of per-platform
// numbers that track runner capacity rather than the code.
//
// So the measurement always runs and is always reported, and the assertion
// is enforced where the number means something: developer machines and the
// local `./build.sh --test` gate. On CI the test reports and skips, which
// ctest shows as a skip rather than a pass. The `performance`-labelled
// entries re-run the same functions with enforcement forced on, and CI runs
// that label as an informational, non-blocking job, so a real regression
// still shows up in the trend.
//
// Set KVIT_ENFORCE_TIMING_BUDGETS=1 to enforce anywhere, including CI.
inline bool kvitTimingBudgetsEnforced()
{
    if (qEnvironmentVariableIsSet("KVIT_ENFORCE_TIMING_BUDGETS"))
        return true;
    // GitHub Actions, GitLab CI, CircleCI and Travis all set CI.
    return !qEnvironmentVariableIsSet("CI");
}

// Message used by the skip, kept identical across suites so a reader
// scanning CI output sees one recognizable reason.
#define KVIT_TIMING_BUDGET_SKIP_REASON                                       \
    "timing budget not enforced on a shared CI runner; the measurement is "  \
    "reported above and the performance-labelled run enforces it"
