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
#include "envbox/engine/restore.hpp"

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
    bool force = false;
    std::vector<std::pair<std::string, bool>> log;
    std::optional<Profile> profile;
    std::optional<engine::RestorePlan> plan;

    void log_line(std::string text, bool is_error = false) {
        log.emplace_back(std::move(text), is_error);
    }
};

void capture(AppState& state) {
    state.log.clear();
    ProfileInfo info;
    info.name = state.profile_name;
    const std::string output = state.output_path;
    auto registry = make_registry();
    auto report = engine::capture_profile(output, info, registry);
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

    auto profile = open_profile(state.profile_path);
    if (!profile) {
        state.log_line(profile.error().message, true);
        return;
    }
    auto registry = make_registry();
    auto plan = engine::plan_restore(*profile, registry);
    if (!plan) {
        state.log_line(plan.error().message, true);
        return;
    }

    state.profile = std::move(*profile);
    state.plan = std::move(*plan);

    state.log_line("Profile \"" + state.profile->info.name + "\" from " + state.profile->info.source.os + " (" + state.profile->info.source.architecture + ")");
    state.log_line("Applications: " + std::to_string(state.profile->info.applications.size()) + ", actions: " + std::to_string(state.plan->actions.size()));
    for (const auto& warning : state.plan->warnings) {
        state.log_line("Warning: " + warning);
    }
    for (const auto& [adapter, reason] : state.plan->missing) {
        state.log_line("Missing " + adapter + ": " + reason, true);
    }
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
    if (ImGui::Button("Capture current environment")) {
        capture(state);
    }
    ImGui::TextDisabled("Captures Git, VS Code and Firefox settings for this machine.");
}

void draw_restore_tab(AppState& state) {
    ImGui::InputText("Profile file", state.profile_path, sizeof state.profile_path);
    if (ImGui::Button("Load and plan")) {
        load_and_plan(state);
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

void draw_about_tab() {
    const OsInfo host = current_os();
    ImGui::Text("Envbox desktop %s", "0.1.0");
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
    std::printf("Envbox desktop %s\n", "0.1.0");
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
