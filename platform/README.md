# Platform

OS-specific backends used by the engine and adapters. **Planned — not implemented yet.**

- `windows/` — Known Folder paths (`%APPDATA%`, `%USERPROFILE%`), registry probing, `where.exe` detection
- `linux/` — XDG Base Directory resolution, `~/.config` vs dotfiles, package-manager hints
- `macos/` — `~/Library/Application Support`, `~/Library/Preferences`, plist handling

Detection helpers must degrade gracefully: unknown version strings are valid, "cannot detect" is a
first-class result reported to the user, never guessed.
