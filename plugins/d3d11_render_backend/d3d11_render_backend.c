struct tm_api_registry_api *tm_global_api_registry;

struct tm_allocator_api *tm_allocator_api;
struct tm_error_api *tm_error_api;
struct tm_logger_api *tm_logger_api;

#include "d3d11_render_backend.h"

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/error.h>
#include <foundation/log.h>

#include <plugins/renderer/nil_render_backend.h> // TODO: remove me

#include <string.h>


// tm_d3d11_backend_i

struct tm_d3d11_backend_o
{
    struct tm_d3d11_backend_i i;

    struct tm_allocator_i allocator;
    struct tm_renderer_backend_i *render_backend;
};

static bool
d3d11__init(struct tm_d3d11_backend_o *inst)
{
    tm_logger_api->print(TM_LOG_TYPE_DEBUG, "d3d11__init");
    return true;
}

static void
d3d11__shutdown(struct tm_d3d11_backend_o *inst)
{
    tm_logger_api->print(TM_LOG_TYPE_DEBUG, "d3d11__shutdown");
    if (inst->render_backend)
    {
        struct tm_nil_renderer_backend_api *tm_nil_renderer_backend_api = tm_global_api_registry->get(TM_NIL_RENDER_BACKEND_API_NAME);
        tm_nil_renderer_backend_api->destroy(inst->render_backend);
        inst->render_backend = NULL;
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


// tm_d3d11_api

static struct tm_d3d11_backend_i *
api__create_backend(struct tm_allocator_i *allocator, struct tm_error_i *error)
{
    struct tm_allocator_i a = tm_allocator_api->create_child(allocator, "d3d11_backend");
    struct tm_d3d11_backend_o *o = tm_alloc(&a, sizeof(*o));
    memset(o, 0, sizeof(*o));

    o->i.inst                    = o;
    o->i.init                    = d3d11__init;
    o->i.shutdown                = d3d11__shutdown;
    o->i.agnostic_render_backend = d3d11__agnostic_render_backend;

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

    tm_set_or_remove_api(reg, load, TM_D3D11_API_NAME, tm_d3d11_api);
}
