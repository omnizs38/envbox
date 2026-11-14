#include "envbox/engine/migration.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace envbox::engine {
namespace {

Status reindex(Profile& profile) {
    auto profile_json = profile_info_to_json(profile.info);
    if (!profile_json) {
        return std::unexpected(profile_json.error());
    }

    std::vector<FileIndexEntry> files;
    files.reserve(profile.payload.size() + 1);

    for (const auto& entry : profile.payload) {
        auto hash = blake3_hex(entry.data);
        if (!hash) {
            return std::unexpected(hash.error());
        }
        files.push_back({entry.path, static_cast<std::uint64_t>(entry.data.size()), std::move(*hash)});
    }

    std::vector<std::byte> profile_json_bytes(reinterpret_cast<const std::byte*>(profile_json->data()),
                                              reinterpret_cast<const std::byte*>(profile_json->data() + profile_json->size()));
    auto profile_hash = blake3_hex(profile_json_bytes);
    if (!profile_hash) {
        return std::unexpected(profile_hash.error());
    }
    files.push_back({"profile.json", static_cast<std::uint64_t>(profile_json_bytes.size()), std::move(*profile_hash)});

    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    profile.manifest.files = std::move(files);
    profile.manifest.generator_version = std::string{kGeneratorVersion};
    return {};
}

}

void MigrationRegistry::add(MigrationStep step) {
    if (step.transform && step.to_version > step.from_version) {
        steps_.push_back(std::move(step));
    }
}

Result<Profile> MigrationRegistry::migrate(Profile profile, std::uint32_t target_version) const {
    const std::uint32_t current = profile.manifest.format_version;
    if (target_version < current) {
        return std::unexpected(Error::make(ErrorCode::validation, "cannot downgrade a profile from format_version " + std::to_string(current) + " to " + std::to_string(target_version)));
    }

    while (profile.manifest.format_version < target_version) {
        const std::uint32_t from = profile.manifest.format_version;
        const auto step = std::find_if(steps_.begin(), steps_.end(), [from](const MigrationStep& candidate) {
            return candidate.from_version == from;
        });
        if (step == steps_.end()) {
            return std::unexpected(Error::make(ErrorCode::unsupported, "no migration step from format_version " + std::to_string(from)));
        }
        if (step->to_version > target_version) {
            return std::unexpected(Error::make(ErrorCode::unsupported, "migration step from format_version " + std::to_string(from) + " overshoots target " + std::to_string(target_version)));
        }

        auto migrated = step->transform(std::move(profile));
        if (!migrated) {
            return std::unexpected(migrated.error());
        }
        profile = std::move(*migrated);
        profile.manifest.format_version = step->to_version;

        auto status = reindex(profile);
        if (!status) {
            return std::unexpected(status.error());
        }
    }

    return profile;
}

MigrationRegistry default_migrations() {
    return MigrationRegistry{};
}

}
