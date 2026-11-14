#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/engine/adapter.hpp"
#include "envbox/sdk/envbox_adapter.h"

namespace envbox::sdk {

struct DynamicAdapter {
    std::shared_ptr<void> library;
    std::shared_ptr<engine::Adapter> adapter;
};

[[nodiscard]] Result<DynamicAdapter> load_dynamic_adapter(const std::filesystem::path& library_path);
[[nodiscard]] std::shared_ptr<engine::Adapter> wrap_c_adapter(const envbox_adapter_api* api);

}
