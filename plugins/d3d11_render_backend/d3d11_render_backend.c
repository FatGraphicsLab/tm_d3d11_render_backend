struct tm_api_registry_api *tm_global_api_registry;

struct tm_allocator_api *tm_allocator_api;
struct tm_error_api *tm_error_api;
struct tm_logger_api *tm_logger_api;
struct tm_sprintf_api *tm_sprintf_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;
struct tm_unicode_api *tm_unicode_api;

#include "d3d11_render_backend.h"

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/carray_print.inl>
#include <foundation/error.h>
#include <foundation/log.h>
#include <foundation/sprintf.h>
#include <foundation/temp_allocator.h>
#include <foundation/unicode.h>
#include <plugins/renderer/nil_render_backend.h> // TODO: remove me
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/shader_compiler.h>
#include <plugins/renderer/shader_compiler_state_blocks_common.h>

#define COBJMACROS
#include <dxgi.h>
#include <string.h>


// tm_d3d11_backend_i

#define MAX_ADAPTER_NUM (8)

enum adapter_type_flag
{
    ADAPTER_TYPE__DISCRETE_GPU = 0,
    ADAPTER_TYPE__INTEGRATED_GPU,
    ADAPTER_TYPE__CPU,
};

struct d3d11_adapter_t
{
    IDXGIAdapter *pAdapter;
    uint8_t name[256];
    uint32_t vendor_id;
    uint32_t device_id;
    enum adapter_type_flag type;
    TM_PAD(4);
};

struct tm_d3d11_backend_o
{
    struct tm_d3d11_backend_i i;

    struct tm_allocator_i allocator;
    struct tm_renderer_backend_i *render_backend;

    struct IDXGIFactory1 *dxgi_factory;
    /* carray */ struct d3d11_adapter_t *adapters;
};

// https://pcisig.com/membership/member-companies
// GPU Vendor ID
enum
{
    PCI_VENDOR_ID__NVIDIA    = 0x10de,
    PCI_VENDOR_ID__AMD       = 0x1002,
    PCI_VENDOR_ID__MICROSOFT = 0x1414,
    PCI_VENDOR_ID__INTEL     = 0x8086,
};

// -------------------------------------------------------------------
// Debug

static const char *
adapter_type_name(enum adapter_type_flag flag)
{
    switch (flag)
    {
    case ADAPTER_TYPE__DISCRETE_GPU:
        return "Discrete GPU";
    case ADAPTER_TYPE__INTEGRATED_GPU:
        return "Integrated GPU";
    case ADAPTER_TYPE__CPU:
        return "CPU";
    default:
        return "Unknown";
    }
}

static void
d3d11__print_adapters(struct tm_d3d11_backend_o *inst)
{
    uint32_t n;
    struct d3d11_adapter_t *adapter;
    char *out = 0;
    struct tm_allocator_i *a = &inst->allocator;

    for (n = 0, adapter = inst->adapters; adapter != tm_carray_end(inst->adapters); ++n, ++adapter)
    {
        tm_carray_printf(&out, a, "Adapter #%u:\n", n);
        tm_carray_printf(&out, a, "  name: %s\n", adapter->name);
        tm_carray_printf(&out, a, "  type: %s\n", adapter_type_name(adapter->type));
        tm_carray_printf(&out, a, "  vendor_id: 0x%x\n", adapter->vendor_id);
        tm_carray_printf(&out, a, "  device_id: 0x%x\n", adapter->device_id);
    }

    tm_logger_api->print(TM_LOG_TYPE_INFO, out);
    tm_carray_free(out, a);
}

// -------------------------------------------------------------------
// Create / Destroy devices

static enum adapter_type_flag
guess_adapter_type(const struct d3d11_adapter_t *adapter)
{
    // adapter->name == "Microsoft Basic Render Driver", a software adapter from win8
    if (adapter->vendor_id == PCI_VENDOR_ID__MICROSOFT)
        return ADAPTER_TYPE__CPU;

    if (adapter->vendor_id == PCI_VENDOR_ID__NVIDIA || adapter->vendor_id == PCI_VENDOR_ID__AMD)
        return ADAPTER_TYPE__DISCRETE_GPU;

    return ADAPTER_TYPE__INTEGRATED_GPU;
}

static bool
accept_adapter(const struct d3d11_adapter_t *adapter, uint32_t required_device_flags)
{
    bool success = required_device_flags == 0;
    success |= (required_device_flags & TM_D3D11_DEVICE_FLAG_DISCRETE) && (adapter->type == ADAPTER_TYPE__DISCRETE_GPU);
    success |= (required_device_flags & TM_D3D11_DEVICE_FLAG_INTEGRATED) && (adapter->type == ADAPTER_TYPE__INTEGRATED_GPU);
    return success;
}

static struct d3d11_adapter_t *
find_adapter(struct tm_d3d11_backend_o *inst, uint32_t device, uint32_t required_device_flags)
{
    struct d3d11_adapter_t *adapter;
    uint32_t index = 0;
    for (adapter = inst->adapters; adapter != tm_carray_end(inst->adapters); ++adapter)
    {
        if (!accept_adapter(adapter, required_device_flags))
            continue;

        if (index == device)
            break;
    }
    return adapter != tm_carray_end(inst->adapters) ? adapter : NULL;
}

static void
d3d11__build_adapters(struct tm_d3d11_backend_o *inst)
{
    HRESULT hr;
    DXGI_ADAPTER_DESC desc;
    struct d3d11_adapter_t adapter;
    uint32_t n;

    n = 0;
    while (1)
    {
        
        hr = IDXGIFactory1_EnumAdapters(inst->dxgi_factory, n, &adapter.pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;

        ++n;

        TM_INIT_TEMP_ALLOCATOR(ta);
        IDXGIAdapter_GetDesc(adapter.pAdapter, &desc);

        strcpy(adapter.name, tm_unicode_api->utf16_to_utf8(desc.Description, ta));
        adapter.vendor_id = desc.VendorId;
        adapter.device_id = desc.DeviceId;

        // IDXGIFactory6 can detect discrete GPU, but only win10 supports it.
        // https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_6/ne-dxgi1_6-dxgi_gpu_preference
        //
        // So we just guess it.
        adapter.type = guess_adapter_type(&adapter);
        if (adapter.type == ADAPTER_TYPE__CPU)
            continue;

        tm_carray_push(inst->adapters, adapter, &inst->allocator);

        TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
    }
}

// Public

static bool
d3d11__init(struct tm_d3d11_backend_o *inst)
{
    tm_logger_api->print(TM_LOG_TYPE_DEBUG, "d3d11__init");

    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory, (void**)(&inst->dxgi_factory));
    if (FAILED(hr))
        return false;

    d3d11__build_adapters(inst);
    d3d11__print_adapters(inst);

    return true;
}

static void
d3d11__shutdown(struct tm_d3d11_backend_o *inst)
{
    struct d3d11_adapter_t *adapter;

    tm_logger_api->print(TM_LOG_TYPE_DEBUG, "d3d11__shutdown");

    for (adapter = inst->adapters; adapter != tm_carray_end(inst->adapters); ++adapter)
    {
        IDXGIAdapter_Release(adapter->pAdapter);
        adapter->pAdapter = 0;
    }
    tm_carray_free(inst->adapters, &inst->allocator);

    if (inst->dxgi_factory)
    {
        IDXGIFactory_Release(inst->dxgi_factory);
        inst->dxgi_factory = 0;
    }

    if (inst->render_backend)
    {
        struct tm_nil_renderer_backend_api *tm_nil_renderer_backend_api = tm_global_api_registry->get(TM_NIL_RENDER_BACKEND_API_NAME);
        tm_nil_renderer_backend_api->destroy(inst->render_backend);
        inst->render_backend = 0;
    }
}

static struct tm_renderer_backend_i *
d3d11__agnostic_render_backend(struct tm_d3d11_backend_o *inst)
{
    if (!inst->render_backend)
    {
        struct tm_nil_renderer_backend_api *tm_nil_renderer_backend_api = tm_global_api_registry->get(TM_NIL_RENDER_BACKEND_API_NAME);
        inst->render_backend = tm_nil_renderer_backend_api->create(&inst->allocator);
    }

    return inst->render_backend;
}

static uint32_t
d3d11__num_physical_devices(struct tm_d3d11_backend_o *inst, uint32_t required_device_flags)
{
    struct d3d11_adapter_t *adapter;
    uint32_t num_devices = 0;
    for (adapter = inst->adapters; adapter != tm_carray_end(inst->adapters); ++adapter)
    {
        if (accept_adapter(adapter, required_device_flags))
            num_devices++;
    }
    return num_devices;
}

static const char *
d3d11__physical_device_name(struct tm_d3d11_backend_o *inst, uint32_t device,
    uint32_t required_device_flags, uint32_t *vendor_id, uint32_t *device_id)
{
    struct d3d11_adapter_t *adapter;

    adapter = find_adapter(inst, device, required_device_flags);
    if (!adapter)
        return "";

    if (vendor_id)
        *vendor_id = adapter->vendor_id;

    if (device_id)
        *device_id = adapter->device_id;

    return adapter->name;
}

static bool
d3d11__physical_device_id(struct tm_d3d11_backend_o *inst, uint32_t device,
    uint32_t required_device_flags, struct tm_d3d11_device_id *result)
{
    struct d3d11_adapter_t *adapter;

    adapter = find_adapter(inst, device, required_device_flags);
    if (!adapter)
        return false;

    result->opaque = (uint32_t)(adapter - inst->adapters);
    return true;
}

static bool
d3d11__create_device(struct tm_d3d11_backend_o *inst, struct tm_d3d11_device_id device_id)
{
    return false;
}

static void
d3d11__destroy_device(struct tm_d3d11_backend_o *inst)
{

}


// -------------------------------------------------------------------
// d3d11 shader_compiler

struct d3d11_shader_compiler_o
{
    struct tm_allocator_i *allocator;
};

static struct tm_renderer_shader_compiler_o *
shader_compiler__init(struct tm_allocator_i *allocator)
{
    struct d3d11_shader_compiler_o *o = tm_alloc(allocator, sizeof(*o));
    o->allocator = allocator;
    return (struct tm_renderer_shader_compiler_o *) o;
}

static void
shader_compiler__shutdown(struct tm_renderer_shader_compiler_o *inst)
{
    struct d3d11_shader_compiler_o *o = (struct d3d11_shader_compiler_o *) inst;
    tm_free(o->allocator, o, sizeof(*o));
}

static const uint32_t supported_block_types[] = {
    TM_RENDERER_STATE_BLOCK_TYPE_TESSELLATION,
    TM_RENDERER_STATE_BLOCK_TYPE_RASTER,
    TM_RENDERER_STATE_BLOCK_TYPE_DEPTH_STENCIL,
    TM_RENDERER_STATE_BLOCK_TYPE_TEXTURE_SAMPLER,
    TM_RENDERER_STATE_BLOCK_TYPE_RENDER_TARGET_BLEND,
    TM_RENDERER_STATE_BLOCK_TYPE_BLEND,
    TM_RENDERER_STATE_BLOCK_TYPE_MULTI_SAMPLE,
};

static uint32_t
shader_compiler__num_state_block_types(struct tm_renderer_shader_compiler_o *inst)
{
    return TM_ARRAY_COUNT(supported_block_types);
}

static uint32_t
shader_compiler__state_block_type(struct tm_renderer_shader_compiler_o *inst, uint32_t state_block_type_idx)
{
    return supported_block_types[state_block_type_idx];
}

static const char *
shader_compiler__state_block_name(struct tm_renderer_shader_compiler_o *inst, uint32_t state_block_type)
{
    return tm_renderer_state_block_names[state_block_type];
}

static uint32_t
shader_compiler__num_states(struct tm_renderer_shader_compiler_o *inst, uint32_t state_block_type)
{
    switch (state_block_type)
    {
    case TM_RENDERER_STATE_BLOCK_TYPE_TESSELLATION:        return TM_RENDERER_TESSELLATION_STATE_MAX_STATES;
    case TM_RENDERER_STATE_BLOCK_TYPE_RASTER:              return TM_RENDERER_RASTER_STATE_MAX_STATES;
    case TM_RENDERER_STATE_BLOCK_TYPE_DEPTH_STENCIL:       return TM_RENDERER_DEPTH_STENCIL_STATE_MAX_STATES;
    case TM_RENDERER_STATE_BLOCK_TYPE_TEXTURE_SAMPLER:     return TM_RENDERER_SAMPLER_STATE_MAX_STATES;
    case TM_RENDERER_STATE_BLOCK_TYPE_RENDER_TARGET_BLEND: return TM_RENDERER_RENDER_TARGET_BLEND_STATE_MAX_STATES;
    case TM_RENDERER_STATE_BLOCK_TYPE_BLEND:               return TM_RENDERER_BLEND_STATE_MAX_STATES;
    case TM_RENDERER_STATE_BLOCK_TYPE_MULTI_SAMPLE:        return TM_RENDERER_MULTI_SAMPLE_MAX_STATES;
    default:                                               return 0;
    }
}

static const char *
shader_compiler__state_name(struct tm_renderer_shader_compiler_o *inst, uint32_t state_block_type,
    uint32_t state)
{
    switch (state_block_type)
    {
    case TM_RENDERER_STATE_BLOCK_TYPE_TESSELLATION:        return tm_renderer_tessellation_state_names[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_RASTER:              return tm_renderer_raster_state_names[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_DEPTH_STENCIL:       return tm_renderer_depth_stencil_state_names[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_TEXTURE_SAMPLER:     return tm_renderer_sampler_state_names[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_RENDER_TARGET_BLEND: return tm_renderer_render_target_blend_state_names[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_BLEND:               return tm_renderer_blend_state_names[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_MULTI_SAMPLE:        return tm_renderer_multi_sample_state_names[state];
    default:                                               return 0;
    }
}

static uint32_t
shader_compiler__value_type(struct tm_renderer_shader_compiler_o *inst, uint32_t state_block_type,
    uint32_t state)
{
    switch (state_block_type)
    {
    case TM_RENDERER_STATE_BLOCK_TYPE_TESSELLATION:        return tm_renderer_tessellation_state_value_types[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_RASTER:              return tm_renderer_raster_state_value_types[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_DEPTH_STENCIL:       return tm_renderer_depth_stencil_state_value_types[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_TEXTURE_SAMPLER:     return tm_renderer_sampler_state_types[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_RENDER_TARGET_BLEND: return tm_renderer_render_target_blend_state_types[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_BLEND:               return tm_renderer_blend_state_types[state];
    case TM_RENDERER_STATE_BLOCK_TYPE_MULTI_SAMPLE:        return tm_renderer_multi_sample_state_types[state];
    default:                                               return 0;
    }
}

static uint32_t
shader_compiler__num_values(struct tm_renderer_shader_compiler_o *inst, uint32_t value_type)
{
    switch (value_type)
    {
    case TM_RENDERER_STATE_VALUE_TYPE_COMPARE_OP:          return TM_RENDERER_COMPARE_OP_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_CULL:                return TM_RENDERER_RASTER_VALUE_CULL_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_FRONT_FACE:          return TM_RENDERER_RASTER_VALUE_FRONT_FACE_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_POLYGON_MODE:        return TM_RENDERER_RASTER_VALUE_POLYGON_MODE_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_STENCIL_OP:          return TM_RENDERER_STENCIL_OP_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_FILTER:              return TM_RENDERER_FILTER_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_MIP_MODE:            return TM_RENDERER_MIP_MODE_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_ADDRESS_MODE:        return TM_RENDERER_ADDRESS_MODE_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_BORDER_COLOR:        return TM_RENDERER_BORDER_COLOR_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_BLEND_FACTOR:        return TM_RENDERER_BLEND_FACTOR_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_BLEND_OPERATION:     return TM_RENDERER_BLEND_OP_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_BLEND_WRITE_MASK:    return TM_RENDERER_BLEND_WRITE_MASK_MAX_VALUES;
    case TM_RENDERER_STATE_VALUE_TYPE_LOGICAL_OPERATION:   return TM_RENDERER_LOGICAL_OP_MAX_VALUES;
    default:                                               return 0;
    }
}

static const char *
shader_compiler__value_name(struct tm_renderer_shader_compiler_o *inst, uint32_t value_type, uint32_t value)
{
    switch (value_type)
    {
    case TM_RENDERER_STATE_VALUE_TYPE_COMPARE_OP:          return tm_renderer_value_compare_op_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_CULL:                return tm_renderer_raster_value_cull_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_FRONT_FACE:          return tm_renderer_raster_value_front_face_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_POLYGON_MODE:        return tm_renderer_raster_value_polygon_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_STENCIL_OP:          return tm_renderer_value_stencil_op_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_FILTER:              return tm_renderer_value_filter_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_MIP_MODE:            return tm_renderer_value_mip_mode_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_ADDRESS_MODE:        return tm_renderer_value_address_mode_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_BORDER_COLOR:        return tm_renderer_value_border_color_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_BLEND_FACTOR:        return tm_renderer_value_blend_factor_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_BLEND_OPERATION:     return tm_renderer_value_blend_op_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_BLEND_WRITE_MASK:    return tm_renderer_value_write_mask_names[value];
    case TM_RENDERER_STATE_VALUE_TYPE_LOGICAL_OPERATION:   return tm_renderer_value_logical_op_names[value];
    default:                                               return 0;
    }
}

static uint32_t
shader_compiler__enum_value(struct tm_renderer_shader_compiler_o *inst, uint32_t value_type, uint32_t value)
{
    switch (value_type)
    {
    case TM_RENDERER_STATE_VALUE_TYPE_BLEND_WRITE_MASK: return tm_renderer_enum_value_write_mask[value];
    default:                                            return 0;
    }
}

static bool
shader_compiler__bindless(struct tm_renderer_shader_compiler_o *inst)
{
    return false;
}

static tm_renderer_bindless_accessor_t
shader_compiler__bindless_access_buffer(struct tm_renderer_shader_compiler_o *inst, uint32_t usage_flags)
{
    tm_renderer_bindless_accessor_t ba = {0};
    return ba;
}

static tm_renderer_bindless_accessor_t
shader_compiler__bindless_access_image(struct tm_renderer_shader_compiler_o *inst, uint32_t type,
    uint32_t usage_flags)
{
    tm_renderer_bindless_accessor_t ba = { 0 };
    return ba;
}

static tm_renderer_bindless_accessor_t
shader_compiler__bindless_access_sampler(struct tm_renderer_shader_compiler_o *inst)
{
    tm_renderer_bindless_accessor_t ba = { 0 };
    return ba;
}

static tm_renderer_bindless_accessor_t
shader_compiler__bindless_access_acceleration_structure(struct tm_renderer_shader_compiler_o *inst)
{
    tm_renderer_bindless_accessor_t ba = { 0 };
    return ba;
}

static struct tm_renderer_shader_blob_t
shader_compiler__compile_state_block(struct tm_renderer_shader_compiler_o *inst, uint32_t bind_type,
    uint32_t block_type, const tm_renderer_state_value_pair_t *states, uint32_t num_raster_states)
{
    struct tm_renderer_shader_blob_t blob = { 0 };
    return blob;
}

static struct tm_renderer_shader_blob_t
shader_compiler__compile_shader(struct tm_renderer_shader_compiler_o *inst, const char *source,
    const char *entry_point, uint32_t source_language, uint32_t stage)
{
    struct tm_renderer_shader_blob_t blob = { 0 };
    return blob;
}

static void
shader_compiler__release_blob(struct tm_renderer_shader_compiler_o *inst, tm_renderer_shader_blob_t blob)
{

}

static struct tm_renderer_shader_compiler_api d3d11_shader_compiler = {
    // Init & Shutdown
    .init                  = shader_compiler__init,
    .shutdown              = shader_compiler__shutdown,

    // State block key:value enumeration
    .num_state_block_types = shader_compiler__num_state_block_types,
    .state_block_type      = shader_compiler__state_block_type,
    .state_block_name      = shader_compiler__state_block_name,
    .num_states            = shader_compiler__num_states,
    .state_name            = shader_compiler__state_name,
    .value_type            = shader_compiler__value_type,
    .num_values            = shader_compiler__num_values,
    .value_name            = shader_compiler__value_name,
    .enum_value            = shader_compiler__enum_value,

    // State block compilation
    .compile_state_block   = shader_compiler__compile_state_block,

    // Shader compilation
    .compile_shader        = shader_compiler__compile_shader,

    // Bindless (D3D11 don't support)
    .bindless                               = shader_compiler__bindless,
    .bindless_access_buffer                 = shader_compiler__bindless_access_buffer,
    .bindless_access_image                  = shader_compiler__bindless_access_image,
    .bindless_access_sampler                = shader_compiler__bindless_access_sampler,
    .bindless_access_acceleration_structure = shader_compiler__bindless_access_acceleration_structure,

    // Common
    .release_blob          = shader_compiler__release_blob,
};

// -------------------------------------------------------------------
// tm_d3d11_api

static struct tm_d3d11_backend_i *
api__create_backend(struct tm_allocator_i *allocator, struct tm_error_i *error)
{
    struct tm_allocator_i a = tm_allocator_api->create_child(allocator, "d3d11_render_backend");
    struct tm_d3d11_backend_o *o = tm_alloc(&a, sizeof(*o));
    memset(o, 0, sizeof(*o));

    o->i.inst                    = o;
    o->i.init                    = d3d11__init;
    o->i.shutdown                = d3d11__shutdown;
    o->i.agnostic_render_backend = d3d11__agnostic_render_backend;
    o->i.num_physical_devices    = d3d11__num_physical_devices;
    o->i.physical_device_name    = d3d11__physical_device_name;
    o->i.physical_device_id      = d3d11__physical_device_id;
    o->i.create_device           = d3d11__create_device;
    o->i.destroy_device          = d3d11__destroy_device;

    o->allocator                 = a;

    return &o->i;
}

static void
api__destroy_backend(struct tm_d3d11_backend_i *backend)
{
    struct tm_d3d11_backend_o *o = backend->inst;
    struct tm_allocator_i a = o->allocator;
    tm_free(&a, o, sizeof(*o));
    tm_allocator_api->destroy_child(&a);
}

static struct tm_renderer_shader_compiler_api *
api__shader_compiler(void)
{
    return &d3d11_shader_compiler;
}

static struct tm_d3d11_api tm_d3d11_api_instance = {
    .create_backend  = api__create_backend,
    .destroy_backend = api__destroy_backend,
    .shader_compiler = api__shader_compiler,
};

struct tm_d3d11_api *tm_d3d11_api = &tm_d3d11_api_instance;

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
    tm_global_api_registry = reg;

    // foundation apis
    tm_allocator_api           = reg->get(TM_ALLOCATOR_API_NAME);
    tm_error_api               = reg->get(TM_ERROR_API_NAME);
    tm_logger_api              = reg->get(TM_LOGGER_API_NAME);
    tm_sprintf_api             = reg->get(TM_SPRINTF_API_NAME);
    tm_temp_allocator_api      = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
    tm_unicode_api             = reg->get(TM_UNICODE_API_NAME);

    tm_set_or_remove_api(reg, load, TM_D3D11_API_NAME, tm_d3d11_api);
}
