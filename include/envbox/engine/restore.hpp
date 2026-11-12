#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/adapter.hpp"
#include "envbox/platform/paths.hpp"

namespace envbox::engine {

struct RestoreAction {
    std::string payload_path;
    std::string application_id;
    std::filesystem::path target;
    std::string note;
    bool target_exists = false;
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

[[nodiscard]] Result<RestorePlan> plan_restore(const Profile& profile, const AdapterRegistry& registry);
[[nodiscard]] Result<RestoreResult> apply_restore(const Profile& profile,
                                                  const RestorePlan& plan,
                                                  const RestoreOptions& options);

}
