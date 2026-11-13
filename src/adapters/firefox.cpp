#include "envbox/adapters/firefox.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "envbox/compat/paths.hpp"
#include "envbox/core/os.hpp"
#include "envbox/platform/paths.hpp"

namespace envbox::adapters {
namespace {

constexpr std::string_view kId = "firefox";
constexpr std::uint32_t kAdapterVersion = 1;
constexpr std::string_view kPayloadPath = "applications/firefox/user.js";

std::string read_text(const std::filesystem::path& path, bool& ok) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        ok = false;
        return {};
    }
    ok = true;
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::filesystem::path firefox_root() {
    const OsInfo host = current_os();
    if (host.kind == OsKind::windows) {
        if (const auto appdata = platform::get_env("APPDATA")) {
            return std::filesystem::path(*appdata) / "Mozilla" / "Firefox";
        }
        const auto home = platform::home_directory();
        if (home.empty()) {
            return {};
        }
        return home / "AppData" / "Roaming" / "Mozilla" / "Firefox";
    }
    if (host.kind == OsKind::macos) {
        const auto home = platform::home_directory();
        if (home.empty()) {
            return {};
        }
        return home / "Library" / "Application Support" / "Firefox";
    }
    if (host.kind == OsKind::linux) {
        const auto home = platform::home_directory();
        if (home.empty()) {
            return {};
        }
        return home / ".mozilla" / "firefox";
    }
    return {};
}

struct IniProfile {
    std::string name;
    std::filesystem::path path;
    bool is_default = false;
};

std::vector<IniProfile> read_profiles_ini(const std::filesystem::path& root) {
    std::vector<IniProfile> profiles;
    bool exists = false;
    const std::string text = read_text(root / "profiles.ini", exists);
    if (!exists) {
        return profiles;
    }

    struct Section {
        bool is_profile = false;
        std::string name;
        std::string path;
        bool relative = true;
        bool is_default = false;
    };

    std::vector<Section> sections;
    Section current;
    bool in_section = false;

    auto flush = [&]() {
        if (in_section && current.is_profile) {
            sections.push_back(current);
        }
        current = Section{};
    };

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string line = trim(std::string_view(text).substr(start, newline == std::string::npos ? std::string::npos : newline - start));
        start = newline == std::string::npos ? text.size() + 1 : newline + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            flush();
            in_section = true;
            current.is_profile = line.rfind("[Profile", 0) == 0;
            continue;
        }
        if (!in_section) {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(std::string_view(line).substr(0, equals));
        const std::string value = trim(std::string_view(line).substr(equals + 1));
        if (key == "Name") {
            current.name = value;
        } else if (key == "Path") {
            current.path = value;
        } else if (key == "IsRelative") {
            current.relative = value != "0";
        } else if (key == "Default") {
            current.is_default = value == "1";
        }
    }
    flush();

    for (const auto& section : sections) {
        const std::filesystem::path path = section.relative ? root / section.path : std::filesystem::path(section.path);
        profiles.push_back({section.name, path, section.is_default});
    }
    return profiles;
}

Result<std::filesystem::path> default_profile_dir() {
    const std::filesystem::path root = firefox_root();
    if (root.empty()) {
        return std::unexpected(Error::make(ErrorCode::not_found, "no Firefox data directory detected on this OS"));
    }
    const auto profiles = read_profiles_ini(root);
    if (profiles.empty()) {
        return std::unexpected(Error::make(ErrorCode::not_found, "no Firefox profiles found in " + root.string() + " (start Firefox once)"));
    }
    for (const auto& profile : profiles) {
        if (profile.is_default) {
            return profile.path;
        }
    }
    return profiles.front().path;
}

bool is_private_pref(std::string_view key) {
    static const std::vector<std::string_view> kPrefixes{
        "signon.",
        "services.sync.",
        "browser.sessionstore.",
        "browser.download.dir",
        "browser.download.lastDir",
        "browser.cache.",
        "toolkit.telemetry.",
        "datareporting.",
        "app.update.",
        "media.gmp",
        "gfx.",
        "layers.",
        "webgl.",
    };
    for (const auto prefix : kPrefixes) {
        if (key.starts_with(prefix)) {
            return true;
        }
    }
    const std::string lowered = ascii_lower(key);
    static const std::vector<std::string_view> kSecretMarkers{
        "password", "passwd", "token", "secret", "api_key", "apikey", "credential",
    };
    for (const auto marker : kSecretMarkers) {
        if (lowered.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool value_mentions_secrets(const nlohmann::json& value) {
    if (value.is_string()) {
        const std::string lowered = ascii_lower(value.get<std::string>());
        static const std::vector<std::string_view> kMarkers{
            "password", "passwd", "token", "secret", "api_key", "apikey", "credential",
        };
        for (const auto marker : kMarkers) {
            if (lowered.find(marker) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

std::string extract_pref_key(std::string_view line) {
    const std::size_t quote = line.find('"');
    if (quote == std::string_view::npos) {
        return {};
    }
    std::string key;
    for (std::size_t i = quote + 1; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            key += line[i];
            key += line[i + 1];
            ++i;
            continue;
        }
        if (line[i] == '"') {
            return key;
        }
        key += line[i];
    }
    return {};
}

struct ParsedPref {
    std::string key;
    nlohmann::json value;
};

std::vector<ParsedPref> parse_prefs_js(const std::string& text, std::size_t& malformed) {
    std::vector<ParsedPref> prefs;
    malformed = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string line = trim(std::string_view(text).substr(start, newline == std::string::npos ? std::string::npos : newline - start));
        start = newline == std::string::npos ? text.size() + 1 : newline + 1;
        if (!line.starts_with("user_pref(")) {
            continue;
        }
        const std::size_t close = line.rfind(')');
        if (close == std::string::npos) {
            ++malformed;
            continue;
        }
        std::string_view body(line.data() + 10, close - 10);
        const std::string key = extract_pref_key(body);
        if (key.empty()) {
            ++malformed;
            continue;
        }
        const std::size_t comma = body.find(',');
        if (comma == std::string_view::npos) {
            ++malformed;
            continue;
        }
        const std::string value_text = trim(body.substr(comma + 1));
        try {
            const auto value = nlohmann::json::parse(value_text);
            prefs.push_back({key, value});
        } catch (const nlohmann::json::exception&) {
            ++malformed;
        }
    }
    return prefs;
}

std::set<std::string> keys_defined_in_user_js(const std::string& text) {
    std::set<std::string> keys;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string line = trim(std::string_view(text).substr(start, newline == std::string::npos ? std::string::npos : newline - start));
        start = newline == std::string::npos ? text.size() + 1 : newline + 1;
        if (!line.starts_with("user_pref(")) {
            continue;
        }
        const std::string key = extract_pref_key(line);
        if (!key.empty()) {
            keys.insert(key);
        }
    }
    return keys;
}

class FirefoxAdapter final : public engine::Adapter {
  public:
    std::string_view id() const override {
        return kId;
    }

    std::uint32_t adapter_version() const override {
        return kAdapterVersion;
    }

    Result<engine::CapturedApplication> capture() const override {
        auto profile_dir = default_profile_dir();
        if (!profile_dir) {
            return std::unexpected(profile_dir.error());
        }

        bool has_user_js = false;
        const std::string user_js_text = read_text(*profile_dir / "user.js", has_user_js);
        bool has_prefs_js = false;
        const std::string prefs_js_text = read_text(*profile_dir / "prefs.js", has_prefs_js);
        if (!has_user_js && !has_prefs_js) {
            return std::unexpected(Error::make(ErrorCode::not_found, "no user.js or prefs.js found in " + profile_dir->string()));
        }

        std::size_t malformed = 0;
        const auto prefs = parse_prefs_js(prefs_js_text, malformed);
        const auto user_js_keys = keys_defined_in_user_js(user_js_text);

        std::string merged = user_js_text;
        if (!merged.empty() && merged.back() != '\n') {
            merged += '\n';
        }

        std::size_t excluded_secrets = 0;
        std::size_t excluded_machine = 0;
        std::size_t skipped_duplicates = 0;
        for (const auto& pref : prefs) {
            if (is_private_pref(pref.key) || value_mentions_secrets(pref.value)) {
                ++excluded_secrets;
                continue;
            }
            if (pref.value.is_string() && compat::contains_windows_path(pref.value.get<std::string>())) {
                ++excluded_machine;
                continue;
            }
            if (user_js_keys.contains(pref.key)) {
                ++skipped_duplicates;
                continue;
            }
            merged += "user_pref(" + nlohmann::json(pref.key).dump() + ", " + pref.value.dump() + ");\n";
        }

        if (merged.empty()) {
            return std::unexpected(Error::make(ErrorCode::not_found, "no portable Firefox preferences found in " + profile_dir->string()));
        }

        std::vector<std::string> warnings;
        if (excluded_secrets > 0) {
            warnings.push_back("excluded " + std::to_string(excluded_secrets) + " credential/private preference(s)");
        }
        if (excluded_machine > 0) {
            warnings.push_back("excluded " + std::to_string(excluded_machine) + " machine-specific preference(s) with Windows paths");
        }
        if (skipped_duplicates > 0) {
            warnings.push_back("skipped " + std::to_string(skipped_duplicates) + " preference(s) already set in user.js");
        }
        if (malformed > 0) {
            warnings.push_back("skipped " + std::to_string(malformed) + " malformed prefs.js line(s)");
        }

        engine::CapturedApplication application;
        application.spec = ApplicationSpec{std::string(kId), "", std::string(kId), kAdapterVersion};
        application.files = {{std::string(kPayloadPath),
                              std::vector<std::byte>(reinterpret_cast<const std::byte*>(merged.data()),
                                                     reinterpret_cast<const std::byte*>(merged.data() + merged.size()))}};
        application.warnings = std::move(warnings);
        return application;
    }

    Result<std::vector<engine::TargetResolution>> resolve_targets(const std::vector<std::string>& payload_paths) const override {
        auto profile_dir = default_profile_dir();
        if (!profile_dir) {
            return std::unexpected(profile_dir.error());
        }
        std::vector<engine::TargetResolution> resolutions;
        for (const auto& payload_path : payload_paths) {
            if (payload_path != kPayloadPath) {
                return std::unexpected(Error::make(ErrorCode::validation, "firefox adapter does not support payload member: " + payload_path));
            }
            resolutions.push_back({payload_path, *profile_dir / "user.js", "writing Firefox user.js (enforced preferences)"});
        }
        return resolutions;
    }
};

}

std::shared_ptr<engine::Adapter> make_firefox_adapter() {
    return std::make_shared<FirefoxAdapter>();
}

}
