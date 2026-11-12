# Desktop

Dear ImGui desktop UI. **Planned — last milestone.**

GUI stack decision: **Dear ImGui** (not Qt) — immediate mode, lightweight, keeps Envbox a
fast native C++ tool with no web runtime or heavy framework dependency.

Scope: application selection from detected adapters, capture/restore wizards with a diff
preview, conflict resolution dialogs, profile library management, integrity inspection.

The desktop app is a thin shell over the same engine library used by the CLI; all logic
lives in `engine/` + `adapters/`.
