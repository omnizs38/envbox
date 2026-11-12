#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace envbox {

enum class OsKind {
    unknown,
    windows,
    linux,
    macos,
};

[[nodiscard]] std::string_view to_string(OsKind kind) noexcept;
[[nodiscard]] std::optional<OsKind> os_kind_from_string(std::string_view text) noexcept;

struct OsInfo {
    OsKind kind = OsKind::unknown;
    std::string architecture;
    std::string version;
};

[[nodiscard]] OsInfo current_os() noexcept;

}
