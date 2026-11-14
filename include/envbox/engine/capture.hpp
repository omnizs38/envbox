#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/engine/adapter.hpp"

namespace envbox::engine {

struct CaptureReport {
    std::string profile_path;
    std::vector<std::string> captured;
    std::vector<std::pair<std::string, std::string>> skipped;
    std::vector<std::string> warnings;
};

struct CaptureOptions {
    std::vector<std::string> only_adapters;
};

[[nodiscard]] Result<CaptureReport> capture_profile(const std::filesystem::path& out_path,
                                                    ProfileInfo info,
                                                    const AdapterRegistry& registry,
                                                    const CaptureOptions& options = {});

}
