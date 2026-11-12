# Platform

OS-specific path resolution is implemented in `src/platform/paths.cpp` (`envbox::platform`):

- `home_directory()` — `%USERPROFILE%` on Windows (matches Git), `$HOME` elsewhere
- `xdg_config_home()` / `xdg_data_home()` — XDG resolution with sensible defaults
- `local_data_directory()` — `%LOCALAPPDATA%` on Windows, XDG data home elsewhere
- `envbox_backup_root()` — where `apply_restore()` stores pre-restore backups
- `get_env()` treats empty strings as unset everywhere

Windows-specific registry probing and macOS plist handling are not implemented yet.
Detection must degrade gracefully: unknown version strings are valid, "cannot detect" is a
first-class result reported to the user, never guessed.
