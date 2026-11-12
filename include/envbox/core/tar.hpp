#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "envbox/core/error.hpp"

namespace envbox {

struct TarEntry {
    std::string path;
    std::vector<std::byte> data;
    bool is_directory = false;
};

[[nodiscard]] Result<std::string> normalize_member_path(std::string_view path);

[[nodiscard]] Result<std::vector<std::byte>> tar_write(const std::vector<TarEntry>& entries);

[[nodiscard]] Result<std::vector<TarEntry>> tar_read(std::span<const std::byte> bytes);

}
