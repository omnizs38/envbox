# Envbox

**Capture your application settings once. Restore them on any OS.**

Envbox captures application configuration (VS Code, Git, Firefox, terminal, …),
normalizes it into a single portable `.envbox` profile, and restores it on another
machine — adapting paths, values, and incompatibilities to the target operating
system instead of blindly copying files.

> Status: **pre-alpha. Core, engine, all three first adapters (Git, VS Code, Firefox), the CLI
> and the cross-OS compatibility layer are implemented and tested.** Remaining: desktop UI, SDK,
> CLI `diff`.

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

Requires: C++23 compiler, CMake ≥ 3.25, nlohmann/json, zstd, BLAKE3, GoogleTest.

On Windows with MSYS2:

```powershell
pacman -S --needed mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja `
    mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-zstd `
    mingw-w64-x86_64-libblake3 mingw-w64-x86_64-gtest

$env:Path = "C:\msys64\mingw64\bin;$env:Path"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Layout

```
include/envbox/core/    public API of the core library
src/core/               .envbox container, ustar, profile (de)serialization + verification
src/platform/           cross-platform path resolution
src/engine/             adapter API, capture + restore (plan/apply), backup-before-restore
src/adapters/git.cpp    Git adapter (global .gitconfig / XDG git config)
src/adapters/vscode.cpp VS Code adapter (settings/keybindings/snippets/extensions + OS-mismatch analysis)
src/adapters/firefox.cpp Firefox adapter (profiles.ini discovery, prefs.js sanitized into user.js)
src/compat/             path classification/conversion between Windows, Linux and macOS
src/cli/main.cpp        envbox CLI (capture, inspect, restore)
tests/                  GoogleTest suite (core, engine, adapters)
engine/                 design notes for the engine components
adapters/               design notes for adapters: vscode, git, firefox
platform/               design notes for OS-specific backends
sdk/                    public adapter SDK for third-party adapters     (planned)
cli/                    command line tool (implemented)
desktop/                Dear ImGui desktop UI                           (planned)
docs/                   format specification and design notes
```

## Roadmap

1. **Envbox Core** — `.envbox` read/write/validate/verify (done)
2. **Capture / Restore engine** — adapter API, backup-before-restore, conflict detection (done)
3. **First adapters** — Git, VS Code, Firefox (done)
4. **Cross-platform compatibility** — path classification/conversion + per-adapter incompatibility detection (done; conversion is best-effort, not guessing)
5. **CLI** — `envbox capture|inspect|restore` (done); `diff` remaining
6. **Desktop** — Dear ImGui application

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
