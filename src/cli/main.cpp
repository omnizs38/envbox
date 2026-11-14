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
#include "envbox/engine/diff.hpp"
#include "envbox/engine/restore.hpp"
#include "envbox/storage/library.hpp"

namespace {

using namespace envbox;

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitIntegrity = 2;
constexpr int kExitConflict = 3;
constexpr int kExitDifferences = 4;

void print_usage() {
    std::cerr << R"(envbox - capture and restore application configurations

usage:
  envbox capture --profile <name> [--output <file>] [--app <ids>] [--json]
  envbox inspect <file.envbox> [--json]
  envbox diff <file.envbox> [--os windows|linux|macos] [--app <ids>] [--full] [--exit-code] [--json]
  envbox restore <file.envbox> [--force] [--backup-dir <dir>] [--dry-run] [--os windows|linux|macos] [--app <ids>] [--json]
  envbox profiles list [--json]
  envbox profiles add <file.envbox>...
  envbox profiles remove <file.envbox>

options:
  --app <ids>   comma-separated application/adapter ids (e.g. git,vscode)

exit codes: 0 ok, 1 error, 2 integrity verification failed, 3 conflicts require --force,
            4 differences found (diff --exit-code)
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

std::vector<std::string> split_csv(const std::string& text) {
    std::vector<std::string> values;
    std::string current;
    for (const char c : text) {
        if (c == ',') {
            if (!current.empty()) {
                values.push_back(current);
                current.clear();
            }
        } else if (c != ' ') {
            current += c;
        }
    }
    if (!current.empty()) {
        values.push_back(current);
    }
    return values;
}

int command_capture(int argc, char** argv) {
    std::string profile_name;
    std::string output;
    std::string apps;
    bool json_output = false;

    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile" && i + 1 < argc) {
            profile_name = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--app" && i + 1 < argc) {
            apps = argv[++i];
        } else if (arg == "--json") {
            json_output = true;
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

    engine::CaptureOptions options;
    options.only_adapters = split_csv(apps);

    auto registry = make_registry();
    auto report = engine::capture_profile(output, info, registry, options);
    if (!report) {
        std::cerr << "envbox capture: " << report.error().message << "\n";
        return exit_code_for(report.error());
    }

    if (json_output) {
        nlohmann::json out;
        out["profile"] = report->profile_path;
        out["captured"] = report->captured;
        out["skipped"] = nlohmann::json::array();
        for (const auto& [id, reason] : report->skipped) {
            out["skipped"].push_back({{"adapter", id}, {"reason", reason}});
        }
        out["warnings"] = report->warnings;
        std::cout << out.dump(2) << "\n";
        return kExitOk;
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

const char* change_label(engine::ChangeKind kind) {
    switch (kind) {
        case engine::ChangeKind::new_file:  return "new";
        case engine::ChangeKind::identical: return "identical";
        case engine::ChangeKind::modified:  return "modified";
        case engine::ChangeKind::skipped:   return "skipped";
    }
    return "unknown";
}

int command_diff(int argc, char** argv) {
    std::string input;
    std::string os_override;
    std::string apps;
    bool full = false;
    bool exit_code = false;
    bool json_output = false;

    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--full") {
            full = true;
        } else if (arg == "--exit-code") {
            exit_code = true;
        } else if (arg == "--json") {
            json_output = true;
        } else if (arg == "--os" && i + 1 < argc) {
            os_override = argv[++i];
        } else if (arg == "--app" && i + 1 < argc) {
            apps = argv[++i];
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "envbox diff: unexpected argument: " << arg << "\n";
            return kExitError;
        }
    }
    if (input.empty()) {
        std::cerr << "envbox diff: missing <file.envbox>\n";
        return kExitError;
    }

    engine::DiffOptions options;
    options.max_diff_lines = full ? 0 : 200;
    options.only_applications = split_csv(apps);
    if (!os_override.empty()) {
        const auto kind = os_kind_from_string(os_override);
        if (!kind) {
            std::cerr << "envbox diff: --os must be one of windows, linux, macos\n";
            return kExitError;
        }
        options.target_os_override = kind;
    }

    auto profile = open_profile(input);
    if (!profile) {
        std::cerr << "envbox diff: " << profile.error().message << "\n";
        return exit_code_for(profile.error());
    }

    auto registry = make_registry();
    auto diff = engine::diff_profile(*profile, registry, options);
    if (!diff) {
        std::cerr << "envbox diff: " << diff.error().message << "\n";
        return kExitError;
    }

    if (json_output) {
        nlohmann::json out;
        out["profile"] = diff->profile_name;
        out["changes"] = nlohmann::json::array();
        for (const auto& change : diff->changes) {
            nlohmann::json item;
            item["payload_path"] = change.payload_path;
            item["target"] = change.target.string();
            item["status"] = change_label(change.kind);
            item["note"] = change.note;
            item["text"] = change.text;
            item["truncated"] = change.truncated;
            item["diff"] = change.diff_text;
            out["changes"].push_back(std::move(item));
        }
        out["warnings"] = diff->warnings;
        out["missing"] = nlohmann::json::array();
        for (const auto& [adapter, reason] : diff->missing) {
            out["missing"].push_back({{"adapter", adapter}, {"reason", reason}});
        }
        out["summary"] = {{"new", diff->count(engine::ChangeKind::new_file)},
                          {"modified", diff->count(engine::ChangeKind::modified)},
                          {"identical", diff->count(engine::ChangeKind::identical)},
                          {"skipped", diff->count(engine::ChangeKind::skipped)}};
        std::cout << out.dump(2) << "\n";
    } else {
        std::cout << "Profile: " << diff->profile_name << "\n";
        for (const auto& change : diff->changes) {
            std::cout << "  [" << change_label(change.kind) << "] ";
            std::cout << (change.target.empty() ? change.payload_path : change.target.string());
            if (!change.note.empty()) {
                std::cout << "  (" << change.note << ")";
            }
            std::cout << "\n";
        }
        for (const auto& warning : diff->warnings) {
            std::cout << "Warning: " << warning << "\n";
        }
        for (const auto& [adapter, reason] : diff->missing) {
            std::cout << "Missing: " << adapter << " (" << reason << ")\n";
        }

        for (const auto& change : diff->changes) {
            if (change.kind != engine::ChangeKind::modified) {
                continue;
            }
            if (change.diff_text.empty()) {
                if (change.truncated) {
                    std::cout << "\n(diff for " << change.target.string() << " omitted: too large to inline)\n";
                }
                continue;
            }
            std::cout << "\n--- " << change.target.string() << "\n" << change.diff_text;
            if (change.truncated) {
                std::cout << "(diff truncated; use --full)\n";
            }
        }

        const std::size_t new_count = diff->count(engine::ChangeKind::new_file);
        const std::size_t modified = diff->count(engine::ChangeKind::modified);
        const std::size_t identical = diff->count(engine::ChangeKind::identical);
        const std::size_t skipped = diff->count(engine::ChangeKind::skipped);
        std::cout << "\n" << new_count << " new, " << modified << " modified, " << identical << " identical, " << skipped
                  << " skipped\n";
    }

    if (exit_code && (diff->count(engine::ChangeKind::new_file) + diff->count(engine::ChangeKind::modified)) > 0) {
        return kExitDifferences;
    }
    return kExitOk;
}

int command_profiles(int argc, char** argv) {
    if (argc < 1) {
        std::cerr << "envbox profiles: missing subcommand (list, add, remove)\n";
        return kExitError;
    }
    const std::string subcommand = argv[0];
    bool json_output = false;
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") {
            json_output = true;
        } else {
            arguments.push_back(arg);
        }
    }

    auto library = storage::Library::open_default();
    if (!library) {
        std::cerr << "envbox profiles: " << library.error().message << "\n";
        return kExitError;
    }

    if (subcommand == "list") {
        auto entries = library->list();
        if (!entries) {
            std::cerr << "envbox profiles: " << entries.error().message << "\n";
            return kExitError;
        }
        if (json_output) {
            nlohmann::json out = nlohmann::json::array();
            for (const auto& entry : *entries) {
                out.push_back({{"name", entry.name},
                               {"path", entry.path.string()},
                               {"description", entry.description},
                               {"source_os", entry.source_os},
                               {"architecture", entry.architecture},
                               {"created_at", entry.created_at},
                               {"added_at", entry.added_at},
                               {"format_version", entry.format_version},
                               {"applications", entry.applications},
                               {"tags", entry.tags},
                               {"file_exists", entry.file_exists}});
            }
            std::cout << out.dump(2) << "\n";
            return kExitOk;
        }
        if (entries->empty()) {
            std::cout << "No profiles in the library. Add one with: envbox profiles add <file.envbox>\n";
            return kExitOk;
        }
        for (const auto& entry : *entries) {
            std::cout << entry.name << " (" << entry.source_os << ", " << entry.applications.size() << " app"
                      << (entry.applications.size() == 1 ? "" : "s") << ")"
                      << (entry.file_exists ? "" : "  [file missing]") << "\n  " << entry.path.string() << "\n";
        }
        return kExitOk;
    }

    if (subcommand == "add") {
        if (arguments.empty()) {
            std::cerr << "envbox profiles add: missing <file.envbox>\n";
            return kExitError;
        }
        int failures = 0;
        for (const auto& path : arguments) {
            auto status = library->add(path);
            if (!status) {
                std::cerr << "envbox profiles add: " << status.error().message << "\n";
                ++failures;
            } else {
                std::cout << "Added: " << path << "\n";
            }
        }
        return failures == 0 ? kExitOk : kExitError;
    }

    if (subcommand == "remove") {
        if (arguments.size() != 1) {
            std::cerr << "envbox profiles remove: expected exactly one <file.envbox>\n";
            return kExitError;
        }
        auto status = library->remove(arguments[0]);
        if (!status) {
            std::cerr << "envbox profiles remove: " << status.error().message << "\n";
            return kExitError;
        }
        std::cout << "Removed: " << arguments[0] << "\n";
        return kExitOk;
    }

    std::cerr << "envbox profiles: unknown subcommand: " << subcommand << "\n";
    return kExitError;
}

int command_restore(int argc, char** argv) {
    std::string input;
    std::string backup_dir;
    std::string os_override;
    std::string apps;
    bool force = false;
    bool dry_run = false;
    bool json_output = false;

    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--force") {
            force = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--json") {
            json_output = true;
        } else if (arg == "--backup-dir" && i + 1 < argc) {
            backup_dir = argv[++i];
        } else if (arg == "--os" && i + 1 < argc) {
            os_override = argv[++i];
        } else if (arg == "--app" && i + 1 < argc) {
            apps = argv[++i];
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
    plan_options.only_applications = split_csv(apps);
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

    const auto plan_json = [&plan]() {
        nlohmann::json out;
        out["profile"] = plan->profile_name;
        out["actions"] = nlohmann::json::array();
        for (const auto& action : plan->actions) {
            out["actions"].push_back({{"payload_path", action.payload_path},
                                      {"target", action.target.string()},
                                      {"exists", action.target_exists},
                                      {"note", action.note}});
        }
        out["conflicts"] = plan->conflicts;
        out["missing"] = nlohmann::json::array();
        for (const auto& [adapter, reason] : plan->missing) {
            out["missing"].push_back({{"adapter", adapter}, {"reason", reason}});
        }
        out["warnings"] = plan->warnings;
        return out;
    };

    if (json_output) {
        nlohmann::json out;
        out["dry_run"] = dry_run;
        out["plan"] = plan_json();
        out["applied"] = nlohmann::json::array();
        out["backup_dir"] = "";
        if (dry_run) {
            std::cout << out.dump(2) << "\n";
            return kExitOk;
        }
        if (!force && !plan->conflicts.empty()) {
            std::cout << out.dump(2) << "\n";
            return kExitConflict;
        }
        auto result = engine::apply_restore(*plan, engine::RestoreOptions{backup_root_or_default(backup_dir), force, false});
        if (!result) {
            std::cerr << "envbox restore: " << result.error().message << "\n";
            return exit_code_for(result.error());
        }
        for (const auto& action : result->applied) {
            out["applied"].push_back({{"payload_path", action.payload_path},
                                      {"target", action.target.string()},
                                      {"backed_up", action.backed_up},
                                      {"backup_path", action.backup_path.string()}});
        }
        out["backup_dir"] = result->backup_dir.string();
        std::cout << out.dump(2) << "\n";
        return kExitOk;
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
    if (command == "diff") {
        return command_diff(argc - 2, argv + 2);
    }
    if (command == "restore") {
        return command_restore(argc - 2, argv + 2);
    }
    if (command == "profiles") {
        return command_profiles(argc - 2, argv + 2);
    }
    std::cerr << "envbox: unknown command: " << command << "\n";
    print_usage();
    return kExitError;
}
