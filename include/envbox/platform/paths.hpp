#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace envbox::platform {

[[nodiscard]] std::optional<std::string> get_env(const char* name);

[[nodiscard]] std::filesystem::path home_directory();
[[nodiscard]] std::filesystem::path xdg_config_home();
[[nodiscard]] std::filesystem::path xdg_data_home();
[[nodiscard]] std::filesystem::path local_data_directory();
[[nodiscard]] std::filesystem::path envbox_data_dir();
[[nodiscard]] std::filesystem::path envbox_backup_root();

[[nodiscard]] std::string sanitize_filename(std::string_view name);

}
