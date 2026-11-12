#include "envbox/adapters/git.hpp"

#include <fstream>
#include <system_error>
#include <vector>

#include "envbox/core/os.hpp"
#include "envbox/platform/paths.hpp"

namespace envbox::adapters {
namespace {

constexpr std::string_view kId = "git";
constexpr std::uint32_t kAdapterVersion = 1;
constexpr std::string_view kPayloadPath = "applications/git/config";

Result<std::vector<std::byte>> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return std::unexpected(Error::make(ErrorCode::io, "cannot open file for reading: " + path.string()));
    }
    const std::streampos end = stream.tellg();
    if (end < 0) {
        return std::unexpected(Error::make(ErrorCode::io, "cannot determine file size: " + path.string()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            return std::unexpected(Error::make(ErrorCode::io, "failed while reading file: " + path.string()));
        }
    }
    return bytes;
}

std::filesystem::path xdg_git_config() {
    const auto xdg = platform::xdg_config_home();
    if (xdg.empty()) {
        return {};
    }
    return xdg / "git" / "config";
}

std::filesystem::path dot_gitconfig() {
    const auto home = platform::home_directory();
    if (home.empty()) {
        return {};
    }
    return home / ".gitconfig";
}

bool is_regular_file_nothrow(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

class GitAdapter final : public engine::Adapter {
  public:
    std::string_view id() const override {
        return kId;
    }

    std::uint32_t adapter_version() const override {
        return kAdapterVersion;
    }

    Result<engine::CapturedApplication> capture() const override {
        std::filesystem::path chosen;

        if (const auto override_path = platform::get_env("GIT_CONFIG_GLOBAL")) {
            chosen = std::filesystem::path(*override_path);
            if (!is_regular_file_nothrow(chosen)) {
                return std::unexpected(Error::make(ErrorCode::not_found, "GIT_CONFIG_GLOBAL points to a non-existent file: " + chosen.string()));
            }
        } else if (const auto xdg = xdg_git_config(); is_regular_file_nothrow(xdg)) {
            chosen = xdg;
        } else if (const auto dot = dot_gitconfig(); is_regular_file_nothrow(dot)) {
            chosen = dot;
        }

        if (chosen.empty()) {
            const auto xdg = xdg_git_config();
            const auto dot = dot_gitconfig();
            return std::unexpected(Error::make(ErrorCode::not_found,
                                               "no global git config found (looked in " + (xdg.empty() ? std::string("<unset>") : xdg.string()) + ", " + (dot.empty() ? std::string("<unset>") : dot.string()) + ")"));
        }

        auto bytes = read_file_bytes(chosen);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }

        engine::CapturedApplication application;
        application.spec = ApplicationSpec{std::string(kId), "", std::string(kId), kAdapterVersion};
        application.files = {{std::string(kPayloadPath), std::move(*bytes)}};
        return application;
    }

    Result<std::vector<engine::TargetResolution>> resolve_targets(const std::vector<std::string>& payload_paths) const override {
        const auto xdg = xdg_git_config();
        const auto dot = dot_gitconfig();

        std::filesystem::path target;
        std::string note;
        if (is_regular_file_nothrow(xdg)) {
            target = xdg;
            note = "existing XDG git config";
        } else if (is_regular_file_nothrow(dot)) {
            target = dot;
            note = "existing ~/.gitconfig";
        } else if (current_os().kind == OsKind::linux) {
            target = xdg;
            note = "fresh install, writing XDG git config";
        } else {
            target = dot;
            note = "fresh install, writing ~/.gitconfig";
        }
        if (target.empty()) {
            return std::unexpected(Error::make(ErrorCode::not_found, "no home directory available"));
        }

        std::vector<engine::TargetResolution> resolutions;
        for (const auto& payload_path : payload_paths) {
            if (payload_path != kPayloadPath) {
                return std::unexpected(Error::make(ErrorCode::validation, "git adapter does not support payload member: " + payload_path));
            }
            resolutions.push_back({payload_path, target, note});
        }
        return resolutions;
    }
};

}

std::shared_ptr<engine::Adapter> make_git_adapter() {
    return std::make_shared<GitAdapter>();
}

}
