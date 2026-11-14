# CLI

`envbox` command line tool. **Implemented: capture, inspect, diff, restore.**

```
envbox capture --profile workstation
envbox capture --profile workstation --app git,vscode --json
envbox inspect workstation.envbox
envbox inspect workstation.envbox --json
envbox diff workstation.envbox
envbox diff workstation.envbox --full --exit-code
envbox diff workstation.envbox --os linux
envbox diff workstation.envbox --app git --json
envbox restore workstation.envbox --dry-run
envbox restore workstation.envbox --dry-run --os linux --app git
envbox restore workstation.envbox
envbox restore workstation.envbox --force
envbox restore workstation.envbox --json
envbox profiles list
envbox profiles list --json
envbox profiles add workstation.envbox
envbox profiles remove workstation.envbox
```

Behavior:

- `capture` defaults to `<name>.envbox` in the current directory; adapters run
  best-effort and skipped applications are reported with reasons; `--app` limits capture
  to the given adapter ids
- `inspect` verifies the profile (format, paths, BLAKE3 index) before printing anything;
  `--json` emits the full profile as JSON
- `diff` compares a profile with the current machine without writing anything:
  per-file `new` / `modified` / `identical` / `skipped` status, warnings, missing
  adapters, and unified text diffs for modified files (`--full` lifts the 200-line
  cap per file). `--os` previews against another OS; `--app` restricts the comparison
  to selected application ids; `--exit-code` returns 4 when files would be created or
  modified
- `restore` always verifies first, backs up pre-existing files to the envbox backup root
  with a `backup.json` manifest, and refuses to overwrite without `--force`; `--app`
  scopes the restore plan to selected application ids
- `--dry-run` prints the plan without touching disk; `--os <target>` analyzes the restore
  for another OS (adaptation warnings only; file placement always follows the host)

`--json` is supported by `capture`, `inspect`, `diff` and `restore` for scripting
(stable top-level keys: `captured`/`skipped`/`warnings`, `changes`/`summary`,
`plan`/`applied`/`backup_dir`).

- `--app <ids>` scopes `capture`, `diff` and `restore` to the given application ids
  (comma-separated); unknown ids are reported, never silently ignored
- `profiles` manages a local SQLite library (default: `<envbox data dir>/library.db`):
  `add` verifies and indexes a profile, `list` shows name/source/apps/path and flags
  profiles whose file disappeared, `remove` drops the entry (never the file)

Exit codes: 0 ok, 1 error, 2 integrity verification failed, 3 conflicts require `--force`,
4 differences found (`diff --exit-code`).
