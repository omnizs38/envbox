# The `.envbox` Format (v1)

This document specifies container format version **1** of the `.envbox` profile format.
It is the reference for readers/writers implemented in `envbox::core`.

Status: **Stable for v1.** Changes that break compatibility require a `format_version` bump.

## 1. Design goals

- **Open.** A `.envbox` file is inspectable with standard tools (`zstd -d`, `tar`).
  No custom binary black boxes; the only novel parts are two JSON documents.
- **Integrity-first.** Every member of the archive is indexed in `manifest.json`
  by BLAKE3 hash and size. Readers MUST verify before applying anything.
- **Not a normalization jail.** Adapters decide which settings to capture and how to
  adapt them. Portable payloads (JSON/TOML/INI used as-is by the application) are
  stored byte-for-byte under `applications/<id>/` and are *not* rewritten by the format.
- **Forward compatible.** Readers MUST ignore unknown JSON keys and unknown top-level
  archive directories. Writers MUST NOT rely on readers understanding unlisted data.

## 2. Container layout

A `.envbox` file is:

```
offset  size  content
0       8     magic: 0x45 0x4E 0x56 0x42 0x4F 0x58 0x1A 0x01  ("ENVBOX" 0x1A 0x01)
8       ...   one Zstandard (RFC 8878) frame containing a POSIX ustar archive
```

- Byte 6 (`0x1A`, SUB) and byte 7 (`0x01`) version the *container layout*.
  If unknown, readers MUST refuse the file.
- After decompression, the payload is a `ustar` archive. Only regular files (`typeflag '0'`)
  and directories (`typeflag '5'`) are meaningful. `pax` extended headers (`'x'`) MAY appear
  and MUST be honored for the `path` and `size` keys; other keys MUST be ignored.
  Any other entry type MUST be rejected.
- Archive member names are `/`-separated, relative, UTF-8, normalized (no `.`/`..` components,
  no leading/trailing `/`, no drive letters or backslashes).

Reference commands to inspect a profile manually:

```sh
dd if=workstation.envbox bs=1 skip=8 | zstd -d | tar -tvf -
```

## 3. Required entries

| Path           | Required | Purpose                                  |
|----------------|----------|------------------------------------------|
| `manifest.json`| yes      | format identification + integrity index |
| `profile.json` | yes      | human-facing profile metadata            |

All other members MUST live under one of these roots:

- `applications/<id>/...` — files captured by an application adapter
- `system/...` — machine-wide environment (environment.json, variables.json, ...)
- `assets/...` — auxiliary binary assets (fonts, themes, ...)

## 4. `manifest.json`

```json
{
  "format": "envbox",
  "format_version": 1,
  "generator": { "name": "envbox", "version": "0.1.0" },
  "files": [
    { "path": "profile.json", "size": 342, "blake3": "a1b2..." },
    { "path": "applications/git/config", "size": 2048, "blake3": "c3d4..." }
  ]
}
```

| Field            | Type   | Rule                                                        |
|------------------|--------|-------------------------------------------------------------|
| `format`         | string | MUST equal `"envbox"`                                       |
| `format_version` | number | MUST be `1` for this spec. `> 1` ⇒ refuse (`unsupported`)   |
| `generator`      | object | informational; `{name, version}` strings                    |
| `files[]`        | array  | one index entry per archive member **except** `manifest.json` |

Each index entry:

| Field   | Type   | Rule                                             |
|---------|--------|--------------------------------------------------|
| `path`  | string | normalized member path (see §2), unique          |
| `size`  | number | byte length of the member, unsigned 64-bit       |
| `blake3`| string | 64 lowercase hex chars, BLAKE3 of member bytes   |

`manifest.json` itself is NOT indexed (it cannot hash itself). Its integrity is
protected only by the zstd frame and tar checksums — acceptable, because a tampered
manifest will fail verification of every other member it mis-describes.

Verification procedure (MUST be performed by readers before restore/diff/inspect):

1. For each `files[]` entry: the member must exist; its byte length must equal `size`;
   its BLAKE3 must equal `blake3`.
2. The set of archive members (minus `manifest.json`) MUST equal the set of `files[].path`.
   Extra unindexed members or missing members are an integrity error.
3. All index paths MUST pass the path rules of §2 and have an allowed root (§3).

## 5. `profile.json`

```json
{
  "name": "My Workstation",
  "description": "Primary dev machine",
  "tags": ["work", "windows"],
  "created_at": "2026-11-12T10:30:00Z",
  "source": { "os": "windows", "architecture": "x86_64" },
  "applications": [
    { "id": "vscode", "version": "1.106.0", "adapter": "vscode", "adapter_version": 1 },
    { "id": "git",    "adapter": "git",    "adapter_version": 1 }
  ]
}
```

| Field             | Type     | Rule                                                   |
|-------------------|----------|--------------------------------------------------------|
| `name`            | string   | required, non-empty                                    |
| `description`     | string   | optional, may be empty                                 |
| `tags`            | string[] | optional                                               |
| `created_at`      | string   | ISO 8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`)                  |
| `source.os`       | string   | one of `windows` \| `linux` \| `macos`                 |
| `source.architecture` | string | `x86_64` \| `arm64` (others tolerated on read)        |
| `applications[]`  | array    | may be empty; restore decides per-adapter behavior     |

Each application entry:

| Field             | Type   | Rule                                             |
|-------------------|--------|--------------------------------------------------|
| `id`              | string | required, non-empty, lowercase adapter id        |
| `version`         | string | optional; empty/missing means "unknown"          |
| `adapter`         | string | required; the adapter responsible for this entry |
| `adapter_version` | number | required, integer ≥ 1                            |

Readers MUST tolerate unknown keys everywhere. Writers MUST NOT emit keys
other than those documented without bumping `format_version`.

## 6. Versioning & compatibility rules

- A reader that understands `format_version = 1` must read any v1 file.
  Within v1, new *optional* JSON keys may appear; unknown ones MUST be ignored.
- `format_version > 1` MUST be refused. Downgrading semantics is never implicit.
- Adapter payload layouts (`applications/<id>/...`) are versioned per-adapter via
  `adapter_version`, NOT via `format_version`.
- Migrating a profile to a newer format version is the job of the `migration` engine,
  never of the reader.

## 7. Security & privacy

- Adapters SHOULD NOT capture secrets (tokens, passwords, private keys) into payloads.
  Envbox Core provides no secret-storage vault; adapters must redact or skip and report.
- Readers MUST NOT write any byte to disk until the full verification of §4 has passed.
- `open_profile(..., verify_hashes=false)` exists for tooling (e.g. listing) but such a
  profile MUST NOT be used as a restore source.
