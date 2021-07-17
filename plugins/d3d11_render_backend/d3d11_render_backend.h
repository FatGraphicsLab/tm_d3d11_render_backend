#pragma once

#include <foundation/api_types.h>
#include <plugins/renderer/renderer_api_types.h>

struct tm_allocator_i;
struct tm_error_i;

// Create / Destroy devices

enum tm_d3d11_device_flag 
{
    // Set to include discrete GPUs when reasoning about physical device.
    TM_D3D11_DEVICE_FLAG_DISCRETE   = 0x1,

    // Set to include integrated GPUs when reasoning about physical device.
    TM_D3D11_DEVICE_FLAG_INTEGRATED = 0x2,
};

struct tm_d3d11_device_id
{
    uint32_t opaque;
};

struct tm_d3d11_backend_o;

struct tm_d3d11_backend_i
{
    void *inst;

    // Init and shutdown of D3D11 instance

    // Initialize D3D11 instance and enumerates physical devices.
    bool (*init)(struct tm_d3d11_backend_o *inst);

    // Shuts down the D3D11 instance.
    void (*shutdown)(struct tm_d3d11_backend_o *inst);

    // Retrieves the graphics API agnostic backend insterface.
    struct tm_renderer_backend_i *(*agnostic_render_backend)(struct tm_d3d11_backend_o *inst);

    // Create / Destroy devices
    
    // Returns number of available physical devices that fulfills the required_device_flags.
    uint32_t (*num_physical_devices)(struct tm_d3d11_backend_o *inst, uint32_t required_device_flags);

    // Returns the name of the physical device with index `device` that fulfills the
    // `required_device_flags`. `vendor_id` and `device_id` are optional.
    const char *(*physical_device_name)(struct tm_d3d11_backend_o *inst, uint32_t device,
        uint32_t required_device_flags, uint32_t *vendor_id, uint32_t *device_id);

    // Looks up a unique identifier to the physical device with index `device` matching
    // `required_device_flags` and store it in `result`. Returns true on success, else false.
    bool (*physical_device_id)(struct tm_d3d11_backend_o *inst, uint32_t device,
        uint32_t required_device_flags, struct tm_d3d11_device_id *result);
};


#define TM_D3D11_API_NAME "tm_d3d11_api"

struct tm_d3d11_api
{
    struct tm_d3d11_backend_i *(*create_backend)(struct tm_allocator_i *allocator, struct tm_error_i *error);
    void (*destroy_backend)(struct tm_d3d11_backend_i *backend);
};
