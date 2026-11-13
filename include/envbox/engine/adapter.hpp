#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "envbox/core/error.hpp"
#include "envbox/core/os.hpp"
#include "envbox/core/profile.hpp"

namespace envbox::engine {

struct CapturedApplication {
    ApplicationSpec spec;
    std::vector<PayloadFile> files;
    std::vector<std::string> warnings;
};

struct TargetResolution {
    std::string payload_path;
    std::filesystem::path target;
    std::string note;
};

struct PayloadAdaptation {
    std::vector<std::byte> data;
    std::vector<std::string> warnings;
};

class Adapter {
  public:
    virtual ~Adapter() = default;
    virtual std::string_view id() const = 0;
    virtual std::uint32_t adapter_version() const = 0;

    virtual Result<CapturedApplication> capture() const = 0;
    virtual Result<std::vector<TargetResolution>> resolve_targets(
        const std::vector<std::string>& payload_paths) const = 0;

    virtual Result<PayloadAdaptation> adapt_payload(const TargetResolution&,
                                                    const std::vector<std::byte>& data,
                                                    const Profile&,
                                                    OsKind) const {
        return PayloadAdaptation{data, {}};
    }
};

class AdapterRegistry {
  public:
    void add(std::shared_ptr<Adapter> adapter);
    [[nodiscard]] const Adapter* find(std::string_view adapter_id) const;
    [[nodiscard]] std::vector<const Adapter*> all() const;

  private:
    std::vector<std::shared_ptr<Adapter>> adapters_;
};

}
