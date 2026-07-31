# Multi-window, multi-vault

## Status: built

Everything this document plans has shipped. Kvit Notes opens any folder as a
vault and any loose file on its own, keeps several vaults open at once in
separate windows of one process, remembers the last session's vaults and a
recent list, and routes a second launch to the running instance rather than
starting a competing process.

Where each piece lives:

| The plan | Where it is |
|---|---|
| Open File… / Open Folder… and the recent-vaults list | `qml/Toolbar.qml`, `qml/DocumentSessionDialogs.qml`, `AppActions::requestOpenVault` |
| Remembered vaults (`session.openVaults`, `session.recentVaults`) | `WindowRegistry::openSession`, `persistOpenVaults`, `recordRecentVault` |
| Per-vault state split from process-global state | `AppContext` (per window) and `ProcessServices` (per process), composed by `VaultWindow` |
| The window/vault registry | `src/qml/windowregistry.{h,cpp}` |
| The single-instance channel | `src/platform/singleinstance.{h,cpp}`, `SingleInstanceTests` |
| Open routing | `WindowRouter`, implemented by `WindowRegistry` |
| Quit and tray behaviour with N windows | `KvitApplication::start`, `setQuitOnLastWindowClosed`, `AppActions::trayTarget` |

Read the rest of this file as the design record: what the problem was, what was
decided, and why. It is not a description of missing work.

## What this was

Kvit Notes opened exactly one notes collection ("vault") in one window, and it
decided which vault to open with no help from the user: a normal launch opened
a fixed default directory, and there was no menu, setting, or memory of a
previously used vault. This document planned the work to make the application
behave the way a folder-based editor such as VS Code or Obsidian does — open
any folder as a vault, open a loose file on its own, keep several vaults open
at once in separate windows, and route a second launch to the window that
already has that vault open instead of starting a competing process.

The plan was deliberately split into two work items. Phase 1 was small and
self-contained: surface the open actions that the code could already perform,
and remember where the user was. Phase 3 was the larger change that made the
application multi-window and multi-vault. (The numbering keeps a gap where an
intermediate "single-instance IPC" step was originally imagined; in a
multi-window, single-process design that step is not separable and was folded
into Phase 3, described there.)

## How notes locations work today

The relevant behavior lives in `AppContext::applyStartupArguments`
(`src/qml/appcontext.cpp`). It reads the first command-line argument and:

- if it names an existing **file**, opens that file in single-file mode
  (`DocumentManager::open`), taking no vault;
- if it names an existing **directory**, opens it as the vault root
  (`StartupController::setRootPath` → `NoteCollection::openRootAsync`);
- if there is no argument, falls back to a fixed default,
  `<Documents>/Kvit` (`QStandardPaths::DocumentsLocation` + `"Kvit"`). On
  Windows that is `C:\Users\<user>\Documents\Kvit`.

So the file-versus-folder distinction the target model needs already exists at
the command line. What is missing is everything around it:

- **No in-application open actions.** Nothing in the UI opens a folder or a
  file; the only way to point the app at a different vault is a command-line
  argument. A normal double-click launch always lands on `<Documents>/Kvit`.
- **No memory.** The default is recomputed every launch, so the app never
  reopens the vault the user last worked in.
- **No handoff between launches.** Each launch is a new process.

What is already solid, and shapes the rest of this plan, is the cross-process
safety model in [ADR 0005](docs/adr/0005-multi-process-behaviour.md). `VaultLock`
takes a kernel advisory lock per vault; a second process that opens the same
vault is refused with a message naming the current holder, so two instances can
never silently overwrite each other's notes. Single-file mode takes no lock,
because it opens none of the shared vault state. ADR 0005 names the missing
handoff explicitly as deferred future work: "handing off to the running
instance … needing single-instance IPC through `QLocalServer` and a window-raise
protocol." Phase 3 builds exactly that, so the two windows the lock currently
forbids become a raise of the existing window instead of a refusal.

The application also runs as a single process composing one set of services in
`AppContext`: one `NoteCollection`, `BlockModel`, `DocumentManager`,
`UndoStack`, search index, and file watcher, all bound to one window through one
`QQmlApplicationEngine`. Some services are inherently per-vault; others are
process-wide (the settings store, the system tray, the update checker, the
global hotkey). Separating those two groups is the core of Phase 3.

## Target model

One process hosts several windows. Each open vault has its own window with its
own vault-scoped state; each loose file opened on its own also gets a window.
Every request to open something — from a menu, from a command-line argument, or
from a second launch that the OS starts — is routed to the one running process,
which either raises the window that already shows that vault or file, or creates
a new one. The per-vault kernel lock stays as the cross-process backstop for the
case where a genuinely separate process (a different user, a different machine
on a shared filesystem) contends for the same vault.

## Planned work

### Phase 1 — Open actions and remembered vaults

The goal is to close the "I cannot change where notes live" gap using wiring
that mostly already exists, without yet changing the one-window model.

**Scope**

- **Open File… / Open Folder… menu actions.** Add both to the application menu.
  "Open File…" can reuse `DocumentManager::openFileDialog`
  (`src/application/documentmanager.h`). "Open Folder…" needs a native folder
  picker (a QML `FolderDialog`) wired to the existing runtime vault-open path,
  `AppContext::openRootAsync` / `NoteCollection::openRootAsync`, which already
  handles switching away from the currently open vault (it closes the previous
  vault's search index before opening the next).
- **Remember the last vault, and recent vaults.** Persist the last successfully
  opened vault path, and a short most-recently-used list, through the existing
  settings store (`SettingsStore::setValue` / `value`,
  `src/platform/settingsstore.cpp`). On a no-argument launch, open the
  remembered vault instead of the fixed `<Documents>/Kvit`. Keep
  `<Documents>/Kvit` as the first-run default when nothing is remembered, and
  create it if absent, so a brand-new install still lands somewhere sensible.
- **A recent-vaults entry point.** Surface the recent list (menu submenu or a
  small startup affordance) so switching between known vaults does not require
  the folder picker each time.

**Behavior in this phase.** Still one window and one vault at a time: "Open
Folder…" switches the current window to the chosen vault, the same in-place
switch `openRootAsync` already performs. Multiple simultaneous vaults wait for
Phase 3.

**Files likely touched.** `qml/` menu and dialogs; `src/qml/appcontext.*` for a
remembered-vault read on startup and a recent-list accessor;
`src/platform/settingsstore.*` only as a consumer (its API already suffices);
possibly `StartupController` for the "open remembered vault" path.

**Tests.** The remembered-vault and recent-list logic is deterministic and
headless-testable (settings round-trip, first-run default when nothing is
stored, recent-list ordering and de-duplication). The dialogs themselves are
thin wrappers over existing invokables and need no new automated coverage beyond
what `DocumentManager` already has.

### Phase 3 — Multi-window, multi-vault (absorbs the single-instance IPC)

This is the architectural work: turn the single-window process into one that
hosts a window per vault and routes every open request through a single running
instance. What an earlier sketch called "Phase 2" — the single-instance
inter-process channel and window-raise protocol — is not a separable deliverable
here, because in a single-process, multi-window design that channel *is* how
opens reach the right window. It is therefore part of this phase.

**Scope**

- **Separate per-vault state from process-global state.** `AppContext` currently
  composes one of everything. The per-vault services (an initial reading, to be
  confirmed by an audit: `NoteCollection`, the collection search index,
  `DocumentManager`, `BlockModel`, `UndoStack`, the file watcher, the startup
  controller) must become one set per open vault, while the process-global
  services (the settings store, the system tray, the update checker, the global
  hotkey, the extension and block-kind registries, the network egress policy)
  stay single and are shared across windows. This split is the bulk of the work
  and its risk; it should be driven by an explicit audit of every member of
  `AppContext` rather than the list above.

- **A window/vault registry.** A process-global object that maps a canonical
  vault path (and each open loose file) to its window, creates a window when a
  vault is opened, and tears its per-vault state down when the window closes
  (releasing that vault's lock). "Already open here" is answered by this
  registry.

- **Single-instance channel.** A `QLocalServer` (a named pipe on Windows, a
  local socket on Unix — both supported by Qt) owned by the first process. A
  later launch connects as a client, forwards its request (a file path, a folder
  path, or "no argument"), and exits; the first process acts on the forwarded
  request. This removes the accumulating-process behavior that Windows QA sees
  today, where every launch starts a new tray-resident process.

- **Open routing.** One routine handles a request whatever its source (menu,
  command-line argument, or a forwarded launch): a vault already shown raises its
  window; a new vault opens a new window; a file opens a single-file window
  (reusing existing single-file mode, pinned by `SingleFileModeTests`). Raising a
  window must activate and focus it on each platform.

- **Interaction with the vault lock.** The kernel lock in ADR 0005 stays as the
  cross-process guarantee. Within the one process, the window registry answers
  "this vault is already open" and raises the existing window rather than
  reaching the in-process one-writer refusal, which realizes the handoff ADR 0005
  deferred. A vault held by a *different* process is still refused with the
  holder message, unchanged.

- **Lifecycle with several windows.** Define quit and tray behavior for N
  windows: when the last window closes the process quits unless close-to-tray is
  enabled and a tray exists (the existing `setQuitOnLastWindowClosed` policy,
  generalized from one window to the set), and decide what the tray shows when
  several vaults are open.

- **Cross-platform.** The single-instance channel, window activation, and lock
  interaction must all be verified on Windows and Linux (and macOS), since the
  named-pipe/socket and window-raise details differ per platform.

**Tests.** Extend the existing multi-process approach in `VaultLockTests`
(`tests/test_vaultlock.cpp`), which already drives real second processes through
`QProcess`, to cover: a second launch forwarding its request and the first
process raising or creating the right window; per-vault state isolation (an edit
or undo in one vault's window does not touch another's); and the last-window quit
and tray behavior. The per-vault/global split should also be checked by
constructing two vault contexts in one test process and asserting they share the
global services and nothing else.

**Documentation.** [ADR 0005](docs/adr/0005-multi-process-behaviour.md) records
the handoff as deferred; when Phase 3 lands, update it (or add a follow-on ADR)
so the accepted decision matches the built behavior, and revise the "one vault
can be open in one window" message the refused session shows.

## Relationship to the QA checklist

Phase 3 settles the open decision recorded in `docs/qa-checklist.md` under the
Windows watch-list (the "second launch while the first is open" item, where the
behavior is "decision pending"): the decided behavior becomes "route the launch
to the running instance and raise the matching window." Phase 1 does not change
that behavior but removes the more basic gap the checklist does not yet cover —
that a user cannot choose where notes live from inside the app.
