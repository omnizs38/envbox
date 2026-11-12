# SDK

Public adapter SDK for third-party adapter developers. **Not implemented yet.**

What exists today: the adapter interface in `include/envbox/engine/adapter.hpp`
(`Adapter::capture()`, `Adapter::resolve_targets()`, `AdapterRegistry`). Built-in adapters
(`src/adapters/`) use it directly, and so can third parties by static linking.

What the SDK milestone adds:

- stable C ABI for dynamic loading + a thin C++ convenience header
- adapter manifest describing id, version, supported OSes, and entry points
- conformance test suite shipped with the SDK

Constraint: the engine never depends on adapter internals; adapters only see the SDK surface.
Format details live in [../docs/format-v1.md](../docs/format-v1.md).
