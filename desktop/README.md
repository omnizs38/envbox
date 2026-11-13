# Desktop

Dear ImGui desktop UI (GLFW + OpenGL 3.3). **Implemented (v1).**

Build: enabled by default (`-DENVBOX_BUILD_DESKTOP=OFF` to skip). The first configure
fetches GLFW 3.4 and Dear ImGui 1.91.8 via `FetchContent`, so it needs network access once.

```
cmake -S . -B build -G Ninja
cmake --build build
build/envbox_desktop.exe        (Windows)
build/envbox_desktop --check    (headless self-check: version, host, adapters)
```

Scope:

- **Capture** tab — profile name + output file, runs all registered adapters, reports
  captured/skipped/warnings (nothing is silent)
- **Restore** tab — load an `.envbox`, preview the full restore plan (targets, new vs
  overwrite, notes, warnings, missing adapters), then apply with explicit overwrite opt-in;
  backups are taken automatically
- **About** tab — host info and behavior summary

The app is a thin shell over `envbox::engine`; it contains no capture/restore logic of its
own. Logs of the last operation are shown in a scrollable pane; errors are highlighted.

Not done yet: native file pickers (paths are typed), profile library management, per-app
selection, conflict-by-conflict review. Those belong to the next UI iteration.
