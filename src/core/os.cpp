#include "envbox/core/os.hpp"

namespace envbox {

std::string_view to_string(OsKind kind) noexcept {
    switch (kind) {
    case OsKind::windows: return "windows";
    case OsKind::linux:   return "linux";
    case OsKind::macos:   return "macos";
    case OsKind::unknown: return "unknown";
    }
    return "unknown";
}

std::optional<OsKind> os_kind_from_string(std::string_view text) noexcept {
    if (text == "windows") return OsKind::windows;
    if (text == "linux")   return OsKind::linux;
    if (text == "macos")   return OsKind::macos;
    if (text == "unknown") return OsKind::unknown;
    return std::nullopt;
}

OsInfo current_os() noexcept {
    OsInfo info;

#if defined(_WIN32)
    info.kind = OsKind::windows;
#elif defined(__APPLE__) && defined(__MACH__)
    info.kind = OsKind::macos;
#elif defined(__linux__)
    info.kind = OsKind::linux;
#else
    info.kind = OsKind::unknown;
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
    info.architecture = "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    info.architecture = "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    info.architecture = "x86";
#else
    info.architecture = "unknown";
#endif

    info.version = {};
    return info;
}

}
