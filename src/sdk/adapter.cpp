#include "envbox/sdk/loader.hpp"

#include <string>

#include "envbox/platform/paths.hpp"

namespace envbox::sdk {
namespace {

ErrorCode map_status(int status) {
    switch (static_cast<envbox_status>(status)) {
        case ENVBOX_OK:          return ErrorCode::io;
        case ENVBOX_ERR_IO:      return ErrorCode::io;
        case ENVBOX_ERR_FORMAT:  return ErrorCode::format;
        case ENVBOX_ERR_UNSUPPORTED: return ErrorCode::unsupported;
        case ENVBOX_ERR_VALIDATION:  return ErrorCode::validation;
        case ENVBOX_ERR_INTEGRITY:   return ErrorCode::integrity;
        case ENVBOX_ERR_NOT_FOUND:   return ErrorCode::not_found;
        case ENVBOX_ERR_CONFLICT:    return ErrorCode::conflict;
    }
    return ErrorCode::io;
}

struct HostStorage {
    std::string env;
    std::string home;
    std::string config;
    std::string data;
};

const char* make_c_string(std::string& storage, const std::filesystem::path& path) {
    storage = path.empty() ? std::string{} : path.string();
    return storage.empty() ? nullptr : storage.c_str();
}

const char* host_get_env(void* user, const char* key) {
    auto* storage = static_cast<HostStorage*>(user);
    if (key == nullptr) {
        return nullptr;
    }
    const auto value = platform::get_env(key);
    if (!value) {
        storage->env.clear();
        return nullptr;
    }
    storage->env = *value;
    return storage->env.c_str();
}

const char* host_home_dir(void* user) {
    return make_c_string(static_cast<HostStorage*>(user)->home, platform::home_directory());
}

const char* host_config_dir(void* user) {
    return make_c_string(static_cast<HostStorage*>(user)->config, platform::xdg_config_home());
}

const char* host_data_dir(void* user) {
    return make_c_string(static_cast<HostStorage*>(user)->data, platform::xdg_data_home());
}

struct CaptureSink {
    engine::CapturedApplication application;
};

void sink_add_file(void* user, const char* payload_path, const void* data, size_t size) {
    auto* sink = static_cast<CaptureSink*>(user);
    if (payload_path == nullptr || (data == nullptr && size != 0)) {
        return;
    }
    PayloadFile file;
    file.path = payload_path;
    file.data.assign(static_cast<const std::byte*>(data), static_cast<const std::byte*>(data) + size);
    sink->application.files.push_back(std::move(file));
}

void sink_add_warning(void* user, const char* message) {
    auto* sink = static_cast<CaptureSink*>(user);
    if (message != nullptr) {
        sink->application.warnings.emplace_back(message);
    }
}

struct ResolveSink {
    std::vector<engine::TargetResolution> resolutions;
};

void resolve_add_target(void* user, const char* payload_path, const char* absolute_target, const char* note) {
    auto* sink = static_cast<ResolveSink*>(user);
    if (payload_path == nullptr || absolute_target == nullptr) {
        return;
    }
    sink->resolutions.push_back({payload_path, std::filesystem::path(absolute_target), note == nullptr ? "" : note});
}

class CApiAdapter final : public engine::Adapter {
  public:
    CApiAdapter(const envbox_manifest* manifest, const envbox_adapter_api* api)
        : manifest_(manifest), api_(api) {}

    std::string_view id() const override {
        return manifest_->id != nullptr ? manifest_->id : "";
    }

    std::uint32_t adapter_version() const override {
        return manifest_->adapter_version;
    }

    Result<engine::CapturedApplication> capture() const override {
        if (api_->capture == nullptr) {
            return std::unexpected(Error::make(ErrorCode::unsupported, "adapter does not implement capture"));
        }
        HostStorage storage;
        const envbox_host host{&storage, host_get_env, host_home_dir, host_config_dir, host_data_dir};
        CaptureSink sink;
        envbox_capture_sink sink_view{&sink, sink_add_file, sink_add_warning};
        char error_buffer[512] = {};
        const envbox_status status = api_->capture(&host, &sink_view, error_buffer, sizeof error_buffer);
        if (status != ENVBOX_OK) {
            const std::string message = error_buffer[0] != '\0' ? std::string(error_buffer) : std::string("adapter capture failed");
            return std::unexpected(Error::make(map_status(status), message));
        }
        sink.application.spec = ApplicationSpec{std::string(id()), "", std::string(id()), adapter_version()};
        return sink.application;
    }

    Result<std::vector<engine::TargetResolution>> resolve_targets(const std::vector<std::string>& payload_paths) const override {
        if (api_->resolve == nullptr) {
            return std::unexpected(Error::make(ErrorCode::unsupported, "adapter does not implement resolve_targets"));
        }
        std::vector<const char*> raw_paths;
        raw_paths.reserve(payload_paths.size());
        for (const auto& path : payload_paths) {
            raw_paths.push_back(path.c_str());
        }
        HostStorage storage;
        const envbox_host host{&storage, host_get_env, host_home_dir, host_config_dir, host_data_dir};
        ResolveSink sink;
        envbox_resolve_sink sink_view{&sink, resolve_add_target, sink_add_warning};
        char error_buffer[512] = {};
        const envbox_status status = api_->resolve(&host, raw_paths.data(), raw_paths.size(), &sink_view, error_buffer,
                                                   sizeof error_buffer);
        if (status != ENVBOX_OK) {
            const std::string message = error_buffer[0] != '\0' ? std::string(error_buffer) : std::string("adapter resolve failed");
            return std::unexpected(Error::make(map_status(status), message));
        }
        return sink.resolutions;
    }

  private:
    const envbox_manifest* manifest_;
    const envbox_adapter_api* api_;
};

}

std::shared_ptr<engine::Adapter> wrap_c_adapter(const envbox_adapter_api* api) {
    if (api == nullptr || api->manifest == nullptr) {
        return nullptr;
    }
    const envbox_manifest* manifest = api->manifest();
    if (manifest == nullptr || manifest->id == nullptr || manifest->id[0] == '\0') {
        return nullptr;
    }
    return std::make_shared<CApiAdapter>(manifest, api);
}

}
