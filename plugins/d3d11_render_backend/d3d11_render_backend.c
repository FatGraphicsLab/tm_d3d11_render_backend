struct tm_api_registry_api *tm_global_api_registry;

struct tm_allocator_api *tm_allocator_api;
struct tm_error_api *tm_error_api;
struct tm_logger_api *tm_logger_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;
struct tm_unicode_api *tm_unicode_api;

#include "d3d11_render_backend.h"

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/error.h>
#include <foundation/log.h>
#include <foundation/temp_allocator.h>
#include <foundation/unicode.h>
#include <plugins/renderer/nil_render_backend.h> // TODO: remove me

#define COBJMACROS
#include <dxgi.h>
#include <string.h>


// tm_d3d11_backend_i

#define MAX_ADAPTER_NUM (8)

struct d3d11_adapter_t
{
    DXGI_ADAPTER_DESC desc;
};

struct tm_d3d11_backend_o
{
    struct tm_d3d11_backend_i i;

    struct tm_allocator_i allocator;
    struct tm_renderer_backend_i *render_backend;

    struct IDXGIFactory1 *dxgi_factory;
    struct d3d11_adapter_t adapters[MAX_ADAPTER_NUM];
    uint32_t n_adapters;
    TM_PAD(4);
};

// Private

static void
d3d11__print_adapters(struct tm_d3d11_backend_o *inst)
{
    struct d3d11_adapter_t *adapter;
    TM_INIT_TEMP_ALLOCATOR(ta);

    for (uint32_t i = 0; i < inst->n_adapters; ++i)
    {
        adapter = &inst->adapters[i];
        tm_logger_api->printf(TM_LOG_TYPE_INFO, "Adapter #%u:", i);
        tm_logger_api->printf(TM_LOG_TYPE_INFO, "  desc: %s", tm_unicode_api->utf16_to_utf8(adapter->desc.Description, ta));
        tm_logger_api->printf(TM_LOG_TYPE_INFO, "  vendor_id: %u", adapter->desc.VendorId);
        tm_logger_api->printf(TM_LOG_TYPE_INFO, "  device_id: %u", adapter->desc.DeviceId);
        tm_logger_api->printf(TM_LOG_TYPE_INFO, "  video_memory: %uMb", adapter->desc.DedicatedVideoMemory / 1024 / 1024);
        tm_logger_api->printf(TM_LOG_TYPE_INFO, "  system_memory: %uMb", adapter->desc.DeviceId / 1024 / 1024);
        tm_logger_api->printf(TM_LOG_TYPE_INFO, "  local_unique_id: %u", adapter->desc.AdapterLuid);
    }

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

// Public

static bool
d3d11__init(struct tm_d3d11_backend_o *inst)
{
    tm_logger_api->print(TM_LOG_TYPE_DEBUG, "d3d11__init");

    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory, (void**)(&inst->dxgi_factory));
    if (FAILED(hr))
        return false;

    inst->n_adapters = 0;
    while (1)
    {
        IDXGIAdapter *pAdapter;
        struct d3d11_adapter_t *adapter;

        if (inst->n_adapters >= MAX_ADAPTER_NUM)
            break;

        hr = IDXGIFactory1_EnumAdapters(inst->dxgi_factory, inst->n_adapters, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;

        adapter = &inst->adapters[inst->n_adapters++];
        IDXGIAdapter_GetDesc(pAdapter, &adapter->desc);
        IDXGIAdapter_Release(pAdapter);
    }

    d3d11__print_adapters(inst);
    return true;
}

static void
d3d11__shutdown(struct tm_d3d11_backend_o *inst)
{
    tm_logger_api->print(TM_LOG_TYPE_DEBUG, "d3d11__shutdown");

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
    return 0;
}


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

static struct tm_d3d11_api tm_d3d11_api_instance = {
    .create_backend  = api__create_backend,
    .destroy_backend = api__destroy_backend, 
};

struct tm_d3d11_api *tm_d3d11_api = &tm_d3d11_api_instance;

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
    tm_global_api_registry = reg;

    // foundation apis
    tm_allocator_api           = reg->get(TM_ALLOCATOR_API_NAME);
    tm_error_api               = reg->get(TM_ERROR_API_NAME);
    tm_logger_api              = reg->get(TM_LOGGER_API_NAME);
    tm_temp_allocator_api      = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
    tm_unicode_api             = reg->get(TM_UNICODE_API_NAME);

    tm_set_or_remove_api(reg, load, TM_D3D11_API_NAME, tm_d3d11_api);
}
