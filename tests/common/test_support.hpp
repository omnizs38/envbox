#pragma once

#include <iterator>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "envbox/core/os.hpp"

namespace envbox::test {

class EnvGuard {
  public:
    EnvGuard() = default;
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

    void set(const char* key, const std::string& value) {
        saved_.emplace_back(key, get_raw(key));
#if defined(_WIN32)
        _putenv_s(key, value.c_str());
#else
        setenv(key, value.c_str(), 1);
#endif
    }

    void clear(const char* key) {
        saved_.emplace_back(key, get_raw(key));
#if defined(_WIN32)
        _putenv_s(key, "");
#else
        unsetenv(key);
#endif
    }

    ~EnvGuard() {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) {
            if (!it->second) {
#if defined(_WIN32)
                _putenv_s(it->first.c_str(), "");
#else
                unsetenv(it->first.c_str());
#endif
            } else {
#if defined(_WIN32)
                _putenv_s(it->first.c_str(), it->second->c_str());
#else
                setenv(it->first.c_str(), it->second->c_str(), 1);
#endif
            }
        }
    }

  private:
    static std::optional<std::string> get_raw(const char* key) {
        const char* value = std::getenv(key);
        if (value == nullptr || *value == '\0') {
            return std::nullopt;
        }
        return std::string(value);
    }

    std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
};

class FakeHome {
  public:
    FakeHome() {
        static int counter = 0;
        const auto unique =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter++);
        dir_ = std::filesystem::temp_directory_path() / ("envbox-test-home-" + unique);
        std::filesystem::create_directories(dir_);
    }

    FakeHome(const FakeHome&) = delete;
    FakeHome& operator=(const FakeHome&) = delete;

    ~FakeHome() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return dir_;
    }

    void wire(EnvGuard& env) const {
        env.set("HOME", dir_.string());
        env.set("USERPROFILE", dir_.string());
        env.set("XDG_CONFIG_HOME", (dir_ / ".config").string());
        env.set("XDG_DATA_HOME", (dir_ / ".local" / "share").string());
        env.set("LOCALAPPDATA", (dir_ / "AppData" / "Local").string());
        env.set("APPDATA", (dir_ / "AppData" / "Roaming").string());
        env.clear("GIT_CONFIG_GLOBAL");
    }

  private:
    std::filesystem::path dir_;
};

[[nodiscard]] inline std::filesystem::path host_vscode_user_dir(const std::filesystem::path& home) {
    switch (current_os().kind) {
        case OsKind::windows: return home / "AppData" / "Roaming" / "Code" / "User";
        case OsKind::macos:   return home / "Library" / "Application Support" / "Code" / "User";
        default:              return home / ".config" / "Code" / "User";
    }
}

[[nodiscard]] inline std::filesystem::path host_firefox_root(const std::filesystem::path& home) {
    switch (current_os().kind) {
        case OsKind::windows: return home / "AppData" / "Roaming" / "Mozilla" / "Firefox";
        case OsKind::macos:   return home / "Library" / "Application Support" / "Firefox";
        default:              return home / ".mozilla" / "firefox";
    }
}

inline void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
}

[[nodiscard]] inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

}
