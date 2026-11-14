#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "envbox/sdk/envbox_adapter.h"

static const envbox_manifest g_manifest = {ENVBOX_SDK_ABI_VERSION, "example", 1, "Example adapter"};

static const char* kPayloadPath = "applications/example/config.json";

static void set_error(char* buffer, size_t size, const char* message) {
    if (buffer != NULL && size > 0) {
        snprintf(buffer, size, "%s", message);
    }
}

static const envbox_manifest* get_manifest(void) {
    return &g_manifest;
}

static envbox_status example_capture(const envbox_host* host,
                                     envbox_capture_sink* sink,
                                     char* error_buffer,
                                     size_t error_buffer_size) {
    const char* home = host->home_dir(host->user);
    if (home == NULL) {
        set_error(error_buffer, error_buffer_size, "no home directory");
        return ENVBOX_ERR_NOT_FOUND;
    }

    char path[1024];
    snprintf(path, sizeof path, "%s/.example/config.json", home);
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error_buffer, error_buffer_size, "no example configuration found");
        return ENVBOX_ERR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error(error_buffer, error_buffer_size, "cannot seek configuration file");
        return ENVBOX_ERR_IO;
    }
    const long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(error_buffer, error_buffer_size, "cannot read configuration file");
        return ENVBOX_ERR_IO;
    }

    char* buffer = (char*)malloc((size_t)size);
    if (buffer == NULL && size > 0) {
        fclose(file);
        set_error(error_buffer, error_buffer_size, "out of memory");
        return ENVBOX_ERR_IO;
    }
    const size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(buffer);
        set_error(error_buffer, error_buffer_size, "short read on configuration file");
        return ENVBOX_ERR_IO;
    }

    sink->add_file(sink->user, kPayloadPath, buffer, (size_t)size);
    free(buffer);
    return ENVBOX_OK;
}

static envbox_status example_resolve(const envbox_host* host,
                                     const char* const* payload_paths,
                                     size_t payload_path_count,
                                     envbox_resolve_sink* sink,
                                     char* error_buffer,
                                     size_t error_buffer_size) {
    const char* home = host->home_dir(host->user);
    if (home == NULL) {
        set_error(error_buffer, error_buffer_size, "no home directory");
        return ENVBOX_ERR_NOT_FOUND;
    }

    char target[1024];
    snprintf(target, sizeof target, "%s/.example/config.json", home);

    for (size_t i = 0; i < payload_path_count; ++i) {
        if (strcmp(payload_paths[i], kPayloadPath) != 0) {
            set_error(error_buffer, error_buffer_size, "unsupported payload member");
            return ENVBOX_ERR_VALIDATION;
        }
        sink->add_target(sink->user, payload_paths[i], target, "example configuration");
    }
    return ENVBOX_OK;
}

static const envbox_adapter_api g_api = {
    ENVBOX_SDK_ABI_VERSION,
    get_manifest,
    example_capture,
    example_resolve,
};

ENVBOX_ADAPTER_API_EXPORT const envbox_adapter_api* envbox_adapter_get_api(void) {
    return &g_api;
}
