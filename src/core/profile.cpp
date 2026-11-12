#include "envbox/core/profile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>
#include <string_view>

#include <blake3.h>
#include <nlohmann/json.hpp>

#include "envbox/core/container.hpp"
#include "envbox/core/os.hpp"

namespace envbox {
namespace {

using nlohmann::json;


std::string bytes_to_string(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
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
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

bool is_iso8601_utc_shape(std::string_view text) {
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != 'Z') {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19) {
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

bool is_lower_hex(std::string_view text, std::size_t length) {
    if (text.size() != length) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool is_valid_app_id(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    if (!((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9'))) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

bool is_known_os_name(std::string_view text) {
    return text == "windows" || text == "linux" || text == "macos" || text == "unknown";
}

bool has_allowed_root(std::string_view path) {
    return path.starts_with("applications/") || path.starts_with("system/") || path.starts_with("assets/");
}

Result<std::string> validate_index_path(std::string_view path) {
    auto normalized = normalize_member_path(path);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    if (*normalized == "manifest.json") {
        return std::unexpected(Error::make(ErrorCode::validation, "manifest.json must not appear in the file index"));
    }
    if (*normalized == "profile.json") {
        return normalized;
    }
    if (!has_allowed_root(*normalized)) {
        return std::unexpected(Error::make(ErrorCode::validation,
                           "archive member must live under applications/, system/ or assets/: " + *normalized));
    }
    return normalized;
}

Result<std::string> require_string(const json& object, const char* key, std::string_view where) {
    if (!object.contains(key)) {
        return std::unexpected(Error::make(ErrorCode::format, std::string(where) + ": missing '" + key + "'"));
    }
    if (!object[key].is_string()) {
        return std::unexpected(Error::make(ErrorCode::format, std::string(where) + ": '" + key + "' must be a string"));
    }
    return object[key].get<std::string>();
}

template <typename T>
Result<T> require_unsigned(const json& object, const char* key, std::string_view where) {
    if (!object.contains(key)) {
        return std::unexpected(Error::make(ErrorCode::format, std::string(where) + ": missing '" + key + "'"));
    }
    try {
        const auto& value = object[key];
        if (value.is_number_unsigned()) {
            return value.get<T>();
        }
        if (value.is_number_integer()) {
            const std::int64_t signed_value = value.get<std::int64_t>();
            if (signed_value < 0) {
                return std::unexpected(Error::make(ErrorCode::format, std::string(where) + ": '" + key + "' must be >= 0"));
            }
            return static_cast<T>(signed_value);
        }
        return std::unexpected(Error::make(ErrorCode::format, std::string(where) + ": '" + key + "' must be a non-negative integer"));
    } catch (const json::exception& e) {
        return std::unexpected(Error::make(ErrorCode::format, std::string(where) + ": '" + key + "': " + e.what()));
    }
}

std::vector<std::byte> string_to_bytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), reinterpret_cast<const std::byte*>(text.data() + text.size())};
}

}

Result<std::string> blake3_hex(std::span<const std::byte> data) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    if (!data.empty()) {
        blake3_hasher_update(&hasher, data.data(), data.size());
    }
    std::uint8_t digest[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, digest, sizeof digest);

    std::string hex(BLAKE3_OUT_LEN * 2, '0');
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < BLAKE3_OUT_LEN; ++i) {
        hex[i * 2] = kHex[(digest[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    return hex;
}


Result<std::string> manifest_to_json(const Manifest& manifest) {
    json files = json::array();
    std::vector<FileIndexEntry> sorted = manifest.files;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    for (const auto& entry : sorted) {
        files.push_back({{"path", entry.path}, {"size", entry.size}, {"blake3", entry.blake3}});
    }

    json out;
    out["format"] = kFormatName;
    out["format_version"] = manifest.format_version;
    out["generator"] = {{"name", manifest.generator_name}, {"version", manifest.generator_version}};
    out["files"] = std::move(files);
    return out.dump(2);
}

Result<Manifest> manifest_from_json(std::string_view json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::exception& e) {
        return std::unexpected(Error::make(ErrorCode::format, std::string("manifest.json: ") + e.what()));
    }
    if (!root.is_object()) {
        return std::unexpected(Error::make(ErrorCode::format, "manifest.json: top-level value must be an object"));
    }

    auto format = require_string(root, "format", "manifest.json");
    if (!format) {
        return std::unexpected(format.error());
    }
    if (*format != kFormatName) {
        return std::unexpected(Error::make(ErrorCode::format, "manifest.json: 'format' must be \"envbox\", got \"" + *format + "\""));
    }

    auto format_version = require_unsigned<std::uint64_t>(root, "format_version", "manifest.json");
    if (!format_version) {
        return std::unexpected(format_version.error());
    }
    if (*format_version == 0) {
        return std::unexpected(Error::make(ErrorCode::format, "manifest.json: 'format_version' must be >= 1"));
    }
    if (*format_version > kFormatVersion) {
        return std::unexpected(Error::make(ErrorCode::unsupported, "manifest.json: format_version " + std::to_string(*format_version) +
                                                       " is newer than the supported version " +
                                                       std::to_string(kFormatVersion)));
    }

    Manifest manifest;
    manifest.format_version = static_cast<std::uint32_t>(*format_version);

    if (root.contains("generator")) {
        const auto& generator = root["generator"];
        if (!generator.is_object()) {
            return std::unexpected(Error::make(ErrorCode::format, "manifest.json: 'generator' must be an object"));
        }
        if (generator.contains("name") && generator["name"].is_string()) {
            manifest.generator_name = generator["name"].get<std::string>();
        }
        if (generator.contains("version") && generator["version"].is_string()) {
            manifest.generator_version = generator["version"].get<std::string>();
        }
    }

    auto files = root.find("files");
    if (files == root.end() || !files->is_array()) {
        return std::unexpected(Error::make(ErrorCode::format, "manifest.json: missing 'files' array"));
    }

    std::set<std::string> seen;
    for (const auto& item : *files) {
        if (!item.is_object()) {
            return std::unexpected(Error::make(ErrorCode::format, "manifest.json: 'files[]' entries must be objects"));
        }
        auto path = require_string(item, "path", "manifest.json files[]");
        if (!path) {
            return std::unexpected(path.error());
        }
        auto valid_path = validate_index_path(*path);
        if (!valid_path) {
            return std::unexpected(valid_path.error());
        }
        auto size = require_unsigned<std::uint64_t>(item, "size", "manifest.json files[]");
        if (!size) {
            return std::unexpected(size.error());
        }
        auto blake3 = require_string(item, "blake3", "manifest.json files[]");
        if (!blake3) {
            return std::unexpected(blake3.error());
        }
        if (!is_lower_hex(*blake3, 64)) {
            return std::unexpected(Error::make(ErrorCode::format, "manifest.json files[]: 'blake3' must be 64 lowercase hex chars in " +
                                                      *valid_path));
        }
        if (!seen.insert(*valid_path).second) {
            return std::unexpected(Error::make(ErrorCode::validation, "manifest.json files[]: duplicate path " + *valid_path));
        }
        manifest.files.push_back({*valid_path, *size, *blake3});
    }

    return manifest;
}


Result<std::string> profile_info_to_json(const ProfileInfo& info) {
    json applications = json::array();
    for (const auto& app : info.applications) {
        json entry{{"id", app.id}, {"adapter", app.adapter}, {"adapter_version", app.adapter_version}};
        if (!app.version.empty()) {
            entry["version"] = app.version;
        }
        applications.push_back(std::move(entry));
    }

    json out;
    out["name"] = info.name;
    out["description"] = info.description;
    out["tags"] = info.tags;
    out["created_at"] = info.created_at;
    out["source"] = {{"os", info.source.os}, {"architecture", info.source.architecture}};
    out["applications"] = std::move(applications);
    return out.dump(2);
}

Result<ProfileInfo> profile_info_from_json(std::string_view json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::exception& e) {
        return std::unexpected(Error::make(ErrorCode::format, std::string("profile.json: ") + e.what()));
    }
    if (!root.is_object()) {
        return std::unexpected(Error::make(ErrorCode::format, "profile.json: top-level value must be an object"));
    }

    auto name = require_string(root, "name", "profile.json");
    if (!name) {
        return std::unexpected(name.error());
    }
    if (name->find_first_not_of(" \t\r\n") == std::string::npos) {
        return std::unexpected(Error::make(ErrorCode::validation, "profile.json: 'name' must be non-empty"));
    }

    ProfileInfo info;
    info.name = *name;

    if (root.contains("description")) {
        if (!root["description"].is_string()) {
            return std::unexpected(Error::make(ErrorCode::format, "profile.json: 'description' must be a string"));
        }
        info.description = root["description"].get<std::string>();
    }
    if (root.contains("tags")) {
        if (!root["tags"].is_array()) {
            return std::unexpected(Error::make(ErrorCode::format, "profile.json: 'tags' must be an array of strings"));
        }
        for (const auto& tag : root["tags"]) {
            if (!tag.is_string()) {
                return std::unexpected(Error::make(ErrorCode::format, "profile.json: 'tags' must be an array of strings"));
            }
            info.tags.push_back(tag.get<std::string>());
        }
    }
    if (root.contains("created_at")) {
        if (!root["created_at"].is_string()) {
            return std::unexpected(Error::make(ErrorCode::format, "profile.json: 'created_at' must be a string"));
        }
        info.created_at = root["created_at"].get<std::string>();
        if (!info.created_at.empty() && !is_iso8601_utc_shape(info.created_at)) {
            return std::unexpected(Error::make(ErrorCode::validation,
                               "profile.json: 'created_at' must look like YYYY-MM-DDTHH:MM:SSZ, got \"" + info.created_at + "\""));
        }
    }

    auto source = root.find("source");
    if (source == root.end() || !source->is_object()) {
        return std::unexpected(Error::make(ErrorCode::format, "profile.json: missing 'source' object"));
    }
    auto os_name = require_string(*source, "os", "profile.json source");
    if (!os_name) {
        return std::unexpected(os_name.error());
    }
    if (!is_known_os_name(*os_name)) {
        return std::unexpected(Error::make(ErrorCode::validation,
                           "profile.json source: 'os' must be one of \"windows\", \"linux\", \"macos\", \"unknown\", got \"" +
                               *os_name + "\""));
    }
    auto architecture = require_string(*source, "architecture", "profile.json source");
    if (!architecture) {
        return std::unexpected(architecture.error());
    }
    if (architecture->empty()) {
        return std::unexpected(Error::make(ErrorCode::validation, "profile.json source: 'architecture' must be non-empty"));
    }
    info.source = {*os_name, *architecture};

    if (root.contains("applications")) {
        if (!root["applications"].is_array()) {
            return std::unexpected(Error::make(ErrorCode::format, "profile.json: 'applications' must be an array"));
        }
        for (const auto& item : root["applications"]) {
            if (!item.is_object()) {
                return std::unexpected(Error::make(ErrorCode::format, "profile.json: 'applications[]' entries must be objects"));
            }
            auto id = require_string(item, "id", "profile.json applications[]");
            if (!id) {
                return std::unexpected(id.error());
            }
            if (!is_valid_app_id(*id)) {
                return std::unexpected(Error::make(ErrorCode::validation, "profile.json applications[]: invalid application id \"" + *id + "\""));
            }
            ApplicationSpec app;
            app.id = *id;
            if (item.contains("version")) {
                if (!item["version"].is_string()) {
                    return std::unexpected(Error::make(ErrorCode::format, "profile.json applications[]: 'version' must be a string"));
                }
                app.version = item["version"].get<std::string>();
            }
            if (item.contains("adapter")) {
                if (!item["adapter"].is_string() || item["adapter"].get<std::string>().empty()) {
                    return std::unexpected(Error::make(ErrorCode::format, "profile.json applications[]: 'adapter' must be a non-empty string"));
                }
                app.adapter = item["adapter"].get<std::string>();
            } else {
                app.adapter = app.id;
            }
            auto adapter_version = require_unsigned<std::uint64_t>(item, "adapter_version", "profile.json applications[]");
            if (!adapter_version) {
                return std::unexpected(adapter_version.error());
            }
            if (*adapter_version == 0 || *adapter_version > 1000000) {
                return std::unexpected(Error::make(ErrorCode::validation, "profile.json applications[]: 'adapter_version' out of range"));
            }
            app.adapter_version = static_cast<std::uint32_t>(*adapter_version);
            info.applications.push_back(std::move(app));
        }
    }

    return info;
}


Status create_profile(const std::filesystem::path& out_path, ProfileInfo info, std::vector<PayloadFile> payload) {
    const auto first = info.name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::unexpected(Error::make(ErrorCode::validation, "profile name must be non-empty"));
    }
    info.name = info.name.substr(first, info.name.find_last_not_of(" \t\r\n") - first + 1);

    if (info.created_at.empty()) {
        info.created_at = now_iso8601_utc();
    } else if (!is_iso8601_utc_shape(info.created_at)) {
        return std::unexpected(Error::make(ErrorCode::validation,
                           "created_at must look like YYYY-MM-DDTHH:MM:SSZ, got \"" + info.created_at + "\""));
    }

    const OsInfo host = current_os();
    if (info.source.os.empty()) {
        info.source.os = std::string(to_string(host.kind));
    }
    if (!is_known_os_name(info.source.os)) {
        return std::unexpected(Error::make(ErrorCode::validation, "source.os must be one of \"windows\", \"linux\", \"macos\", \"unknown\""));
    }
    if (info.source.architecture.empty()) {
        info.source.architecture = host.architecture;
    }

    for (auto& app : info.applications) {
        if (!is_valid_app_id(app.id)) {
            return std::unexpected(Error::make(ErrorCode::validation, "invalid application id \"" + app.id + "\""));
        }
        if (app.adapter.empty()) {
            app.adapter = app.id;
        }
        if (app.adapter_version == 0) {
            return std::unexpected(Error::make(ErrorCode::validation, "adapter_version must be >= 1 for application \"" + app.id + "\""));
        }
    }

    std::set<std::string> seen_paths;
    std::vector<std::pair<std::string, std::vector<std::byte>>> member_data;
    member_data.reserve(payload.size() + 1);

    for (const auto& file : payload) {
        auto path = validate_index_path(file.path);
        if (!path) {
            return std::unexpected(path.error());
        }
        if (!seen_paths.insert(*path).second) {
            return std::unexpected(Error::make(ErrorCode::validation, "duplicate payload path: " + *path));
        }
        member_data.emplace_back(*path, file.data);
    }

    auto profile_json = profile_info_to_json(info);
    if (!profile_json) {
        return std::unexpected(profile_json.error());
    }
    member_data.emplace_back("profile.json", string_to_bytes(*profile_json));

    Manifest manifest;
    manifest.format_version = kFormatVersion;
    manifest.generator_name = std::string{kGeneratorName};
    manifest.generator_version = std::string{kGeneratorVersion};

    for (const auto& [path, data] : member_data) {
        auto hash = blake3_hex(data);
        if (!hash) {
            return std::unexpected(hash.error());
        }
        manifest.files.push_back({path, static_cast<std::uint64_t>(data.size()), std::move(*hash)});
    }
    std::sort(manifest.files.begin(), manifest.files.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });

    auto manifest_json = manifest_to_json(manifest);
    if (!manifest_json) {
        return std::unexpected(manifest_json.error());
    }

    std::sort(member_data.begin(), member_data.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<TarEntry> entries;
    entries.reserve(member_data.size() + 1);
    entries.push_back({"manifest.json", string_to_bytes(*manifest_json), false});
    for (auto& [path, data] : member_data) {
        entries.push_back({path, std::move(data), false});
    }

    return write_container(out_path, entries);
}

Result<Profile> open_profile(const std::filesystem::path& in_path, OpenOptions options) {
    auto entries = read_container(in_path);
    if (!entries) {
        return std::unexpected(entries.error());
    }

    const TarEntry* manifest_entry = nullptr;
    const TarEntry* profile_entry = nullptr;
    std::vector<TarEntry> payload;
    payload.reserve(entries->size());

    for (const auto& entry : *entries) {
        if (entry.path == "manifest.json") {
            manifest_entry = &entry;
        } else if (entry.path == "profile.json") {
            profile_entry = &entry;
        } else {
            payload.push_back(entry);
        }
    }
    if (manifest_entry == nullptr) {
        return std::unexpected(Error::make(ErrorCode::not_found, "not an envbox profile: manifest.json is missing"));
    }
    if (profile_entry == nullptr) {
        return std::unexpected(Error::make(ErrorCode::not_found, "not an envbox profile: profile.json is missing"));
    }

    auto manifest = manifest_from_json(bytes_to_string(manifest_entry->data));
    if (!manifest) {
        return std::unexpected(manifest.error());
    }
    auto info = profile_info_from_json(bytes_to_string(profile_entry->data));
    if (!info) {
        return std::unexpected(info.error());
    }

    std::set<std::string> indexed;
    for (const auto& file : manifest->files) {
        indexed.insert(file.path);
    }
    if (!indexed.contains("profile.json")) {
        return std::unexpected(Error::make(ErrorCode::integrity, "manifest.json does not index profile.json"));
    }

    for (const auto& file : manifest->files) {
        const TarEntry* member = nullptr;
        if (file.path == "profile.json") {
            member = profile_entry;
        } else {
            const auto it = std::find_if(payload.begin(), payload.end(),
                                         [&file](const TarEntry& entry) { return entry.path == file.path; });
            if (it != payload.end()) {
                member = &*it;
            }
        }
        if (member == nullptr) {
            return std::unexpected(Error::make(ErrorCode::integrity, "indexed file missing from archive: " + file.path));
        }
        if (static_cast<std::uint64_t>(member->data.size()) != file.size) {
            return std::unexpected(Error::make(ErrorCode::integrity, "size mismatch for " + file.path + ": expected " +
                                                         std::to_string(file.size) + ", got " +
                                                         std::to_string(member->data.size())));
        }
        if (options.verify_hashes) {
            auto hash = blake3_hex(member->data);
            if (!hash) {
                return std::unexpected(hash.error());
            }
            if (*hash != file.blake3) {
                return std::unexpected(Error::make(ErrorCode::integrity, "BLAKE3 mismatch for " + file.path));
            }
        }
    }

    for (const auto& entry : payload) {
        if (!indexed.contains(entry.path)) {
            return std::unexpected(Error::make(ErrorCode::integrity, "unindexed member in archive: " + entry.path));
        }
    }
    if (indexed.contains("manifest.json")) {
        return std::unexpected(Error::make(ErrorCode::integrity, "manifest.json must not appear in the file index"));
    }

    Profile profile;
    profile.info = std::move(*info);
    profile.manifest = std::move(*manifest);
    std::sort(payload.begin(), payload.end(),
              [](const TarEntry& a, const TarEntry& b) { return a.path < b.path; });
    profile.payload = std::move(payload);
    return profile;
}

}
