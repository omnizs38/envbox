# Adapters

One independent module per supported application.

Each adapter declares: where settings live (per OS), which files are captured,
which values are platform-dependent, how to adapt them for other targets,
where files go on restore, and which dependencies must be present.

Implemented:

1. `git/` (`src/adapters/git.cpp`) — captures the global git config (`GIT_CONFIG_GLOBAL`,
   else `$XDG_CONFIG_HOME/git/config`, else `~/.gitconfig`). Restore resolves the target on
   the machine doing the restore: existing XDG config wins, else existing `~/.gitconfig`,
   else host default (Linux: `~/.config/git/config`, Windows/macOS: `~/.gitconfig`).
2. `vscode/` (`src/adapters/vscode.cpp`) — captures `settings.json`, `keybindings.json`,
   `snippets/`, and an extensions list derived from `~/.vscode/extensions/` (no shell-out).
   On restore, settings are analyzed for OS incompatibilities and reported with suggestions
   (e.g. `terminal.integrated.defaultProfile.windows` set but restoring onto Linux → "consider
   bash/zsh"; Windows paths and Windows-only fonts in settings are flagged too). Files are
   written unchanged — analysis only, never silent edits; the cross-OS preview is available
   via `envbox restore profile.envbox --dry-run --os linux`. The extensions list is
   informational; extensions are not auto-installed yet.

Planned:

3. `firefox/` — `prefs.js` sanitization and `user.js` extraction

Adapters are built against the adapter interface (`include/envbox/engine/adapter.hpp`);
the public SDK will formalize this as a stable, dynamically loadable API.
