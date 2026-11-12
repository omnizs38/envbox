# Engine

Capture / restore / migration / diff engines. **Planned — not implemented yet.**

- `capture/` — collects adapter payloads, normalizes paths, produces a `.envbox` profile
- `restore/` — verifies a profile, backs up existing settings, applies payloads, reports conflicts
- `migration/` — converts a profile to a newer `format_version` (never done by the reader)
- `diff/` — shows what a restore would change before anything is written

The engine drives adapters; it contains no application-specific logic.
