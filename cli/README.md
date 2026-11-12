# CLI

`envbox` command line tool. **Planned — lands after the capture/restore engine.**

```
envbox capture --profile workstation
envbox inspect workstation.envbox
envbox restore workstation.envbox
envbox diff workstation.envbox
```

Design notes:

- `capture` defaults to `<name>.envbox` in the current directory; `--app git,vscode` limits scope
- `restore` always verifies the profile first, backs up existing files, and lists conflicts
  before writing; `--dry-run` prints the plan without touching disk
- exit codes: 0 ok, 1 generic error, 2 verification/integrity failure, 3 conflicts require `--force`
- machine-readable output via `--json` for scripting
