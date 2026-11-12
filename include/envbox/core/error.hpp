#pragma once

#include <expected>
#include <string>
#include <utility>

namespace envbox {

enum class ErrorCode {
    io,
    format,
    unsupported,
    validation,
    integrity,
    not_found,
};

struct Error {
    ErrorCode code;
    std::string message;

    [[nodiscard]] static Error make(ErrorCode code, std::string message) {
        return {code, std::move(message)};
    }
};

template <typename T>
using Result = std::expected<T, Error>;

using Status = Result<void>;

}
