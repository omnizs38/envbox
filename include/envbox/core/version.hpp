#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace envbox {

inline constexpr std::string_view kFormatName = "envbox";
inline constexpr std::uint32_t kFormatVersion = 1;
inline constexpr std::string_view kGeneratorName = "envbox";
inline constexpr std::string_view kGeneratorVersion = "1.0.0";

inline constexpr unsigned char kContainerMagic[8] = {'E', 'N', 'V', 'B', 'O', 'X', 0x1A, 0x01};
inline constexpr std::size_t kContainerMagicSize = sizeof(kContainerMagic);

}
