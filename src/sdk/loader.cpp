#include "envbox/sdk/loader.hpp"

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace envbox::sdk {
namespace {

constexpr const char* kEntryPoint = "envbox_adapter_get_api";

#if defined(_WIN32)
using NativeHandle = HMODULE;

struct LibraryHandle {
    NativeHandle handle = nullptr;
    ~LibraryHandle() {
        if (handle != nullptr) {
            FreeLibrary(handle);
        }
    }
};

Result<std::shared_ptr<LibraryHandle>> open_library(const std::filesystem::path& path) {
    HMODULE handle = LoadLibraryW(path.wstring().c_str());
    if (handle == nullptr) {
        return std::unexpected(Error::make(ErrorCode::io, "cannot load adapter library: " + path.string() + " (error " + std::to_string(GetLastError()) + ")"));
    }
    auto holder = std::make_shared<LibraryHandle>();
    holder->handle = handle;
    return holder;
}

void* resolve_symbol(NativeHandle handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(handle, name));
}
#else
using NativeHandle = void*;

struct LibraryHandle {
    NativeHandle handle = nullptr;
    ~LibraryHandle() {
        if (handle != nullptr) {
            dlclose(handle);
        }
    }
};

Result<std::shared_ptr<LibraryHandle>> open_library(const std::filesystem::path& path) {
    void* handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        return std::unexpected(Error::make(ErrorCode::io, "cannot load adapter library: " + path.string() + (error != nullptr ? ": " + std::string(error) : "")));
    }
    auto holder = std::make_shared<LibraryHandle>();
    holder->handle = handle;
    return holder;
}

void* resolve_symbol(NativeHandle handle, const char* name) {
    return dlsym(handle, name);
}
#endif

}

Result<DynamicAdapter> load_dynamic_adapter(const std::filesystem::path& library_path) {
    if (!std::filesystem::is_regular_file(library_path)) {
        return std::unexpected(Error::make(ErrorCode::not_found, "adapter library not found: " + library_path.string()));
    }

    auto library = open_library(library_path);
    if (!library) {
        return std::unexpected(library.error());
    }

    void* symbol = resolve_symbol((*library)->handle, kEntryPoint);
    if (symbol == nullptr) {
        return std::unexpected(Error::make(ErrorCode::format, "adapter library does not export " + std::string(kEntryPoint) + ": " + library_path.string()));
    }

    auto* entry = reinterpret_cast<const envbox_adapter_api* (*)()>(symbol);
    const envbox_adapter_api* api = entry();
    if (api == nullptr) {
        return std::unexpected(Error::make(ErrorCode::format, "adapter returned a null API table: " + library_path.string()));
    }
    if (api->abi_version != ENVBOX_SDK_ABI_VERSION) {
        return std::unexpected(Error::make(ErrorCode::unsupported, "adapter ABI version " + std::to_string(api->abi_version) + " is not supported (host supports " + std::to_string(ENVBOX_SDK_ABI_VERSION) + "): " + library_path.string()));
    }
    if (api->manifest == nullptr) {
        return std::unexpected(Error::make(ErrorCode::format, "adapter does not provide a manifest: " + library_path.string()));
    }
    const envbox_manifest* manifest = api->manifest();
    if (manifest == nullptr || manifest->abi_version != ENVBOX_SDK_ABI_VERSION) {
        return std::unexpected(Error::make(ErrorCode::unsupported, "adapter manifest ABI version mismatch: " + library_path.string()));
    }

    auto adapter = wrap_c_adapter(api);
    if (adapter == nullptr) {
        return std::unexpected(Error::make(ErrorCode::format, "adapter manifest is invalid: " + library_path.string()));
    }
    return DynamicAdapter{std::shared_ptr<void>(*library), std::move(adapter)};
}

}
