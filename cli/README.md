# CLI

`envbox` command line tool. **Implemented: capture, inspect, restore.**

```
envbox capture --profile workstation
envbox inspect workstation.envbox
envbox inspect workstation.envbox --json
envbox restore workstation.envbox --dry-run
envbox restore workstation.envbox --dry-run --os linux
envbox restore workstation.envbox
envbox restore workstation.envbox --force
```

Behavior:

- `capture` defaults to `<name>.envbox` in the current directory; adapters run
  best-effort and skipped applications are reported with reasons
- `inspect` verifies the profile (format, paths, BLAKE3 index) before printing anything;
  `--json` emits the full profile as JSON
- `restore` always verifies first, backs up pre-existing files to the envbox backup root
  with a `backup.json` manifest, and refuses to overwrite without `--force`
- `--dry-run` prints the plan without touching disk; `--os <target>` analyzes the restore
  for another OS (adaptation warnings only; file placement always follows the host)

Exit codes: 0 ok, 1 error, 2 integrity verification failed, 3 conflicts require `--force`.

Planned: `diff` (preview changes in detail), `--app git,vscode` scoping, `--json` on all commands.
