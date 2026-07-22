# Changelog

All notable changes to Kvit Notes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Single-file mode: `kvit-notes <file.md>` opens a lone markdown file with
  no vault. Collection chrome stays hidden, startup is immediate, and the
  full block editor (math, diagrams, tables, kanban) works in-file. A quiet
  status-bar line, "Create vault from this folder…", upgrades the file's
  folder into a vault without touching its files.
- Disclosed, opt-out update check: at startup, at most once per day, the
  editor asks the GitHub Releases API whether a newer version exists and
  shows a passive status-bar notice if so. Settings → General turns it off.
  No telemetry, no automatic download.
- `--math-selftest` flag: headless probe that verifies the installed math
  resources resolve and render (packaging QA).
- Cross-platform build presets (`CMakePresets.json`), install targets, and a
  relocatable math-resource path for packaged builds.
- Application icon.

### Changed

- The project is licensed under MPL-2.0 (LICENSE, per-file headers).

### Fixed

- Settings dialog: the title bar now drags the dialog, so it can be pushed
  aside to watch a theme or typography change land in the document behind
  it. The tab strip no longer spills past the dialog's right border — the
  theme cards had demanded more width than the dialog had, and now wrap.
- Inline math is no longer cut off or blurred. Equation bitmaps were exactly
  as wide as the formula's advance, so glyphs that paint outside it lost
  their ends (an italic `f` lost both its hook and its tail), and they were
  drawn at whatever sub-pixel offset the line's text advances added up to,
  which filtered them across neighbouring pixels. The renderer now leaves a
  transparent side margin, and the overlay places each image on a whole
  device pixel at its bitmap's own size.

## [1.0.0] - unreleased

The first public release: the full block editor (hybrid live-preview
markdown, the complete block palette, tables and kanban, callouts, toggles,
embeds, drop caps), a notes collection with folders, tags, global search,
wiki-links with backlinks and a quick switcher, LaTeX math with the NewTX
default face, five natively rendered Mermaid diagram families with
on-diagram editing, ASCII-diagram repair, import/export (Markdown, HTML,
PDF, plain text), themes and typography settings, autosave with backups and
crash recovery, and War-and-Peace-scale performance.
