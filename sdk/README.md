# SDK

Public adapter SDK — **implemented (ABI v1): stable C ABI + dynamic loading**.

Third-party adapters are shared libraries that export one function:

```c
const envbox_adapter_api* envbox_adapter_get_api(void);
```

The host validates the ABI and wraps the adapter in the same `envbox::engine::Adapter`
interface used by built-in adapters, so an external adapter participates in capture,
restore planning, backups, conflict detection and integrity checks like any other.

## Files

| Path | Purpose |
|------|---------|
| `include/envbox/sdk/envbox_adapter.h` | pure C ABI (C99-clean, no C++ required) |
| `include/envbox/sdk/loader.hpp` | C++ host side: `load_dynamic_adapter()`, `wrap_c_adapter()` |
| `src/sdk/loader.cpp` | `LoadLibraryW` / `dlopen` + ABI validation, keeps the library alive |
| `src/sdk/adapter.cpp` | `CApiAdapter`: forwards engine calls into the C ABI |
| `sdk/examples/example_adapter.c` | complete worked example (~120 lines of C) |
| `sdk/examples/bad_abi_adapter.c` | conformance negative case (ABI 999) |
| `sdk/examples/missing_symbol_adapter.c` | conformance negative case (no entry point) |

## ABI summary

```c
typedef struct envbox_adapter_api {
    uint32_t abi_version;
    const envbox_manifest* (*manifest)(void);
    envbox_status (*capture)(const envbox_host*, envbox_capture_sink*, char*, size_t);
    envbox_status (*resolve)(const envbox_host*, const char* const*, size_t,
                             envbox_resolve_sink*, char*, size_t);
} envbox_adapter_api;
```

The host passes services instead of absolute paths, so adapters stay portable:

- `get_env(key)` — environment lookup (`NULL` when unset)
- `home_dir()`, `config_dir()` (XDG config / `%APPDATA%`), `data_dir()` (XDG data / `%LOCALAPPDATA%`)

Capture reports payload files through `add_file(payload_path, data, size)` using profile
paths such as `applications/<id>/config.json`; warnings via `add_warning()`. Restore reports
each payload path's absolute target via `add_target(payload_path, target, note)`; simply not
reporting a path means "skipped" and the engine surfaces a warning instead of dropping it
silently. Failures return an `envbox_status` (mapped to `envbox::ErrorCode`) plus a
human-readable message in the error buffer.

## Lifetime & safety rules

- Strings returned by host callbacks are valid **during the current call only** — copy them.
- Payload bytes passed to `add_file` are copied by the host before the call returns.
- The adapter library stays loaded as long as the returned `DynamicAdapter` is alive
  (`load_dynamic_adapter` returns both the `adapter` and its `library` keep-alive).
- Engine calls are serialized; adapters do not need to be thread-safe for v1.
- Adapters must not write to the target machine during `capture` or `resolve`.

## Versioning

`ENVBOX_SDK_ABI_VERSION` (currently `1`) must match both `envbox_adapter_api::abi_version`
and `envbox_manifest::abi_version`. Mismatches are rejected with an `unsupported` error
before any adapter code runs. Additive ABI changes bump the version; the host refuses
unknown versions rather than guessing.

## Writing your own adapter

1. Include `envbox_adapter.h`, implement manifest/capture/resolve, export
   `envbox_adapter_get_api()`.
2. Build as a shared library (`SHARED`/`MODULE`); on Windows the export macro expands to
   `__declspec(dllexport)`.
3. Run the SDK conformance tests conceptually: load succeeds, capture returns schema-valid
   payload paths (`applications/<id>/...`), resolve returns absolute targets, every failure
   path returns a status + message.
4. Load it with `envbox::sdk::load_dynamic_adapter(path)` and register it in the engine.

Known v1 limitation: the analysis/adaptation hook (`Adapter::adapt_payload`, used by the
VS Code adapter for Windows→Linux warnings) is not exposed across the ABI yet. External
adapters currently capture and restore verbatim.

## Build

The SDK is part of the normal build:

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build -R Sdk --output-on-failure
```

The example adapter is built as `envbox_example_adapter.dll`/`.so` next to the test binaries
and exercised by `tests/sdk/test_sdk.cpp` (load, ABI rejection, capture, resolve,
error propagation, and a full capture→restore roundtrip through the engine).
