#include "envbox/compat/paths.hpp"

#include <cctype>
#include <string>

namespace envbox::compat {
namespace {

bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_ascii_alnum(char c) {
    return is_ascii_alpha(c) || (c >= '0' && c <= '9');
}

std::string normalize_separators(std::string text, char separator) {
    for (auto& c : text) {
        if (c == '\\' || c == '/') {
            c = separator;
        }
    }
    return text;
}

std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string os_name(OsKind kind) {
    return std::string(to_string(kind));
}

bool split_user_path(std::string_view path, std::string_view prefix_lower, std::string& user, std::string& tail) {
    const std::string prefix = ascii_lower(prefix_lower);
    if (path.size() < prefix.size() || ascii_lower(path.substr(0, prefix.size())) != prefix) {
        return false;
    }
    const std::string_view after = path.substr(prefix.size());
    const std::size_t slash = after.find('/');
    const std::string_view user_part = (slash == std::string_view::npos) ? after : after.substr(0, slash);
    if (user_part.empty()) {
        return false;
    }
    user.assign(user_part);
    tail.assign(slash == std::string_view::npos ? std::string_view{} : after.substr(slash));
    return true;
}

}

PathKind classify_path(std::string_view path) {
    if (path.empty()) {
        return PathKind::empty;
    }
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
        return PathKind::windows_absolute;
    }
    if (path.size() >= 2 && is_ascii_alpha(path[0]) && path[1] == ':' &&
        (path.size() == 2 || path[2] == '\\' || path[2] == '/')) {
        return PathKind::windows_absolute;
    }
    if (path[0] == '/') {
        return PathKind::unix_absolute;
    }
    return PathKind::relative;
}

bool contains_windows_path(std::string_view text) {
    for (std::size_t i = 0; i + 2 < text.size(); ++i) {
        if (!is_ascii_alpha(text[i]) || text[i + 1] != ':') {
            continue;
        }
        const char separator = text[i + 2];
        if (separator != '\\' && separator != '/') {
            continue;
        }
        if (i > 0 && is_ascii_alnum(text[i - 1])) {
            continue;
        }
        return true;
    }
    return false;
}

Result<PathConversion> convert_path(std::string_view path_text, OsKind source, OsKind target) {
    const std::string path(path_text);
    const PathKind kind = classify_path(path);

    if (kind == PathKind::empty || kind == PathKind::relative) {
        return PathConversion{path, true, "relative path, no conversion needed"};
    }
    if (source == target) {
        return PathConversion{path, true, {}};
    }
    if (source == OsKind::unknown || target == OsKind::unknown) {
        return std::unexpected(Error::make(ErrorCode::validation, "cannot convert paths between unknown operating systems"));
    }

    if (kind == PathKind::windows_absolute) {
        if (path.starts_with("\\\\") || path.starts_with("//")) {
            return PathConversion{normalize_separators(path, '/'), false, "UNC network paths have no automatic " + os_name(target) + " equivalent"};
        }
        const char drive = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));
        std::string rest = path.size() > 2 ? normalize_separators(path.substr(2), '/') : std::string("/");

        if (drive == 'C' && target != OsKind::windows) {
            std::string user;
            std::string tail;
            if (split_user_path(rest, "/users/", user, tail)) {
                if (target == OsKind::linux) {
                    return PathConversion{"/home/" + user + tail, true, {}};
                }
                if (target == OsKind::macos) {
                    return PathConversion{"/Users/" + user + tail, true, {}};
                }
            }
        }

        if (target == OsKind::linux) {
            return PathConversion{"/mnt/" + std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(drive)))) + rest,
                                  false, "mapped to a WSL-style mount point; adjust to the real mount path"};
        }
        return PathConversion{rest, false, "drive-letter paths have no automatic macOS mapping (mount point unknown)"};
    }

    if (kind == PathKind::unix_absolute && target == OsKind::windows) {
        std::string user;
        std::string tail;
        if (split_user_path(path, "/home/", user, tail) || split_user_path(path, "/Users/", user, tail)) {
            return PathConversion{normalize_separators("C:/Users/" + user + tail, '\\'), true, {}};
        }
        return PathConversion{normalize_separators(path, '\\'), false, "prefix with a drive letter is required on Windows"};
    }

    return PathConversion{path, true, {}};
}

}
