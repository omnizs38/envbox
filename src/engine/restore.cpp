#include "envbox/engine/restore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace envbox::engine {
namespace {

std::string now_stamp() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

std::string now_iso8601_utc() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

Result<std::vector<std::byte>> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return std::unexpected(Error::make(ErrorCode::io, "cannot open file for reading: " + path.string()));
    }
    const std::streampos end = stream.tellg();
    if (end < 0) {
        return std::unexpected(Error::make(ErrorCode::io, "cannot determine file size: " + path.string()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            return std::unexpected(Error::make(ErrorCode::io, "failed while reading file: " + path.string()));
        }
    }
    return bytes;
}

Status write_file_atomic(const std::filesystem::path& target, const std::vector<std::byte>& bytes) {
    std::filesystem::path tmp = target;
    tmp += ".envbox-tmp";
    {
        std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return std::unexpected(Error::make(ErrorCode::io, "cannot open file for writing: " + tmp.string()));
        }
        if (!bytes.empty()) {
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        stream.flush();
        if (!stream) {
            stream.close();
            std::error_code cleanup;
            std::filesystem::remove(tmp, cleanup);
            return std::unexpected(Error::make(ErrorCode::io, "failed while writing file: " + tmp.string()));
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::error_code cleanup;
        std::filesystem::remove(tmp, cleanup);
        return std::unexpected(Error::make(ErrorCode::io, "cannot move temporary file into place: " + target.string() + ": " + ec.message()));
    }
    return {};
}

}

Result<RestorePlan> plan_restore(const Profile& profile, const AdapterRegistry& registry) {
    RestorePlan plan;
    plan.profile_name = profile.info.name;

    std::set<std::string> payload_app_ids;
    for (const auto& entry : profile.payload) {
        if (entry.path.starts_with("applications/")) {
            const std::string rest = entry.path.substr(std::string("applications/").size());
            const std::size_t slash = rest.find('/');
            if (slash != std::string::npos) {
                payload_app_ids.insert(rest.substr(0, slash));
            }
        } else {
            plan.warnings.push_back("no adapter exists for payload root of '" + entry.path + "'; skipping");
        }
    }

    std::set<std::string> declared_app_ids;
    for (const auto& app : profile.info.applications) {
        declared_app_ids.insert(app.id);
        const Adapter* adapter = registry.find(app.adapter);
        if (adapter == nullptr) {
            plan.missing.emplace_back(app.adapter, "no adapter registered for application '" + app.id + "'");
            continue;
        }
        const std::string prefix = "applications/" + app.id + "/";
        std::vector<std::string> paths;
        for (const auto& entry : profile.payload) {
            if (entry.path.starts_with(prefix)) {
                paths.push_back(entry.path);
            }
        }
        if (paths.empty()) {
            plan.warnings.push_back("application '" + app.id + "' declared but has no captured files");
            continue;
        }
        auto resolutions = adapter->resolve_targets(paths);
        if (!resolutions) {
            plan.missing.emplace_back(app.adapter, resolutions.error().message);
            continue;
        }
        for (auto& resolution : *resolutions) {
            RestoreAction action;
            action.payload_path = std::move(resolution.payload_path);
            action.application_id = app.id;
            action.target = std::move(resolution.target);
            action.note = std::move(resolution.note);
            std::error_code ec;
            action.target_exists = std::filesystem::exists(action.target, ec) && !ec;
            if (action.target_exists) {
                plan.conflicts.push_back(action.target.string());
            }
            plan.actions.push_back(std::move(action));
        }
    }

    for (const auto& id : payload_app_ids) {
        if (!declared_app_ids.contains(id)) {
            plan.missing.emplace_back(id, "payload files present but application is not declared in the profile");
        }
    }

    return plan;
}

Result<RestoreResult> apply_restore(const Profile& profile, const RestorePlan& plan, const RestoreOptions& options) {
    if (!options.dry_run && !options.force && !plan.conflicts.empty()) {
        std::string message = "refusing to overwrite " + std::to_string(plan.conflicts.size()) + " existing file(s): ";
        const std::size_t preview = std::min<std::size_t>(plan.conflicts.size(), 3);
        for (std::size_t i = 0; i < preview; ++i) {
            message += plan.conflicts[i];
            if (i + 1 < preview) {
                message += ", ";
            }
        }
        message += " (re-run with force; existing files are backed up automatically)";
        return std::unexpected(Error::make(ErrorCode::conflict, std::move(message)));
    }

    std::unordered_map<std::string, const TarEntry*> payload_by_path;
    for (const auto& entry : profile.payload) {
        payload_by_path.emplace(entry.path, &entry);
    }

    struct BackupRecord {
        int index = 0;
        std::string original;
        std::string backup_name;
        std::uint64_t size = 0;
        std::string blake3;
    };
    std::vector<BackupRecord> backup_entries;

    RestoreResult result;
    std::filesystem::path backup_dir;
    int backup_index = 0;
    std::size_t applied_count = 0;
    const std::size_t total = plan.actions.size();

    for (const auto& action : plan.actions) {
        const auto found = payload_by_path.find(action.payload_path);
        if (found == payload_by_path.end()) {
            return std::unexpected(Error::make(ErrorCode::integrity, "plan references a missing payload: " + action.payload_path));
        }
        const TarEntry& payload = *found->second;

        if (options.dry_run) {
            result.applied.push_back({action.payload_path, action.target, false, {}});
            continue;
        }

        if (action.target_exists) {
            if (backup_dir.empty()) {
                const std::string dir_name = now_stamp() + "-" + platform::sanitize_filename(plan.profile_name);
                std::error_code ec;
                std::filesystem::path candidate = options.backup_root / dir_name;
                for (int suffix = 1; suffix < 1000 && std::filesystem::exists(candidate, ec); ++suffix) {
                    candidate = options.backup_root / (dir_name + "-" + std::to_string(suffix));
                }
                std::filesystem::create_directories(candidate, ec);
                if (ec) {
                    return std::unexpected(Error::make(ErrorCode::io, "cannot create backup directory " + candidate.string() + ": " + ec.message()));
                }
                backup_dir = std::move(candidate);
            }
            ++backup_index;
            const std::string backup_name = std::to_string(backup_index) + "-" + platform::sanitize_filename(action.target.filename().string());
            const std::filesystem::path backup_path = backup_dir / backup_name;
            std::error_code ec;
            std::filesystem::copy_file(action.target, backup_path, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return std::unexpected(Error::make(ErrorCode::io, "cannot back up " + action.target.string() + " to " + backup_path.string() + ": " + ec.message()));
            }
            auto bytes = read_file_bytes(backup_path);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            auto hash = blake3_hex(*bytes);
            if (!hash) {
                return std::unexpected(hash.error());
            }
            backup_entries.push_back({backup_index, action.target.string(), backup_name, bytes->size(), std::move(*hash)});
            result.applied.push_back({action.payload_path, action.target, true, backup_path});
        } else {
            result.applied.push_back({action.payload_path, action.target, false, {}});
        }

        if (!action.target.parent_path().empty()) {
            std::error_code ec;
            std::filesystem::create_directories(action.target.parent_path(), ec);
            if (ec) {
                return std::unexpected(Error::make(ErrorCode::io, "cannot create directory " + action.target.parent_path().string()));
            }
        }
        if (auto status = write_file_atomic(action.target, payload.data); !status) {
            return std::unexpected(Error::make(ErrorCode::io, "restore failed after applying " + std::to_string(applied_count) + " of " + std::to_string(total) + " file(s); backup kept at " + backup_dir.string() + ": " + status.error().message));
        }
        ++applied_count;
    }

    if (!backup_dir.empty()) {
        nlohmann::json entries = nlohmann::json::array();
        for (const auto& record : backup_entries) {
            entries.push_back({{"index", record.index},
                               {"original", record.original},
                               {"backup", record.backup_name},
                               {"size", record.size},
                               {"blake3", record.blake3}});
        }
        nlohmann::json backup_manifest;
        backup_manifest["kind"] = "envbox-restore-backup";
        backup_manifest["profile"] = plan.profile_name;
        backup_manifest["created_at"] = now_iso8601_utc();
        backup_manifest["entries"] = std::move(entries);
        const auto text = backup_manifest.dump(2);
        auto status = write_file_atomic(backup_dir / "backup.json",
                                        std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()),
                                                                reinterpret_cast<const std::byte*>(text.data() + text.size())));
        if (!status) {
            return std::unexpected(status.error());
        }
        result.backup_dir = backup_dir;
    }

    return result;
}

}
