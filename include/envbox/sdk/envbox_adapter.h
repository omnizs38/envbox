#ifndef ENVBOX_ADAPTER_H
#define ENVBOX_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENVBOX_SDK_ABI_VERSION 1u

#if defined(_WIN32)
#define ENVBOX_ADAPTER_API_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define ENVBOX_ADAPTER_API_EXPORT __attribute__((visibility("default")))
#else
#define ENVBOX_ADAPTER_API_EXPORT
#endif

typedef enum envbox_status {
    ENVBOX_OK = 0,
    ENVBOX_ERR_IO = 1,
    ENVBOX_ERR_FORMAT = 2,
    ENVBOX_ERR_UNSUPPORTED = 3,
    ENVBOX_ERR_VALIDATION = 4,
    ENVBOX_ERR_INTEGRITY = 5,
    ENVBOX_ERR_NOT_FOUND = 6,
    ENVBOX_ERR_CONFLICT = 7,
} envbox_status;

typedef struct envbox_manifest {
    uint32_t abi_version;
    const char* id;
    uint32_t adapter_version;
    const char* display_name;
} envbox_manifest;

typedef struct envbox_host {
    void* user;
    const char* (*get_env)(void* user, const char* key);
    const char* (*home_dir)(void* user);
    const char* (*config_dir)(void* user);
    const char* (*data_dir)(void* user);
} envbox_host;

typedef struct envbox_capture_sink {
    void* user;
    void (*add_file)(void* user, const char* payload_path, const void* data, size_t size);
    void (*add_warning)(void* user, const char* message);
} envbox_capture_sink;

typedef struct envbox_resolve_sink {
    void* user;
    void (*add_target)(void* user, const char* payload_path, const char* absolute_target, const char* note);
    void (*add_warning)(void* user, const char* message);
} envbox_resolve_sink;

typedef envbox_status (*envbox_capture_fn)(const envbox_host* host,
                                           envbox_capture_sink* sink,
                                           char* error_buffer,
                                           size_t error_buffer_size);

typedef envbox_status (*envbox_resolve_fn)(const envbox_host* host,
                                           const char* const* payload_paths,
                                           size_t payload_path_count,
                                           envbox_resolve_sink* sink,
                                           char* error_buffer,
                                           size_t error_buffer_size);

typedef struct envbox_adapter_api {
    uint32_t abi_version;
    const envbox_manifest* (*manifest)(void);
    envbox_capture_fn capture;
    envbox_resolve_fn resolve;
} envbox_adapter_api;

ENVBOX_ADAPTER_API_EXPORT const envbox_adapter_api* envbox_adapter_get_api(void);

#ifdef __cplusplus
}
#endif

#endif
