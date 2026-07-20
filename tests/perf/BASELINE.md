# Phase 0 Performance Baseline

Captured 2026-07-08 on this workspace with Qt 6.10.1, GCC 11.4.1,
Release-style CMake build in `build/`.

Refresh commands:

```sh
./build/tests/test_performance_warandpeace -v2
./build/tests/test_performance_ops -v2
QT_QPA_PLATFORM=offscreen ./build/tests/test_performance_app -v2
```

## Existing Proxy Gate

| Operation | Fixture | Baseline |
|---|---:|---:|
| `DocumentSerializer::loadIntoModel` | WP-SYNTH, 6,241 blocks | 40 ms |
| Serialize | WP-SYNTH | 5 ms |
| Bulk delete | WP-SYNTH | 46 ms |
| Bulk undo | WP-SYNTH | 3 ms |

## Phase 0 Headless PerfLog Gates

| Operation id | Fixture | Baseline |
|---|---:|---:|
| `statusbar.count` | WP, 6,241 blocks / 562,900 words | 4,575.67 ms |
| `startup.scan` | VAULT-10K, 10,003 notes | 25,728.29 ms |
| `search.doc_recompute` | WP, query `Prince` | 4,475.76 ms |
| `outline.rebuild` | HEADINGS-2K | 104.41 ms |
| `block.bulk_delete` | WP-SYNTH | 46.48 ms |
| `selection.prune` | WP-SYNTH | 1.69 ms |

## Phase 0 GUI Harness

Offscreen Qt Quick run; advisory because windowing and render timing vary.

| Operation id | Fixture | Baseline |
|---|---:|---:|
| `note.open` | WP | 42.67 ms |
| `startup.first_frame` | WP loaded before QML window | 17,938.00 ms |
| `statusbar.count` | WP through QML status bar | 13,153.85 ms |
