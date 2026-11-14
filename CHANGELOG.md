# Changelog

## 1.0.0 — 2026-11-15

First full release: all planned features implemented and tested on Linux, macOS and Windows.

### Format & core
- `.envbox` container format v1: `ENVBOX` magic + Zstandard frame containing a ustar archive
- `manifest.json` (format id, BLAKE3/size index of every member) and `profile.json` (name, source, applications)
- Strict validation, path rules (no traversal/absolute/drive-letter paths), atomic writes
- Full O(n) integrity verification (member-set equality, sizes, BLAKE3) before any restore
- Compatibility layer: path classification and best-effort conversion (Windows ↔ Linux/macOS)
  with explicit exact/inexact results, URL-safe Windows-path detection

### Engine
- Adapter API + registry; capture with per-adapter skip reporting (`CaptureOptions`)
- Restore planning: target resolution, conflict detection, OS-preview analysis, application scoping
- Apply: refuses overwrite without `force`, backs up replaced files with a `backup.json`
  manifest (BLAKE3), atomic writes, dry-run mode
- Diff: read-only comparison with per-file new/modified/identical/skipped status and unified
  text diffs (LCS + hunks, truncation limits, binary detection)
- Migration: versioned upgrade steps with downgrade/missing-step rejection and index re-computation

### Adapters
- Git: global config discovery (`GIT_CONFIG_GLOBAL`, XDG, `~/.gitconfig`), cross-OS restore targeting
- VS Code: settings/keybindings/snippets, extensions list from disk, JSONC analysis with
  Windows→Linux warnings (terminal profile suggestions, path/font detection)
- Firefox: `profiles.ini` discovery, `prefs.js` sanitized (credentials/machine paths/hardware
  prefs removed) and merged into a startup-enforced `user.js`

### CLI
- `capture`, `inspect`, `diff`, `restore`, `profiles` commands
- `--app` scoping, `--json` machine-readable output, `--os` preview, `--dry-run`, `--force`
- Exit codes: 0 ok, 1 error, 2 integrity, 3 conflicts, 4 differences

### Storage
- SQLite profile library: verified upsert, list with missing-file flags, remove, find

### SDK
- Stable C ABI v1 (`envbox_adapter_get_api`), ABI/manifest validation, dynamic loading
  (`LoadLibraryW`/`dlopen`), C++ wrapper, example adapter + ABI/missing-symbol conformance tests

### Desktop
- Dear ImGui 1.91.8 + GLFW 3.4 application: Capture (per-app selection), Restore (plan preview,
  per-app selection, force opt-in), Diff (change table + diff pane), Library (SQLite index),
  About
- `--check` headless self-check and `--selftest` hidden-window GLFW/OpenGL/ImGui smoke test

### Quality
- 107 tests across core, engine, adapters, compat, storage, SDK, migration
- GitHub Actions CI on Ubuntu 24.04, macOS 15 (arm64) and Windows 2022 (MSYS2/MinGW):
  configure, build, full test suite, desktop selftest and end-to-end CLI smoke tests
