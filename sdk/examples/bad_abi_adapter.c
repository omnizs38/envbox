#include "envbox/sdk/envbox_adapter.h"

static const envbox_manifest g_manifest = {999u, "bad-abi", 1, "Incompatible adapter"};

static const envbox_manifest* get_manifest(void) {
    return &g_manifest;
}

static envbox_status bad_capture(const envbox_host* host,
                                 envbox_capture_sink* sink,
                                 char* error_buffer,
                                 size_t error_buffer_size) {
    (void)host;
    (void)sink;
    (void)error_buffer;
    (void)error_buffer_size;
    return ENVBOX_ERR_UNSUPPORTED;
}

static envbox_status bad_resolve(const envbox_host* host,
                                 const char* const* payload_paths,
                                 size_t payload_path_count,
                                 envbox_resolve_sink* sink,
                                 char* error_buffer,
                                 size_t error_buffer_size) {
    (void)host;
    (void)payload_paths;
    (void)payload_path_count;
    (void)sink;
    (void)error_buffer;
    (void)error_buffer_size;
    return ENVBOX_ERR_UNSUPPORTED;
}

static const envbox_adapter_api g_api = {
    999u,
    get_manifest,
    bad_capture,
    bad_resolve,
};

ENVBOX_ADAPTER_API_EXPORT const envbox_adapter_api* envbox_adapter_get_api(void) {
    return &g_api;
}
