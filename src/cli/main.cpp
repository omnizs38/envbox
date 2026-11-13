#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "envbox/adapters/firefox.hpp"
#include "envbox/adapters/git.hpp"
#include "envbox/adapters/vscode.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/capture.hpp"
#include "envbox/engine/restore.hpp"

namespace {

using namespace envbox;

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitIntegrity = 2;
constexpr int kExitConflict = 3;

void print_usage() {
    std::cerr << R"(envbox - capture and restore application configurations

usage:
  envbox capture --profile <name> [--output <file>]
  envbox inspect <file.envbox> [--json]
  envbox restore <file.envbox> [--force] [--backup-dir <dir>] [--dry-run] [--os windows|linux|macos]

exit codes: 0 ok, 1 error, 2 integrity verification failed, 3 conflicts require --force
)";
}

engine::AdapterRegistry make_registry() {
    engine::AdapterRegistry registry;
    registry.add(adapters::make_firefox_adapter());
    registry.add(adapters::make_git_adapter());
    registry.add(adapters::make_vscode_adapter());
    return registry;
}

int exit_code_for(const Error& error) {
    return error.code == ErrorCode::integrity ? kExitIntegrity : kExitError;
}

int command_capture(int argc, char** argv) {
    std::string profile_name;
    std::string output;

    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile" && i + 1 < argc) {
            profile_name = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output = argv[++i];
        } else {
            std::cerr << "envbox capture: unknown or incomplete option: " << arg << "\n";
            print_usage();
            return kExitError;
        }
    }
    if (profile_name.empty()) {
        std::cerr << "envbox capture: --profile <name> is required\n";
        return kExitError;
    }
    if (output.empty()) {
        output = profile_name + ".envbox";
    }

    ProfileInfo info;
    info.name = profile_name;

    auto registry = make_registry();
    auto report = engine::capture_profile(output, info, registry);
    if (!report) {
        std::cerr << "envbox capture: " << report.error().message << "\n";
        return exit_code_for(report.error());
    }

    std::cout << "Captured:";
    for (const auto& id : report->captured) {
        std::cout << " " << id;
    }
    std::cout << "\n";
    if (report->skipped.empty()) {
        std::cout << "Skipped: none\n";
    } else {
        for (const auto& [id, reason] : report->skipped) {
            std::cout << "Skipped: " << id << " (" << reason << ")\n";
        }
    }
    for (const auto& warning : report->warnings) {
        std::cout << "Warning: " << warning << "\n";
    }
    std::cout << "Profile written to " << report->profile_path << "\n";
    return kExitOk;
}

int command_inspect(int argc, char** argv) {
    std::string input;
    bool json_output = false;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") {
            json_output = true;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "envbox inspect: unexpected argument: " << arg << "\n";
            return kExitError;
        }
    }
    if (input.empty()) {
        std::cerr << "envbox inspect: missing <file.envbox>\n";
        return kExitError;
    }

    auto profile = open_profile(input);
    if (!profile) {
        std::cerr << "envbox inspect: " << profile.error().message << "\n";
        return exit_code_for(profile.error());
    }

    if (json_output) {
        nlohmann::json out;
        out["name"] = profile->info.name;
        out["description"] = profile->info.description;
        out["tags"] = profile->info.tags;
        out["created_at"] = profile->info.created_at;
        out["source"] = {{"os", profile->info.source.os}, {"architecture", profile->info.source.architecture}};
        out["format_version"] = profile->manifest.format_version;
        out["generator"] = {{"name", profile->manifest.generator_name}, {"version", profile->manifest.generator_version}};
        out["applications"] = nlohmann::json::array();
        for (const auto& app : profile->info.applications) {
            out["applications"].push_back({{"id", app.id},
                                           {"version", app.version},
                                           {"adapter", app.adapter},
                                           {"adapter_version", app.adapter_version}});
        }
        out["files"] = nlohmann::json::array();
        for (const auto& file : profile->manifest.files) {
            out["files"].push_back({{"path", file.path}, {"size", file.size}, {"blake3", file.blake3}});
        }
        std::cout << out.dump(2) << "\n";
        return kExitOk;
    }

    std::cout << "Profile: " << profile->info.name << "\n";
    if (!profile->info.description.empty()) {
        std::cout << "Description: " << profile->info.description << "\n";
    }
    std::cout << "Created: " << profile->info.created_at << "\n";
    std::cout << "Source: " << profile->info.source.os << " (" << profile->info.source.architecture << ")\n";
    std::cout << "Format version: " << profile->manifest.format_version << " (generator " << profile->manifest.generator_name << " " << profile->manifest.generator_version << ")\n";
    std::cout << "Applications:\n";
    for (const auto& app : profile->info.applications) {
        std::cout << "  - " << app.id;
        if (!app.version.empty()) {
            std::cout << " " << app.version;
        }
        std::cout << " (adapter " << app.adapter << " v" << app.adapter_version << ")\n";
    }
    std::cout << "Files: " << profile->manifest.files.size() << "\n";
    for (const auto& file : profile->manifest.files) {
        std::cout << "  " << file.path << " (" << file.size << " bytes)\n";
    }
    return kExitOk;
}

std::filesystem::path backup_root_or_default(const std::string& override_path) {
    if (!override_path.empty()) {
        return override_path;
    }
    return platform::envbox_backup_root();
}

int command_restore(int argc, char** argv) {
    std::string input;
    std::string backup_dir;
    std::string os_override;
    bool force = false;
    bool dry_run = false;

    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--force") {
            force = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--backup-dir" && i + 1 < argc) {
            backup_dir = argv[++i];
        } else if (arg == "--os" && i + 1 < argc) {
            os_override = argv[++i];
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "envbox restore: unexpected argument: " << arg << "\n";
            return kExitError;
        }
    }
    if (input.empty()) {
        std::cerr << "envbox restore: missing <file.envbox>\n";
        return kExitError;
    }

    engine::PlanOptions plan_options;
    if (!os_override.empty()) {
        if (!dry_run) {
            std::cerr << "envbox restore: --os only makes sense with --dry-run\n";
            return kExitError;
        }
        const auto kind = os_kind_from_string(os_override);
        if (!kind) {
            std::cerr << "envbox restore: --os must be one of windows, linux, macos\n";
            return kExitError;
        }
        plan_options.target_os_override = kind;
    }

    auto profile = open_profile(input);
    if (!profile) {
        std::cerr << "envbox restore: " << profile.error().message << "\n";
        return exit_code_for(profile.error());
    }

    auto registry = make_registry();
    auto plan = engine::plan_restore(*profile, registry, plan_options);
    if (!plan) {
        std::cerr << "envbox restore: " << plan.error().message << "\n";
        return kExitError;
    }

    std::cout << "Restore plan for \"" << plan->profile_name << "\"\n";
    std::cout << "Actions (" << plan->actions.size() << "):\n";
    for (const auto& action : plan->actions) {
        std::cout << "  " << action.target.string() << (action.target_exists ? "  [overwrite]" : "  [new]") << "\n";
        if (!action.note.empty()) {
            std::cout << "    " << action.note << "\n";
        }
    }
    for (const auto& warning : plan->warnings) {
        std::cout << "Warning: " << warning << "\n";
    }
    for (const auto& [adapter, reason] : plan->missing) {
        std::cout << "Missing: " << adapter << " (" << reason << ")\n";
    }

    if (dry_run) {
        if (!plan->conflicts.empty()) {
            std::cout << "Conflicts (" << plan->conflicts.size() << ", would refuse without --force):\n";
            for (const auto& conflict : plan->conflicts) {
                std::cout << "  " << conflict << "\n";
            }
        }
        std::cout << "Dry run: nothing was written\n";
        return kExitOk;
    }

    if (!force && !plan->conflicts.empty()) {
        std::cout << "Conflicts (" << plan->conflicts.size() << "):\n";
        for (const auto& conflict : plan->conflicts) {
            std::cout << "  " << conflict << "\n";
        }
        std::cout << "Re-run with --force to proceed (existing files are backed up automatically)\n";
        return kExitConflict;
    }

    auto result = engine::apply_restore(*plan, engine::RestoreOptions{backup_root_or_default(backup_dir), force, false});
    if (!result) {
        std::cerr << "envbox restore: " << result.error().message << "\n";
        return exit_code_for(result.error());
    }

    for (const auto& action : result->applied) {
        std::cout << "Restored: " << action.target.string() << (action.backed_up ? "  [backed up]" : "") << "\n";
    }
    if (!result->backup_dir.empty()) {
        std::cout << "Backup of replaced files: " << result->backup_dir.string() << "\n";
    }
    return kExitOk;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return kExitError;
    }
    const std::string command = argv[1];
    if (command == "capture") {
        return command_capture(argc - 2, argv + 2);
    }
    if (command == "inspect") {
        return command_inspect(argc - 2, argv + 2);
    }
    if (command == "restore") {
        return command_restore(argc - 2, argv + 2);
    }
    std::cerr << "envbox: unknown command: " << command << "\n";
    print_usage();
    return kExitError;
}
