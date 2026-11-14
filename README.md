# Envbox

[![CI](https://github.com/omnizs38/envbox/actions/workflows/ci.yml/badge.svg)](https://github.com/omnizs38/envbox/actions/workflows/ci.yml)

**Capture your application settings once. Restore them on any OS.**

Envbox captures application configuration (VS Code, Git, Firefox, terminal, …),
normalizes it into a single portable `.envbox` profile, and restores it on another
machine — adapting paths, values, and incompatibilities to the target operating
system instead of blindly copying files.

> Status: **1.0 — all planned features implemented: core format, capture/restore/diff engine,
> Git / VS Code / Firefox adapters, cross-OS compatibility, CLI, SQLite profile library,
> adapter SDK (C ABI), migration engine and the Dear ImGui desktop app.** CI builds and tests
> on Linux, macOS and Windows.

## How it works

```
Windows                        profile.envbox                 Fedora Linux
┌─────────────────┐           ┌──────────────────┐          ┌─────────────────┐
│ VS Code  Git    │  Capture  │ manifest.json    │ Restore  │ VS Code  Git    │
│ Firefox Tml     │ ────────▶ │ profile.json     │ ───────▶ │ Firefox Tml     │
│ (source OS)     │           │ applications/…   │          │ (target OS)     │
└─────────────────┘           │ system/… assets/ │          └─────────────────┘
                              └──────────────────┘
```

- [docs/format-v1.md](docs/format-v1.md) — the `.envbox` container format specification
- Capture and restore are driven by **application adapters** that know where each
  application keeps its settings and which values are platform-dependent.

## Build

Requires: C++23 compiler, CMake ≥ 3.25, nlohmann/json, zstd, BLAKE3, SQLite3, GoogleTest.

On Windows with MSYS2:

```powershell
pacman -S --needed mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja `
    mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-zstd `
    mingw-w64-x86_64-libblake3 mingw-w64-x86_64-gtest mingw-w64-x86_64-sqlite3

$env:Path = "C:\msys64\mingw64\bin;$env:Path"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The desktop app (`envbox_desktop`) is built by default; its first configure fetches GLFW and
Dear ImGui via `FetchContent` (network required once). Use `-DENVBOX_BUILD_DESKTOP=OFF` to
build only the CLI and library.

Missing development packages are fetched automatically (nlohmann/json, GoogleTest, zstd and a
portable BLAKE3 build), so a stock toolchain is enough. CI (`.github/workflows/ci.yml`) builds
and tests on Ubuntu 24.04, macOS 15 and Windows (MSYS2/MinGW), including a desktop selftest and
end-to-end CLI smoke tests.

## Layout

```
include/envbox/core/    public API of the core library
src/core/               .envbox container, ustar, profile (de)serialization + verification
src/platform/           cross-platform path resolution
src/engine/             adapter API, capture + restore (plan/apply), diff, migration, backups
src/engine/migration.cpp migration steps between format versions + index re-computation
src/adapters/git.cpp    Git adapter (global .gitconfig / XDG git config)
src/adapters/vscode.cpp VS Code adapter (settings/keybindings/snippets/extensions + OS-mismatch analysis)
src/adapters/firefox.cpp Firefox adapter (profiles.ini discovery, prefs.js sanitized into user.js)
src/compat/             path classification/conversion between Windows, Linux and macOS
src/storage/library.cpp SQLite profile library (index, list, missing-file flags)
src/cli/main.cpp        envbox CLI (capture, inspect, diff, restore, profiles)
tests/                  GoogleTest suite (core, engine, adapters, SDK, storage)
engine/                 design notes for the engine components
adapters/               design notes for adapters: vscode, git, firefox
platform/               design notes for OS-specific backends
sdk/                    adapter SDK (C ABI v1 + loader + example adapter)
cli/                    command line tool (implemented)
desktop/                Dear ImGui desktop UI (implemented; GLFW + OpenGL)
docs/                   format specification and design notes
```

## Roadmap

1. **Envbox Core** — `.envbox` read/write/validate/verify (done)
2. **Capture / Restore engine** — adapter API, backup-before-restore, conflict detection (done)
3. **First adapters** — Git, VS Code, Firefox (done)
4. **Cross-platform compatibility** — path classification/conversion + per-adapter incompatibility detection (done; conversion is best-effort, not guessing)
5. **CLI** — `envbox capture|inspect|diff|restore` (done)
6. **Desktop** — Dear ImGui application (v1 done: capture, restore plan/apply, logs)
7. **Adapter SDK** — stable C ABI, dynamic loading, example adapter, conformance tests (done)
8. **Storage** — SQLite profile library: index, list, remove, missing-file detection (done)
9. **Migration engine** — versioned profile upgrades with index re-computation (done; no v2 format yet)

## Development principles

- `.envbox` is an **open format**; the spec lives in this repo.
- Adapters are **independent modules** with a public SDK; Envbox Core knows nothing
  about specific applications.
- Portable payloads (JSON/TOML/INI used as-is by applications) are stored as-is and
  described by adaptation rules, not rewritten into a custom normalized model.
- Envbox never silently overwrites: restore backs up existing settings first and
  reports conflicts.

## License

GPL-3.0 — see [LICENSE](LICENSE).
