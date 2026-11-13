#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/adapter.hpp"
#include "envbox/platform/paths.hpp"

namespace envbox::engine {

struct RestoreAction {
    std::string payload_path;
    std::string application_id;
    std::string adapter_id;
    std::filesystem::path target;
    std::string note;
    bool target_exists = false;
    bool adapted = false;
    std::vector<std::byte> data;
};

struct RestorePlan {
    std::string profile_name;
    std::vector<RestoreAction> actions;
    std::vector<std::pair<std::string, std::string>> missing;
    std::vector<std::string> conflicts;
    std::vector<std::string> warnings;
};

struct RestoreOptions {
    std::filesystem::path backup_root = platform::envbox_backup_root();
    bool force = false;
    bool dry_run = false;
};

struct AppliedAction {
    std::string payload_path;
    std::filesystem::path target;
    bool backed_up = false;
    std::filesystem::path backup_path;
};

struct RestoreResult {
    std::filesystem::path backup_dir;
    std::vector<AppliedAction> applied;
};

struct PlanOptions {
    std::optional<OsKind> target_os_override;
};

[[nodiscard]] Result<RestorePlan> plan_restore(const Profile& profile,
                                               const AdapterRegistry& registry,
                                               const PlanOptions& options = {});
[[nodiscard]] Result<RestoreResult> apply_restore(const RestorePlan& plan,
                                                  const RestoreOptions& options);

}
