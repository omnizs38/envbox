# Engine

Capture / restore implemented in `src/engine/` (`envbox::engine`).

- `capture/` — `capture_profile()`: runs all registered adapters (or a filtered subset via
  `CaptureOptions::only_adapters`), records per-adapter failures as skipped entries (never
  silently drops an application), produces a `.envbox` profile
- `restore/` — `plan_restore()` / `apply_restore()`:
  - verifies the profile (open with hash verification) before planning
  - resolves payload files to target paths through the adapter (or only selected
    applications via `PlanOptions::only_applications`)
  - reports conflicts (existing targets) without writing anything
  - refuses to overwrite without `force`; backs up every pre-existing file to `<backup_root>/<timestamp>-<profile>/` with a `backup.json` manifest (BLAKE3 per entry) before writing
  - atomic writes (temp file + rename); `dry_run` mode never touches disk
- `migration/` — `MigrationRegistry::migrate()`: upgrades a profile through registered
  versioned steps (`from_version` → `to_version`), rejects downgrades, missing steps and
  overshooting steps, re-computes the manifest index (BLAKE3) after each transform, and ships
  with an empty default registry (v1 is the current format; the machinery is ready for v2)
- `diff/` — `diff_profile()`: read-only comparison of a profile against the current machine,
  per-file `new` / `modified` / `identical` / `skipped` classification, unified line diffs for
  text files with hunk context and an optional line cap, binary detection, and propagation of
  warnings/missing adapters from the restore plan

Unmanaged payload roots (e.g. `system/`) and declared-but-uncaptured applications are reported in the plan (`warnings` / `missing`), never silently ignored.
