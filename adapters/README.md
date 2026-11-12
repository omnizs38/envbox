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

Planned:

2. `vscode/` — `settings.json`, `keybindings.json`, `snippets/`, extensions list; flags
   platform-dependent keys (e.g. terminal profile on Windows)
3. `firefox/` — `prefs.js` sanitization and `user.js` extraction

Adapters are built against the adapter interface (`include/envbox/engine/adapter.hpp`);
the public SDK will formalize this as a stable, dynamically loadable API.
