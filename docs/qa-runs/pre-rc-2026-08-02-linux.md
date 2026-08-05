# QA run: Linux (Wayland), pre-release-candidate

| Field | Value |
|---|---|
| Platform | Linux, Wayland desktop session |
| Artifact | AppImage from a `workflow_dispatch` packaging run |
| Version / commit | untagged; the tree as of 2026-08-02, before `f773c0b` |
| Checksum | not published (no tag) |
| Machine | desktop PC (Intel processor, NVIDIA RTX 5080), reached remotely with no monitor attached |
| OS version | Ubuntu 24.04 |
| Date | 2026-08-02 |
| Runner | owner |
| Result | blocked on first launch, then fixed |

The first real-hardware run of a Linux artifact, on the default Ubuntu
session, which is Wayland. Both findings stopped the artifact rather than
degrading it, and neither could have been caught from the development
machine: the AppImage runs there because that machine happens to carry the
libraries a stock Ubuntu 24.04 does not.

## Deviations

1. **The type-2 AppImage would not start.** Ubuntu 24.04 does not ship
   `libfuse2`, which a type-2 image needs to mount itself. Fixed in
   `f773c0b` by moving to a static AppImage runtime with no fuse2
   dependency, with direct-launch tests on Ubuntu 24.04 added.
2. **There was no native Wayland path.** The artifact carried no Qt
   Wayland platform plugin, so a Wayland session fell through to XWayland.
   Fixed in `6a9d5d9` and `f482225`, which bundle the Qt Wayland client and
   its client-integration plugins, with a headless Weston smoke test added
   to CI.

A separate defect was found and fixed in the same window: the quick-capture
window drew with an unthemed palette, because the theme was not applied to
that separate window (`4ed4179`).

## Not exercised

- A hand re-run of the fixed AppImage on this machine. The fixes are
  verified by the CI packaging job and the Weston smoke test, which is not
  the same as a desktop session on real hardware. **This is the outstanding
  step for Linux.**
- The real-GPU check (`QSG_INFO=1` naming the RTX 5080 rather than
  `llvmpipe`). The machine runs headless, so this may need a dummy plug or
  a streaming session to answer.
- Tray behaviour on GNOME with no extension installed.
- The distribution section, the screen-reader section (Orca), and the
  standing features pass.
- X11, by decision: see the Linux watch-list in
  [../qa-checklist.md](../qa-checklist.md).
