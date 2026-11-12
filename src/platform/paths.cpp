#include "envbox/platform/paths.hpp"

#include <cstdlib>

namespace envbox::platform {

std::optional<std::string> get_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

std::filesystem::path home_directory() {
#if defined(_WIN32)
    if (auto user = get_env("USERPROFILE")) {
        return std::filesystem::path(*user);
    }
    if (auto home = get_env("HOME")) {
        return std::filesystem::path(*home);
    }
    return {};
#else
    if (auto home = get_env("HOME")) {
        return std::filesystem::path(*home);
    }
    if (auto user = get_env("USERPROFILE")) {
        return std::filesystem::path(*user);
    }
    return {};
#endif
}

std::filesystem::path xdg_config_home() {
    if (auto value = get_env("XDG_CONFIG_HOME")) {
        return std::filesystem::path(*value);
    }
    const auto home = home_directory();
    if (home.empty()) {
        return {};
    }
    return home / ".config";
}

std::filesystem::path xdg_data_home() {
    if (auto value = get_env("XDG_DATA_HOME")) {
        return std::filesystem::path(*value);
    }
    const auto home = home_directory();
    if (home.empty()) {
        return {};
    }
    return home / ".local" / "share";
}

std::filesystem::path local_data_directory() {
#if defined(_WIN32)
    if (auto appdata = get_env("LOCALAPPDATA")) {
        return std::filesystem::path(*appdata);
    }
    const auto home = home_directory();
    if (home.empty()) {
        return {};
    }
    return home / "AppData" / "Local";
#else
    return xdg_data_home();
#endif
}

std::filesystem::path envbox_data_dir() {
    const auto base = local_data_directory();
    if (base.empty()) {
        return {};
    }
    return base / "envbox";
}

std::filesystem::path envbox_backup_root() {
    const auto data = envbox_data_dir();
    if (data.empty()) {
        return {};
    }
    return data / "backups";
}

std::string sanitize_filename(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const unsigned char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == '.';
        out += ok ? static_cast<char>(c) : '_';
    }
    while (!out.empty() && out.front() == '.') {
        out.erase(out.begin());
    }
    if (out.empty()) {
        out = "_";
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

}
