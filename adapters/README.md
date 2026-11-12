# Adapters

One independent module per supported application. **Planned — not implemented yet.**

Each adapter declares: where settings live (per OS), which files are captured,
which values are platform-dependent, how to adapt them for other targets,
where files go on restore, and which dependencies must be present.

Planned first adapters (in order):

1. `git/` — `~/.gitconfig` (Windows: `%USERPROFILE%\.gitconfig`); portable ini subset
2. `vscode/` — `settings.json`, `keybindings.json`, `snippets/`, extensions list via
   `code --list-extensions`; flags platform-dependent keys (e.g. terminal profile)
3. `firefox/` — `prefs.js` sanitization and `user.js` extraction

Adapters are built against the public adapter SDK (`sdk/`), never against Core internals.
