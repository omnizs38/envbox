# Engine

Capture / restore implemented in `src/engine/` (`envbox::engine`).

- `capture/` — `capture_profile()`: runs all registered adapters, records per-adapter failures as skipped entries (never silently drops an application), produces a `.envbox` profile
- `restore/` — `plan_restore()` / `apply_restore()`:
  - verifies the profile (open with hash verification) before planning
  - resolves payload files to target paths through the adapter
  - reports conflicts (existing targets) without writing anything
  - refuses to overwrite without `force`; backs up every pre-existing file to `<backup_root>/<timestamp>-<profile>/` with a `backup.json` manifest (BLAKE3 per entry) before writing
  - atomic writes (temp file + rename); `dry_run` mode never touches disk
- `migration/` — planned: convert a profile to a newer `format_version`
- `diff/` — planned: show what a restore would change

Unmanaged payload roots (e.g. `system/`) and declared-but-uncaptured applications are reported in the plan (`warnings` / `missing`), never silently ignored.
