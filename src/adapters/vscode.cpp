#include "envbox/adapters/vscode.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <vector>

#include <nlohmann/json.hpp>

#include "envbox/core/os.hpp"
#include "envbox/platform/paths.hpp"

namespace envbox::adapters {
namespace {

constexpr std::string_view kId = "vscode";
constexpr std::uint32_t kAdapterVersion = 1;

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

bool is_regular_file_nothrow(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool is_directory_nothrow(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

std::filesystem::path vscode_user_dir() {
    const OsInfo host = current_os();
    if (host.kind == OsKind::windows) {
        if (const auto appdata = platform::get_env("APPDATA")) {
            return std::filesystem::path(*appdata) / "Code" / "User";
        }
        const auto home = platform::home_directory();
        if (home.empty()) {
            return {};
        }
        return home / "AppData" / "Roaming" / "Code" / "User";
    }
    if (host.kind == OsKind::macos) {
        const auto home = platform::home_directory();
        if (home.empty()) {
            return {};
        }
        return home / "Library" / "Application Support" / "Code" / "User";
    }
    if (host.kind == OsKind::linux) {
        const auto xdg = platform::xdg_config_home();
        if (xdg.empty()) {
            return {};
        }
        return xdg / "Code" / "User";
    }
    return {};
}

std::filesystem::path vscode_extensions_dir() {
    const auto home = platform::home_directory();
    if (home.empty()) {
        return {};
    }
    return home / ".vscode" / "extensions";
}

std::string target_os_suffix(OsKind kind) {
    switch (kind) {
        case OsKind::windows: return "windows";
        case OsKind::macos:   return "osx";
        case OsKind::linux:   return "linux";
        default:              return "";
    }
}

std::string strip_jsonc(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    std::size_t i = 0;
    bool in_string = false;
    bool escape = false;
    while (i < text.size()) {
        const char c = text[i];
        if (in_string) {
            out += c;
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            ++i;
            continue;
        }
        if (c == '"') {
            in_string = true;
            out += c;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < text.size()) {
            const char next = text[i + 1];
            if (next == '/') {
                i += 2;
                while (i < text.size() && text[i] != '\n') {
                    ++i;
                }
                continue;
            }
            if (next == '*') {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                    ++i;
                }
                i = std::min(i + 2, text.size());
                continue;
            }
        }
        out += c;
        ++i;
    }

    std::string result;
    result.reserve(out.size());
    in_string = false;
    escape = false;
    for (std::size_t k = 0; k < out.size(); ++k) {
        const char c = out[k];
        if (in_string) {
            result += c;
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            result += c;
            continue;
        }
        if (c == ',') {
            std::size_t lookahead = k + 1;
            while (lookahead < out.size() && (out[lookahead] == ' ' || out[lookahead] == '\t' || out[lookahead] == '\n' || out[lookahead] == '\r')) {
                ++lookahead;
            }
            if (lookahead < out.size() && (out[lookahead] == '}' || out[lookahead] == ']')) {
                continue;
            }
        }
        result += c;
    }
    return result;
}

std::string bytes_as_string(const std::vector<std::byte>& data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

void analyze_node(const nlohmann::json& node,
                  const std::string& path,
                  OsKind target_os,
                  std::vector<std::string>& warnings,
                  std::map<std::string, std::map<std::string, std::string>>& terminal_keys) {
    static const std::vector<std::string_view> kTerminalBases{
        "terminal.integrated.defaultProfile.",
        "terminal.integrated.automationProfile.",
    };
    static const std::regex kWindowsPath("[A-Za-z]:[\\\\/]");

    if (node.is_object()) {
        for (const auto& [key, value] : node.items()) {
            for (const auto base : kTerminalBases) {
                if (key.size() > base.size() && key.compare(0, base.size(), base) == 0) {
                    const std::string suffix(key.substr(base.size()));
                    if (suffix == "windows" || suffix == "osx" || suffix == "linux") {
                        const std::string text = value.is_string() ? value.get<std::string>() : value.dump();
                        terminal_keys[std::string(base)][suffix] = text;
                    }
                }
            }
            const std::string child = path.empty() ? key : path + "." + key;
            analyze_node(value, child, target_os, warnings, terminal_keys);
        }
        return;
    }
    if (node.is_array()) {
        std::size_t index = 0;
        for (const auto& item : node) {
            analyze_node(item, path + "[" + std::to_string(index) + "]", target_os, warnings, terminal_keys);
            ++index;
        }
        return;
    }
    if (!node.is_string()) {
        return;
    }
    const std::string value = node.get<std::string>();
    if (target_os != OsKind::windows) {
        if (std::regex_search(value, kWindowsPath)) {
            warnings.push_back("'" + path + "' references a Windows path (\"" + value + "\"); it will not resolve on the target OS");
        }
        if (value.find("Cascadia") != std::string::npos) {
            warnings.push_back("'" + path + "' uses font \"" + value + "\", which ships with Windows Terminal and may not exist on the target OS");
        }
    }
}

struct ParsedExtensions {
    std::vector<std::pair<std::string, std::string>> entries;
};

bool parse_extension_dirname(const std::string& name, std::string& id, std::string& version) {
    for (std::size_t dash = 0; dash < name.size(); ++dash) {
        if (name[dash] != '-' || dash == 0) {
            continue;
        }
        const std::string candidate_id = name.substr(0, dash);
        const std::string rest = name.substr(dash + 1);
        if (candidate_id.find('.') == std::string::npos || rest.empty() || !(rest[0] >= '0' && rest[0] <= '9')) {
            continue;
        }
        const std::size_t end_of_version = rest.find('-');
        const std::string candidate_version = end_of_version == std::string::npos ? rest : rest.substr(0, end_of_version);
        if (candidate_version.empty() || candidate_version.find_first_not_of("0123456789.") != std::string::npos) {
            continue;
        }
        id = candidate_id;
        version = candidate_version;
        return true;
    }
    return false;
}

class VscodeAdapter final : public engine::Adapter {
  public:
    std::string_view id() const override {
        return kId;
    }

    std::uint32_t adapter_version() const override {
        return kAdapterVersion;
    }

    Result<engine::CapturedApplication> capture() const override {
        const std::vector<std::string> config_names{"settings.json", "keybindings.json"};
        const std::filesystem::path user_dir = vscode_user_dir();
        const std::filesystem::path extensions_dir = vscode_extensions_dir();

        std::vector<PayloadFile> files;
        std::vector<std::string> warnings;

        bool settings_found = false;
        bool keybindings_found = false;

        if (!user_dir.empty()) {
            for (const auto& name : config_names) {
                const std::filesystem::path file = user_dir / name;
                if (is_regular_file_nothrow(file)) {
                    auto bytes = read_file_bytes(file);
                    if (!bytes) {
                        return std::unexpected(bytes.error());
                    }
                    files.push_back({"applications/vscode/" + name, std::move(*bytes)});
                    if (name == "settings.json") {
                        settings_found = true;
                    } else {
                        keybindings_found = true;
                    }
                }
            }

            const std::filesystem::path snippets_dir = user_dir / "snippets";
            if (is_directory_nothrow(snippets_dir)) {
                std::vector<std::string> snippets;
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(snippets_dir, ec)) {
                    if (entry.is_regular_file()) {
                        const std::string filename = entry.path().filename().string();
                        const bool accepted = filename.ends_with(".json") || filename.ends_with(".code-snippets");
                        if (accepted) {
                            snippets.push_back(filename);
                        }
                    }
                }
                std::sort(snippets.begin(), snippets.end());
                for (const auto& filename : snippets) {
                    auto bytes = read_file_bytes(snippets_dir / filename);
                    if (!bytes) {
                        return std::unexpected(bytes.error());
                    }
                    files.push_back({"applications/vscode/snippets/" + filename, std::move(*bytes)});
                }
            }
        }

        if (!user_dir.empty() && !settings_found) {
            warnings.push_back("settings.json not found in " + user_dir.string());
        }
        if (!user_dir.empty() && !keybindings_found) {
            warnings.push_back("keybindings.json not found in " + user_dir.string());
        }

        if (is_directory_nothrow(extensions_dir)) {
            std::vector<std::pair<std::string, std::string>> entries;
            std::error_code ec;
            for (const auto& dir : std::filesystem::directory_iterator(extensions_dir, ec)) {
                if (!dir.is_directory()) {
                    continue;
                }
                std::string id;
                std::string version;
                if (parse_extension_dirname(dir.path().filename().string(), id, version)) {
                    entries.emplace_back(std::move(id), std::move(version));
                }
            }
            std::sort(entries.begin(), entries.end());
            entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
            if (!entries.empty()) {
                nlohmann::json array = nlohmann::json::array();
                for (const auto& [id, version] : entries) {
                    array.push_back({{"id", id}, {"version", version}});
                }
                const std::string text = array.dump(2);
                files.push_back({"applications/vscode/extensions.json",
                                 std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()),
                                                        reinterpret_cast<const std::byte*>(text.data() + text.size()))});
            }
        }

        if (files.empty()) {
            return std::unexpected(Error::make(ErrorCode::not_found,
                                               "no VS Code data found (looked in " +
                                                   (user_dir.empty() ? std::string("<unset>") : user_dir.string()) +
                                                   ", " + (extensions_dir.empty() ? std::string("<unset>") : extensions_dir.string()) + ")"));
        }

        engine::CapturedApplication application;
        application.spec = ApplicationSpec{std::string(kId), "", std::string(kId), kAdapterVersion};
        application.files = std::move(files);
        application.warnings = std::move(warnings);
        return application;
    }

    Result<std::vector<engine::TargetResolution>> resolve_targets(const std::vector<std::string>& payload_paths) const override {
        const std::filesystem::path user_dir = vscode_user_dir();
        if (user_dir.empty()) {
            return std::unexpected(Error::make(ErrorCode::not_found, "no VS Code config directory available on this OS"));
        }

        std::vector<engine::TargetResolution> resolutions;
        for (const auto& payload_path : payload_paths) {
            if (payload_path == "applications/vscode/settings.json" || payload_path == "applications/vscode/keybindings.json") {
                const std::string filename = payload_path.substr(std::string("applications/vscode/").size());
                resolutions.push_back({payload_path, user_dir / filename, "writing VS Code " + filename});
                continue;
            }
            if (payload_path.starts_with("applications/vscode/snippets/")) {
                const std::string filename = payload_path.substr(std::string("applications/vscode/snippets/").size());
                resolutions.push_back({payload_path, user_dir / "snippets" / filename, "writing VS Code snippet"});
                continue;
            }
            if (payload_path == "applications/vscode/extensions.json") {
                continue;
            }
            return std::unexpected(Error::make(ErrorCode::validation, "vscode adapter does not support payload member: " + payload_path));
        }
        return resolutions;
    }

    Result<engine::PayloadAdaptation> adapt_payload(const engine::TargetResolution& resolution,
                                                    const std::vector<std::byte>& data,
                                                    const Profile& profile,
                                                    OsKind target_os) const override {
        const bool analyze = resolution.payload_path.ends_with(".json") &&
                             (resolution.payload_path == "applications/vscode/settings.json" ||
                              resolution.payload_path == "applications/vscode/keybindings.json");
        if (!analyze) {
            return engine::PayloadAdaptation{data, {}};
        }

        const std::string text = strip_jsonc(bytes_as_string(data));
        std::vector<std::string> warnings;
        std::map<std::string, std::map<std::string, std::string>> terminal_keys;

        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(text);
        } catch (const nlohmann::json::exception&) {
            warnings.push_back(resolution.payload_path.substr(std::string("applications/vscode/").size()) + ": not valid JSON(C); stored as-is, incompatibilities not analyzed");
            return engine::PayloadAdaptation{data, std::move(warnings)};
        }

        analyze_node(parsed, "", target_os, warnings, terminal_keys);

        const std::string target_suffix = target_os_suffix(target_os);
        const std::string source_os = profile.info.source.os;
        if (!target_suffix.empty()) {
            for (const auto& [base, suffixes] : terminal_keys) {
                if (suffixes.contains(target_suffix)) {
                    continue;
                }
                const auto it = suffixes.begin();
                const std::string source_key = base + it->first;
                warnings.push_back("no terminal profile configured for " + std::string(to_string(target_os)) + ": only '" + source_key + "' (= \"" + it->second + "\") is set" +
                                   (source_os == "windows" && target_os != OsKind::windows
                                        ? "; PowerShell may be absent — consider setting '" + base + target_suffix + "' to \"bash\" or \"zsh\""
                                        : "; VS Code falls back to its system default terminal"));
            }
        }

        return engine::PayloadAdaptation{data, std::move(warnings)};
    }
};

}

std::shared_ptr<engine::Adapter> make_vscode_adapter() {
    return std::make_shared<VscodeAdapter>();
}

}
