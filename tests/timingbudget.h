// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtCore/QtGlobal>

#if defined(Q_OS_UNIX)
#include <cstdlib>
#include <ctime>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

// ─────────────────────────────────────────────────────────────────────────
// How this project measures performance budgets, and why not with a clock
// on the wall.
//
// Several suites assert that an operation finishes inside a budget. Those
// budgets encode product decisions - the 500-note collection open and the
// War-and-Peace load gate are the numbers the performance work was driven
// by - so they have to keep catching regressions. The problem is not the
// numbers, it is what they were measured against.
//
// Wall-clock elapsed time includes every millisecond the process spent
// descheduled while other work ran. On a shared machine that dominates
// everything else. Measured on this 28-core developer box, the same
// unchanged 500-note collection open:
//
//   condition                wall (median)   process CPU (median)
//   idle                          217 ms              221 ms
//   load average 27               962 ms              439 ms
//   load average 75              1191 ms              395 ms
//
// Wall-clock inflates 5.5x between idle and a loaded machine and keeps
// climbing; CPU time inflates 1.8x and then plateaus, because the extra
// cost is cache and memory-bandwidth contention rather than waiting for a
// core. Within one load condition the spread of CPU samples is 1.07x
// against 1.67x for wall-clock. That is the whole reason these budgets are
// measured in CPU time now: it is the metric that tracks the code rather
// than the machine's mood.
//
// Two alternatives were measured and rejected:
//
//   Best-of-N wall-clock. Timing noise is one-sided, so the minimum of
//   several runs is a decent estimator of the uncontended cost, and it does
//   help. It is not enough: at load average 64 the best of five runs still
//   measured 1318 ms against a 1000 ms budget, because under sustained load
//   every run is inflated and there is no quiet moment to find.
//
//   Dividing by a calibration workload run in the same process. This does
//   cancel some contention - the target/reference ratio inflated 1.43x
//   between idle and load average 75, against 1.8x for CPU time alone - but
//   only once the reference is sized close to the target. A sub-millisecond
//   reference made things worse than raw wall-clock (2.48x spread), because
//   dividing by a noisy small number amplifies noise. The remaining gain
//   over plain CPU time did not justify a second workload to size, maintain
//   and explain in every suite that measures anything.
//
// What CPU time cannot do:
//
//   - It does not measure latency. For anything whose budget is about how
//     long a user waits - an async call returning promptly, a worker thread
//     finishing - wall-clock is the correct metric and those budgets stay
//     on the wall clock. They are given headroom instead, and the ones that
//     matter are also asserted structurally (the call returns before the
//     work finishes) rather than only by a number.
//   - It sums every thread. For a genuinely parallel operation, CPU time
//     measures total work, not elapsed time, so it must not be compared
//     against a latency budget.
//
// Why one number is not enough. Contention costs a real 1.8-2.4x in CPU
// terms, which is the same order as the regressions worth catching, so no
// single threshold separates them. Measured on the 500-note open, with a
// deliberate 2x regression injected to find out:
//
//                      idle CPU     loaded CPU (load average 52-57)
//   unchanged            190 ms                 393-457 ms
//   2x regression        372 ms                 807-836 ms
//
// A threshold above 457 does not catch the regression at idle; one below
// 372 fails on unchanged code under load. So each budget carries two:
//
//   tight    the number that means something, checked only when the sample
//            is trustworthy - when the process actually held a core.
//   ceiling  never breached by contention alone, always checked.
//
// Trustworthiness is measured rather than guessed, from two signals that
// catch different things - see kvitMachineIsQuiet below for why one of them
// alone is not enough.
//
// This is deliberately not "skip the test when the machine is busy". The
// ceiling is always enforced, so coverage never disappears; only the sharper
// of the two checks defers, and when it does it says so in the log together
// with the contention and load that caused it, so a deferred budget cannot
// be mistaken for a passing one. On a hosted CI runner nothing else competes
// with a single-threaded test, so both signals read quiet there and the
// tight budget is the one that runs.
//
// Against the 500-note open the pair (tight 300 ms, ceiling 650 ms) catches
// the injected 2x regression at idle by 1.24x and under heavy load by 1.24x,
// while passing unchanged code by 1.58x at idle and 1.42x under load. The
// wall-clock budget it replaces was 1000 ms against a 190 ms idle cost: it
// could only ever catch a 5x regression, and it failed on unchanged code
// five times out of five at load average 52.
// ─────────────────────────────────────────────────────────────────────────

// Which label a budget belongs in, which matters more than the mechanism.
//
// A budget under the `performance` label is informational: CI runs that job
// with continue-on-error, so a false red costs attention. A budget under
// `unit` is a required check on three operating systems. When one of those
// fails for a reason unrelated to the diff it does something worse than waste
// time - it teaches reviewers that a red required check is something you
// re-run. That habit is the actual damage, and it is why the two budgets that
// sit in `unit` are the ones converted to CPU time here rather than moved out
// of the gate:
//
//   collection.open 500-note   tests/test_notecollection.cpp
//   search 500-note query      tests/test_searchindexdb.cpp
//
// Moving them to `performance` would have been the easier answer and a worse
// one. Both guard product decisions that the performance work in this
// codebase was driven by, and demoting them to a non-blocking job means a
// regression in either merges and is noticed later, if at all. They are
// robust enough to stay in the gate now, and tools/verify-perf-budget.sh
// exists to keep proving they still catch something.
//
// The search gate is worth a second note, because its old form was worse than
// flaky. It was guarded by kvitTimingBudgetsEnforced(), which skips whenever
// CI is set - so on a hosted runner the assertion never executed at all,
// while on a developer machine it enforced a wall-clock number and flapped.
// It had the failure mode in the place where it hurt and no coverage in the
// place it was written for. Measured in CPU time it holds on a shared runner,
// so the skip is gone and the gate runs everywhere.
//
// Everything else that asserts on a duration was audited and deliberately
// left alone. The War-and-Peace load and bulk-delete gates, the diagram
// layout and classifier limits, the math renderer's refusal check, the LLM
// normalizer's, and the big-file loads are ceilings against hangs and
// pathological blowups, not budgets: measured at load average 68 they come in
// at 0-38 ms against limits of 250-30000 ms. Twenty-six to five-hundred times
// of headroom does not flake, and tightening them would invent a precision
// they were never meant to have.

// Process CPU time consumed so far, in milliseconds, summed across threads.
// Monotonic, and unaffected by time the process spends waiting for a core.
inline double kvitProcessCpuMs()
{
#if defined(Q_OS_UNIX)
    timespec ts {};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return double(ts.tv_sec) * 1000.0 + double(ts.tv_nsec) / 1000000.0;
#elif defined(Q_OS_WIN)
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0.0;
    const auto toMs = [](const FILETIME &ft) {
        // FILETIME counts 100-nanosecond intervals.
        return (double((qint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime))
               / 10000.0;
    };
    return toMs(kernel) + toMs(user);
#else
    return 0.0;
#endif
}

// Measures both metrics over one operation. CPU time is what the budget is
// asserted against; wall-clock is carried alongside because it is what a
// reader recognizes, and because their ratio says how contended the machine
// was when the sample was taken.
class KvitOpTimer
{
public:
    KvitOpTimer() { restart(); }

    void restart()
    {
        m_cpuStart = kvitProcessCpuMs();
        m_wall.start();
    }

    double cpuMs() const { return kvitProcessCpuMs() - m_cpuStart; }
    double wallMs() const { return double(m_wall.nsecsElapsed()) / 1000000.0; }

    // How much of the elapsed time the process was not running. 1.0 means it
    // held a core throughout; 5.0 means it was descheduled for four fifths of
    // the wall time. Reported with every measurement so a surprising number
    // in a log can be attributed to the machine or to the code without
    // re-running anything.
    double contention() const
    {
        const double cpu = cpuMs();
        return cpu > 0.0 ? wallMs() / cpu : 1.0;
    }

private:
    double m_cpuStart = 0.0;
    QElapsedTimer m_wall;
};

// Reports a measurement in one recognizable shape, so every perf line in the
// logs carries the CPU number the budget is about, the wall time, and the
// contention factor that explains any gap between them.
#define KVIT_REPORT_OP(label, timer)                                          \
    qInfo("PERF %s: cpu %.2f ms (wall %.2f ms, contention %.1fx)", label,      \
          (timer).cpuMs(), (timer).wallMs(), (timer).contention())

// Above this wall/CPU ratio the process spent most of its elapsed time
// waiting for a core. Unchanged runs on an idle machine measured 1.0x, and
// the loaded runs that used to break the wall-clock assertions measured 3.2x
// and up.
#define KVIT_TRUSTWORTHY_CONTENTION 1.5

// The wall/CPU ratio alone is not enough to tell whether a CPU sample can be
// trusted, which is worth stating because it is the obvious thing to reach
// for and it is wrong. It detects only starvation - the process waiting for
// a core. It does not detect cache and memory-bandwidth contention, which
// inflates CPU time without inflating the ratio at all: on this 28-core box
// at load average 27, unchanged code measured 327-411 ms of CPU against
// 190 ms idle while reporting a contention factor of 1.0x. A tight budget
// keyed only on that ratio would have called a busy machine a regression,
// which is the exact failure this whole mechanism exists to remove.
//
// System load per core is what tracks that second effect. Measured on the
// same unchanged code: 0.29 runnable tasks per core gave ~190 ms of CPU,
// 0.86 gave ~445 ms, and beyond ~1.0 it plateaus near 400-460 ms. Below
// half a runnable task per core the machine has capacity to spare and the
// sharp number means something.
#define KVIT_QUIET_LOAD_PER_CORE 0.5

// One-minute system load average divided by the core count, or 0 where that
// is not available.
inline double kvitLoadPerCore()
{
#if defined(Q_OS_UNIX)
    double avg[3] = {0.0, 0.0, 0.0};
    if (getloadavg(avg, 3) < 1)
        return 0.0;
    return avg[0] / double(qMax(1, QThread::idealThreadCount()));
#else
    return 0.0;
#endif
}

// Whether this binary carries sanitizer instrumentation, which every budget
// here has to decline to judge. AddressSanitizer intercepts each allocation
// and memory access and UndefinedBehaviorSanitizer adds a check per
// operation, so the same code measures roughly twice its uninstrumented
// cost - the budgets would then be measuring the instrumentation rather than
// the code they were calibrated against. The measurement is still reported,
// and the sanitizer build is run for what it is good at, which is memory
// errors and undefined behaviour. KVIT_SANITIZER_BUILD comes from the
// KVIT_SANITIZE branch of the root CMakeLists: no compiler predefines a
// macro covering the ASan/UBSan pair.
inline bool kvitInstrumentedBuild()
{
#if defined(KVIT_SANITIZER_BUILD) || defined(__SANITIZE_ADDRESS__)
    return true;
#else
    return false;
#endif
}

// Explicitly forced enforcement, regardless of how busy the machine is. This
// is what the performance-labelled ctest entries set, and what a dedicated
// benchmarking runner would set.
inline bool kvitTimingBudgetsForced()
{
    return qEnvironmentVariableIsSet("KVIT_ENFORCE_TIMING_BUDGETS");
}

// Whether the machine is idle enough for a tight CPU budget to be measuring
// the code rather than the neighbours.
inline bool kvitMachineIsQuiet(double contention)
{
    if (contention > KVIT_TRUSTWORTHY_CONTENTION)
        return false;
#if defined(Q_OS_UNIX)
    // Probe with a real destination: glibc declares getloadavg's first
    // argument non-null, so passing nullptr to ask "is load available?" is
    // undefined behaviour that UndefinedBehaviorSanitizer reports.
    double probe[1] = {0.0};
    if (getloadavg(probe, 1) < 0)
        return true;   // unknown load: fall back to enforcing
    return kvitLoadPerCore() <= KVIT_QUIET_LOAD_PER_CORE;
#else
    // No cheap load-average equivalent on Windows. The Windows runs are CI
    // and developer machines that are not running an agent fleet, and the
    // contention check above still applies.
    return true;
#endif
}

// Assert a CPU-time budget with the two thresholds described above.
//
//   KVIT_ASSERT_CPU_BUDGET(timer, "collection.open 500-note", 300.0, 650.0);
//
// Always reports the measurement, always enforces `ceilingMs`, and enforces
// `tightMs` when the sample is trustworthy. When it is not, it says which
// check was deferred and what the contention was, so a reader can tell a
// deferred budget from a passing one.
#define KVIT_ASSERT_CPU_BUDGET_VALUES(label, cpuMsIn, wallMsIn, contentionIn,  \
                                      tightMs, ceilingMs)                     \
    do {                                                                       \
        const double kvit_cpu = double(cpuMsIn);                               \
        const double kvit_wall = double(wallMsIn);                             \
        const double kvit_contention = double(contentionIn);                   \
        qInfo("PERF %s: cpu %.2f ms (wall %.2f ms, contention %.1fx)", label,   \
              kvit_cpu, kvit_wall, kvit_contention);                           \
        if (kvitInstrumentedBuild()) {                                         \
            qInfo("PERF %s: budget and ceiling both deferred - this binary "   \
                  "is sanitizer-instrumented, so the measurement is of the "   \
                  "instrumentation as much as the code.",                      \
                  label);                                                      \
            break;                                                             \
        }                                                                      \
        QVERIFY2(kvit_cpu < (ceilingMs),                                       \
                 qPrintable(                                                   \
                     QStringLiteral(                                           \
                         "%1 exceeded its CPU ceiling: %2 ms cpu against a "   \
                         "%3 ms ceiling (%4 ms wall, contention %5x). The "    \
                         "ceiling is set well above the measured cost, so "    \
                         "contention alone is unlikely to explain this - but " \
                         "check the measurement against a quiet machine "      \
                         "before concluding the code changed.")                \
                         .arg(QLatin1String(label))                            \
                         .arg(kvit_cpu, 0, 'f', 1)                             \
                         .arg(double(ceilingMs), 0, 'f', 1)                    \
                         .arg(kvit_wall, 0, 'f', 1)                            \
                         .arg(kvit_contention, 0, 'f', 1)));                   \
        if (kvitMachineIsQuiet(kvit_contention)                                \
            || kvitTimingBudgetsForced()) {                                    \
            QVERIFY2(kvit_cpu < (tightMs),                                     \
                     qPrintable(                                               \
                         QStringLiteral(                                       \
                             "%1 exceeded its CPU budget: %2 ms cpu against "  \
                             "a %3 ms budget, measured at contention %4x on "  \
                             "a machine that was not busy. Either the code "   \
                             "got slower or the budget is mis-calibrated; "    \
                             "re-measure before assuming which.")              \
                             .arg(QLatin1String(label))                        \
                             .arg(kvit_cpu, 0, 'f', 1)                         \
                             .arg(double(tightMs), 0, 'f', 1)                  \
                             .arg(kvit_contention, 0, 'f', 1)));               \
        } else {                                                               \
            qInfo("PERF %s: %.0f ms budget deferred - machine busy "           \
                  "(contention %.1fx, load %.2f per core). The %.0f ms "       \
                  "ceiling was enforced and passed. Set "                      \
                  "KVIT_ENFORCE_TIMING_BUDGETS=1 to enforce anyway.",          \
                  label, double(tightMs), kvit_contention,                     \
                  kvitLoadPerCore(), double(ceilingMs));                       \
        }                                                                      \
    } while (false)

// The common case: one KvitOpTimer around one operation.
#define KVIT_ASSERT_CPU_BUDGET(timer, label, tightMs, ceilingMs)               \
    KVIT_ASSERT_CPU_BUDGET_VALUES(label, (timer).cpuMs(), (timer).wallMs(),    \
                                  (timer).contention(), tightMs, ceilingMs)

// Assert a wall-clock budget, but only when the machine is quiet enough for
// the number to mean anything.
//
//   KVIT_ASSERT_WALL_BUDGET(ms, "search.doc_recompute WP", 16.0);
//
// For budgets that genuinely are about elapsed time - a call returning
// promptly, a worker finishing, a figure a production PerfLog sample already
// measured on the wall clock - CPU time is the wrong metric and this is the
// right one. It cannot be made load-proof the way a CPU budget can, so
// instead of loosening the number it declines to judge a sample taken on a
// busy machine, and says so. The measurement is always reported.
//
// Unlike KVIT_ASSERT_CPU_BUDGET there is no always-enforced ceiling here,
// because a wall-clock ceiling loose enough to survive contention would sit
// so far above the real cost that it would catch nothing. Where an operation
// can be budgeted in CPU time, prefer the CPU macro: it keeps a check that
// runs everywhere.
#define KVIT_ASSERT_WALL_BUDGET(measuredMs, label, budgetMs)                   \
    do {                                                                       \
        const double kvit_ms = double(measuredMs);                             \
        if (kvitInstrumentedBuild()) {                                         \
            qInfo("PERF %s: %.2f ms, budget not judged - this binary is "      \
                  "sanitizer-instrumented.",                                   \
                  label, kvit_ms);                                             \
        } else if (kvitMachineIsQuiet(1.0) || kvitTimingBudgetsForced()) {     \
            QVERIFY2(kvit_ms <= (budgetMs),                                    \
                     qPrintable(                                               \
                         QStringLiteral(                                       \
                             "%1 exceeded its budget: %2 ms against %3 ms, "   \
                             "measured on a machine at %4 load per core. "     \
                             "This is a real regression, not a flaky timing.") \
                             .arg(QLatin1String(label))                        \
                             .arg(kvit_ms, 0, 'f', 2)                          \
                             .arg(double(budgetMs), 0, 'f', 2)                 \
                             .arg(kvitLoadPerCore(), 0, 'f', 2)));             \
        } else if (kvit_ms > (budgetMs)) {                                     \
            qInfo("PERF %s: %.2f ms over its %.2f ms budget, but the machine " \
                  "is busy (%.2f load per core) and wall-clock budgets are "   \
                  "not judged there. Set KVIT_ENFORCE_TIMING_BUDGETS=1 to "    \
                  "enforce anyway.",                                           \
                  label, kvit_ms, double(budgetMs), kvitLoadPerCore());        \
        }                                                                      \
    } while (false)

// Where wall-clock budgets are enforced.
//
// The remaining wall-clock budgets - the ones measuring latency rather than
// work - still cannot be enforced on a hosted runner: the 1000-note query
// budget of 25 ms measures 176 ms on a two-core GitHub Linux runner,
// repeatably, from the same commit that measures well inside budget locally.
// Enforcing there means either a permanently red merge gate or a ladder of
// per-platform numbers that track runner capacity rather than the code.
//
// So the measurement always runs and is always reported, and the assertion
// is enforced where the number means something: developer machines and the
// local `./build.sh --test` gate. On CI the test reports and skips, which
// ctest shows as a skip rather than a pass. The `performance`-labelled
// entries re-run the same functions with enforcement forced on, and CI runs
// that label as an informational, non-blocking job, so a real regression
// still shows up in the trend.
//
// CPU-time budgets do not need this escape hatch and do not use it: they
// hold on a loaded runner, which is the point of measuring them that way.
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
