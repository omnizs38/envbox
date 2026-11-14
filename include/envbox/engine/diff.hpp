#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/adapter.hpp"
#include "envbox/engine/restore.hpp"

namespace envbox::engine {

enum class ChangeKind {
    new_file,
    identical,
    modified,
    skipped,
};

struct FileChange {
    std::string payload_path;
    std::filesystem::path target;
    ChangeKind kind = ChangeKind::new_file;
    std::string note;
    bool text = false;
    bool truncated = false;
    std::string diff_text;
};

struct ProfileDiff {
    std::string profile_name;
    std::vector<FileChange> changes;
    std::vector<std::string> warnings;
    std::vector<std::pair<std::string, std::string>> missing;

    [[nodiscard]] std::size_t count(ChangeKind kind) const;
};

struct DiffOptions {
    std::optional<OsKind> target_os_override;
    std::vector<std::string> only_applications;
    std::size_t max_diff_lines = 200;
    bool include_identical = true;
};

[[nodiscard]] Result<ProfileDiff> diff_profile(const Profile& profile,
                                               const AdapterRegistry& registry,
                                               const DiffOptions& options = {});

}
