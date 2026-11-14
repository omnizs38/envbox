#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "envbox/adapters/firefox.hpp"
#include "envbox/adapters/git.hpp"
#include "envbox/adapters/vscode.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/capture.hpp"
#include "envbox/engine/diff.hpp"
#include "envbox/engine/restore.hpp"
#include "envbox/storage/library.hpp"

namespace {

using namespace envbox;

engine::AdapterRegistry make_registry() {
    engine::AdapterRegistry registry;
    registry.add(adapters::make_firefox_adapter());
    registry.add(adapters::make_git_adapter());
    registry.add(adapters::make_vscode_adapter());
    return registry;
}

struct AppState {
    char profile_name[128] = "workstation";
    char output_path[512] = "workstation.envbox";
    char profile_path[512] = "";
    char diff_path[512] = "";
    char library_path[512] = "";
    bool force = false;
    bool show_identical = true;
    bool capture_git = true;
    bool capture_vscode = true;
    bool capture_firefox = true;
    int selected_change = -1;
    int selected_library = -1;
    std::vector<std::string> selected_apps;
    std::vector<std::pair<std::string, bool>> log;
    std::optional<Profile> profile;
    std::optional<engine::RestorePlan> plan;
    std::optional<engine::ProfileDiff> diff;
    std::optional<std::vector<storage::LibraryEntry>> library_entries;

    void log_line(std::string text, bool is_error = false) {
        log.emplace_back(std::move(text), is_error);
    }
};

void refresh_library(AppState& state) {
    auto library = storage::Library::open_default();
    if (!library) {
        state.library_entries.reset();
        state.selected_library = -1;
        state.log_line("Library: " + library.error().message, true);
        return;
    }
    auto entries = library->list();
    if (!entries) {
        state.library_entries.reset();
        state.selected_library = -1;
        state.log_line("Library: " + entries.error().message, true);
        return;
    }
    state.library_entries = std::move(*entries);
    state.selected_library = -1;
}

void add_to_library(AppState& state) {
    auto library = storage::Library::open_default();
    if (!library) {
        state.log_line("Library: " + library.error().message, true);
        return;
    }
    auto status = library->add(state.library_path);
    if (!status) {
        state.log_line(status.error().message, true);
        return;
    }
    state.log_line("Added to library: " + std::string(state.library_path));
    refresh_library(state);
}

void remove_from_library(AppState& state) {
    if (!state.library_entries || state.selected_library < 0 ||
        state.selected_library >= static_cast<int>(state.library_entries->size())) {
        state.log_line("No library profile selected", true);
        return;
    }
    const auto path = (*state.library_entries)[static_cast<std::size_t>(state.selected_library)].path;
    auto library = storage::Library::open_default();
    if (!library) {
        state.log_line("Library: " + library.error().message, true);
        return;
    }
    auto status = library->remove(path);
    if (!status) {
        state.log_line(status.error().message, true);
        return;
    }
    state.log_line("Removed from library: " + path.string());
    refresh_library(state);
}

void refresh_plan(AppState& state) {
    state.plan.reset();
    if (!state.profile) {
        return;
    }
    if (state.selected_apps.empty()) {
        state.log_line("No applications selected; nothing to restore", true);
        return;
    }

    engine::PlanOptions options;
    options.only_applications = state.selected_apps;
    auto registry = make_registry();
    auto plan = engine::plan_restore(*state.profile, registry, options);
    if (!plan) {
        state.log_line(plan.error().message, true);
        return;
    }

    state.plan = std::move(*plan);
    state.log_line("Actions: " + std::to_string(state.plan->actions.size()) + ", conflicts: " +
                   std::to_string(state.plan->conflicts.size()));
    for (const auto& warning : state.plan->warnings) {
        state.log_line("Warning: " + warning);
    }
    for (const auto& [adapter, reason] : state.plan->missing) {
        state.log_line("Missing " + adapter + ": " + reason, true);
    }
}

void capture(AppState& state) {
    state.log.clear();

    engine::CaptureOptions options;
    if (state.capture_git) {
        options.only_adapters.push_back("git");
    }
    if (state.capture_vscode) {
        options.only_adapters.push_back("vscode");
    }
    if (state.capture_firefox) {
        options.only_adapters.push_back("firefox");
    }
    if (options.only_adapters.empty()) {
        state.log_line("Select at least one application to capture", true);
        return;
    }

    ProfileInfo info;
    info.name = state.profile_name;
    const std::string output = state.output_path;
    auto registry = make_registry();
    auto report = engine::capture_profile(output, info, registry, options);
    if (!report) {
        state.log_line(report.error().message, true);
        return;
    }
    std::string captured = "Captured:";
    for (const auto& id : report->captured) {
        captured += " " + id;
    }
    state.log_line(captured);
    for (const auto& [id, reason] : report->skipped) {
        state.log_line("Skipped " + id + ": " + reason);
    }
    for (const auto& warning : report->warnings) {
        state.log_line("Warning: " + warning);
    }
    state.log_line("Profile written to " + report->profile_path);
}

void load_and_plan(AppState& state) {
    state.log.clear();
    state.profile.reset();
    state.plan.reset();
    state.selected_apps.clear();

    auto profile = open_profile(state.profile_path);
    if (!profile) {
        state.log_line(profile.error().message, true);
        return;
    }
    state.profile = std::move(*profile);
    for (const auto& app : state.profile->info.applications) {
        state.selected_apps.push_back(app.id);
    }
    state.log_line("Profile \"" + state.profile->info.name + "\" from " + state.profile->info.source.os + " (" +
                   state.profile->info.source.architecture + ")");
    state.log_line("Applications: " + std::to_string(state.profile->info.applications.size()));
    refresh_plan(state);
}

void apply(AppState& state) {
    if (!state.plan) {
        state.log_line("Load a profile first", true);
        return;
    }
    auto result = engine::apply_restore(*state.plan, engine::RestoreOptions{platform::envbox_backup_root(), state.force, false});
    if (!result) {
        state.log_line(result.error().message, true);
        return;
    }
    for (const auto& action : result->applied) {
        state.log_line("Restored " + action.target.string() + (action.backed_up ? " [backed up]" : ""));
    }
    if (!result->backup_dir.empty()) {
        state.log_line("Backup: " + result->backup_dir.string());
    }
}

void draw_log_panel(AppState& state) {
    ImGui::BeginChild("log", ImVec2(0, 220), ImGuiChildFlags_Borders);
    for (const auto& [text, is_error] : state.log) {
        if (is_error) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", text.c_str());
        } else {
            ImGui::TextUnformatted(text.c_str());
        }
    }
    ImGui::EndChild();
}

void draw_capture_tab(AppState& state) {
    ImGui::InputText("Profile name", state.profile_name, sizeof state.profile_name);
    ImGui::InputText("Output file", state.output_path, sizeof state.output_path);
    ImGui::TextUnformatted("Applications:");
    ImGui::Checkbox("Git", &state.capture_git);
    ImGui::SameLine();
    ImGui::Checkbox("VS Code", &state.capture_vscode);
    ImGui::SameLine();
    ImGui::Checkbox("Firefox", &state.capture_firefox);
    if (ImGui::Button("Capture current environment")) {
        capture(state);
    }
    ImGui::TextDisabled("Captures only the selected applications for this machine.");
}

void draw_restore_tab(AppState& state) {
    ImGui::InputText("Profile file", state.profile_path, sizeof state.profile_path);
    if (ImGui::Button("Load and plan")) {
        load_and_plan(state);
    }

    if (state.profile) {
        ImGui::Separator();
        ImGui::TextUnformatted("Applications to restore:");
        bool changed = false;
        for (std::size_t i = 0; i < state.profile->info.applications.size(); ++i) {
            const auto& app = state.profile->info.applications[i];
            bool selected = std::find(state.selected_apps.begin(), state.selected_apps.end(), app.id) != state.selected_apps.end();
            if (i > 0) {
                ImGui::SameLine();
            }
            if (ImGui::Checkbox(app.id.c_str(), &selected)) {
                const auto it = std::find(state.selected_apps.begin(), state.selected_apps.end(), app.id);
                if (selected) {
                    state.selected_apps.push_back(app.id);
                } else if (it != state.selected_apps.end()) {
                    state.selected_apps.erase(it);
                }
                changed = true;
            }
        }
        if (changed) {
            refresh_plan(state);
        }
    }

    if (state.plan) {
        ImGui::Separator();
        ImGui::Text("Actions: %d   Conflicts: %d", static_cast<int>(state.plan->actions.size()),
                    static_cast<int>(state.plan->conflicts.size()));
        if (ImGui::BeginTable("actions", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Target");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Note");
            ImGui::TableHeadersRow();
            for (const auto& action : state.plan->actions) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(action.target.string().c_str());
                ImGui::TableSetColumnIndex(1);
                if (action.target_exists) {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "overwrite");
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "new");
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(action.note.c_str());
            }
            ImGui::EndTable();
        }

        for (const auto& conflict : state.plan->conflicts) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Conflict: %s", conflict.c_str());
        }
        ImGui::Checkbox("Allow overwriting (backup is taken automatically)", &state.force);

        const bool blocked = !state.plan->conflicts.empty() && !state.force;
        ImGui::BeginDisabled(blocked);
        if (ImGui::Button("Apply restore")) {
            apply(state);
        }
        ImGui::EndDisabled();
        if (blocked) {
            ImGui::SameLine();
            ImGui::TextDisabled("enable overwrite or choose a profile without conflicts");
        }
    } else {
        ImGui::TextDisabled("Load an .envbox profile to preview the restore plan. Nothing is written until you apply.");
    }
}

void compute_diff(AppState& state) {
    state.log.clear();
    state.diff.reset();
    state.selected_change = -1;

    auto profile = open_profile(state.diff_path);
    if (!profile) {
        state.log_line(profile.error().message, true);
        return;
    }

    auto registry = make_registry();
    engine::DiffOptions options;
    auto diff = engine::diff_profile(*profile, registry, options);
    if (!diff) {
        state.log_line(diff.error().message, true);
        return;
    }

    state.diff = std::move(*diff);
    state.log_line("Diff for \"" + state.diff->profile_name + "\": " +
                   std::to_string(state.diff->count(engine::ChangeKind::new_file)) + " new, " +
                   std::to_string(state.diff->count(engine::ChangeKind::modified)) + " modified, " +
                   std::to_string(state.diff->count(engine::ChangeKind::identical)) + " identical, " +
                   std::to_string(state.diff->count(engine::ChangeKind::skipped)) + " skipped");
    for (const auto& warning : state.diff->warnings) {
        state.log_line("Warning: " + warning);
    }
    for (const auto& [adapter, reason] : state.diff->missing) {
        state.log_line("Missing " + adapter + ": " + reason, true);
    }
}

const char* change_label(engine::ChangeKind kind) {
    switch (kind) {
        case engine::ChangeKind::new_file:  return "new";
        case engine::ChangeKind::identical: return "identical";
        case engine::ChangeKind::modified:  return "modified";
        case engine::ChangeKind::skipped:   return "skipped";
    }
    return "unknown";
}

ImVec4 change_color(engine::ChangeKind kind) {
    switch (kind) {
        case engine::ChangeKind::new_file:  return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        case engine::ChangeKind::identical: return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        case engine::ChangeKind::modified:  return ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
        case engine::ChangeKind::skipped:   return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void draw_diff_tab(AppState& state) {
    ImGui::InputText("Profile file##diff", state.diff_path, sizeof state.diff_path);
    if (ImGui::Button("Compute diff")) {
        compute_diff(state);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show identical files", &state.show_identical);

    if (!state.diff) {
        ImGui::TextDisabled("Compare a profile with this machine. Read-only: nothing is written.");
        return;
    }

    ImGui::Separator();
    if (ImGui::BeginTable("diff_changes", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 240))) {
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("Note");
        ImGui::TableHeadersRow();

        for (std::size_t index = 0; index < state.diff->changes.size(); ++index) {
            const auto& change = state.diff->changes[index];
            if (!state.show_identical && change.kind == engine::ChangeKind::identical) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected = state.selected_change == static_cast<int>(index);
            if (ImGui::Selectable(change_label(change.kind), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                state.selected_change = static_cast<int>(index);
            }
            ImGui::SameLine();
            ImGui::TableSetColumnIndex(1);
            const std::string target = change.target.empty() ? change.payload_path : change.target.string();
            ImGui::TextColored(change_color(change.kind), "%s", target.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(change.note.c_str());
        }
        ImGui::EndTable();
    }

    if (state.selected_change >= 0 && state.selected_change < static_cast<int>(state.diff->changes.size())) {
        const auto& change = state.diff->changes[static_cast<std::size_t>(state.selected_change)];
        ImGui::BeginChild("diff_text", ImVec2(0, 240), ImGuiChildFlags_Borders);
        if (!change.diff_text.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(change.diff_text.c_str());
            ImGui::PopTextWrapPos();
            if (change.truncated) {
                ImGui::TextDisabled("(diff truncated)");
            }
        } else if (change.kind == engine::ChangeKind::modified && change.truncated) {
            ImGui::TextDisabled("Diff omitted: file too large to inline.");
        } else if (change.kind == engine::ChangeKind::modified) {
            ImGui::TextDisabled("Binary file differs; no text diff available.");
        } else {
            ImGui::TextDisabled("No content diff for this entry.");
        }
        ImGui::EndChild();
    }
}

void draw_library_tab(AppState& state) {
    ImGui::InputText("Profile file##library", state.library_path, sizeof state.library_path);
    if (ImGui::Button("Add to library")) {
        add_to_library(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        refresh_library(state);
    }

    if (!state.library_entries) {
        ImGui::TextDisabled("Open the library to see indexed profiles.");
        return;
    }
    if (state.library_entries->empty()) {
        ImGui::TextDisabled("No profiles in the library yet. Capture one, then add its .envbox file above.");
        return;
    }

    if (ImGui::BeginTable("library", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 300))) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Applications");
        ImGui::TableSetupColumn("Path");
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (std::size_t index = 0; index < state.library_entries->size(); ++index) {
            const auto& entry = (*state.library_entries)[index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected = state.selected_library == static_cast<int>(index);
            if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                state.selected_library = static_cast<int>(index);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.source_os.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string applications;
            for (const auto& id : entry.applications) {
                if (!applications.empty()) {
                    applications += ", ";
                }
                applications += id;
            }
            ImGui::TextUnformatted(applications.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(entry.path.string().c_str());
            ImGui::TableSetColumnIndex(4);
            if (entry.file_exists) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "ok");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "missing");
            }
        }
        ImGui::EndTable();
    }

    const bool has_selection = state.selected_library >= 0 &&
                               state.selected_library < static_cast<int>(state.library_entries->size());
    ImGui::BeginDisabled(!has_selection);
    if (ImGui::Button("Use in Restore")) {
        const auto& entry = (*state.library_entries)[static_cast<std::size_t>(state.selected_library)];
        std::snprintf(state.profile_path, sizeof state.profile_path, "%s", entry.path.string().c_str());
        state.log_line("Restore path set from library: " + entry.path.string());
    }
    ImGui::SameLine();
    if (ImGui::Button("Use in Diff")) {
        const auto& entry = (*state.library_entries)[static_cast<std::size_t>(state.selected_library)];
        std::snprintf(state.diff_path, sizeof state.diff_path, "%s", entry.path.string().c_str());
        state.log_line("Diff path set from library: " + entry.path.string());
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove from library")) {
        remove_from_library(state);
    }
    ImGui::EndDisabled();
}

void draw_about_tab() {
    const OsInfo host = current_os();
    ImGui::Text("Envbox desktop %s", "1.0.0");
    ImGui::Text("Host: %s (%s)", std::string(to_string(host.kind)).c_str(), host.architecture.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Envbox captures application settings into a portable .envbox profile and restores them on another OS, adapting paths and reporting incompatible parameters.");
    ImGui::TextWrapped("Adapters: Git, VS Code, Firefox. Restore always verifies the profile, refuses silent overwrites, and backs up replaced files first.");
}

void draw_ui(AppState& state) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Envbox", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Capture")) {
            draw_capture_tab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Restore")) {
            draw_restore_tab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diff")) {
            draw_diff_tab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Library")) {
            draw_library_tab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("About")) {
            draw_about_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    draw_log_panel(state);
    ImGui::End();
}

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int run_check() {
    std::printf("Envbox desktop %s\n", "1.0.0");
    std::printf("Dear ImGui %s\n", IMGUI_VERSION);
    std::printf("Host: %s (%s)\n", std::string(to_string(current_os().kind)).c_str(), current_os().architecture.c_str());
    std::printf("Adapters: firefox, git, vscode\n");
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--check") == 0) {
        return run_check();
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "envbox-desktop: failed to initialize GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1100, 720, "Envbox", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "envbox-desktop: failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    AppState state;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw_ui(state);

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
