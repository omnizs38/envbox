#pragma once

#include <memory>

#include "envbox/engine/adapter.hpp"

namespace envbox::adapters {

[[nodiscard]] std::shared_ptr<engine::Adapter> make_git_adapter();

}
