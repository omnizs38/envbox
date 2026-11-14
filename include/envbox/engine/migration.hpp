#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/profile.hpp"
#include "envbox/core/version.hpp"

namespace envbox::engine {

struct MigrationStep {
    std::uint32_t from_version = 0;
    std::uint32_t to_version = 0;
    std::function<Result<Profile>(Profile)> transform;
};

class MigrationRegistry {
  public:
    void add(MigrationStep step);
    [[nodiscard]] Result<Profile> migrate(Profile profile, std::uint32_t target_version) const;

  private:
    std::vector<MigrationStep> steps_;
};

[[nodiscard]] MigrationRegistry default_migrations();

}
