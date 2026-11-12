#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/tar.hpp"

namespace envbox {


[[nodiscard]] Result<std::vector<std::byte>> encode_container(const std::vector<TarEntry>& entries);
[[nodiscard]] Status write_container(const std::filesystem::path& out_path, const std::vector<TarEntry>& entries);

[[nodiscard]] Result<std::vector<TarEntry>> decode_container(std::span<const std::byte> bytes);
[[nodiscard]] Result<std::vector<TarEntry>> read_container(const std::filesystem::path& in_path);

}
