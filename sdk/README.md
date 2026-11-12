# SDK

Public adapter SDK for third-party adapter developers. **Planned — not implemented yet.**

Goals:

- adapter authors link against `envbox-sdk` only; Core does not depend on any adapter
- stable C ABI for dynamic loading, plus a thin C++ convenience header
- adapter manifest describing id, version, supported OSes, and capture/restore entry points

Third-party adapters are verified against a conformance test suite shipped with the SDK.
Format details live in [../docs/format-v1.md](../docs/format-v1.md); adapter API design is
tracked as part of the capture/restore milestone.
