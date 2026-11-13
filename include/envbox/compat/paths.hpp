#pragma once

#include <string>
#include <string_view>

#include "envbox/core/error.hpp"
#include "envbox/core/os.hpp"

namespace envbox::compat {

enum class PathKind {
    empty,
    relative,
    windows_absolute,
    unix_absolute,
};

[[nodiscard]] PathKind classify_path(std::string_view path);
[[nodiscard]] bool contains_windows_path(std::string_view text);

struct PathConversion {
    std::string path;
    bool exact = false;
    std::string note;
};

[[nodiscard]] Result<PathConversion> convert_path(std::string_view path, OsKind source, OsKind target);

}
