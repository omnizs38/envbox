#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/tar.hpp"
#include "envbox/core/version.hpp"

namespace envbox {

struct ApplicationSpec {
    std::string id;
    std::string version;
    std::string adapter;
    std::uint32_t adapter_version = 1;
};

struct SourceSpec {
    std::string os;
    std::string architecture;
};

struct ProfileInfo {
    std::string name;
    std::string description;
    std::vector<std::string> tags;
    std::string created_at;
    SourceSpec source;
    std::vector<ApplicationSpec> applications;
};

struct FileIndexEntry {
    std::string path;
    std::uint64_t size = 0;
    std::string blake3;
};

struct Manifest {
    std::uint32_t format_version = kFormatVersion;
    std::string generator_name = std::string{kGeneratorName};
    std::string generator_version = std::string{kGeneratorVersion};
    std::vector<FileIndexEntry> files;
};

struct PayloadFile {
    std::string path;
    std::vector<std::byte> data;
};

struct OpenOptions {
    bool verify_hashes = true;
};

struct Profile {
    ProfileInfo info;
    Manifest manifest;
    std::vector<TarEntry> payload;
};

[[nodiscard]] Result<std::string> blake3_hex(std::span<const std::byte> data);

[[nodiscard]] Status create_profile(const std::filesystem::path& out_path,
                                    ProfileInfo info,
                                    std::vector<PayloadFile> payload);

[[nodiscard]] Result<Profile> open_profile(const std::filesystem::path& in_path,
                                           OpenOptions options = {});

[[nodiscard]] Result<Manifest> manifest_from_json(std::string_view json_text);
[[nodiscard]] Result<ProfileInfo> profile_info_from_json(std::string_view json_text);
[[nodiscard]] Result<std::string> manifest_to_json(const Manifest& manifest);
[[nodiscard]] Result<std::string> profile_info_to_json(const ProfileInfo& info);

}
