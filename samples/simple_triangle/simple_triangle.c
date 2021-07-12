struct tm_api_registry_api *tm_global_api_registry;

struct tm_allocator_api *tm_allocator_api;
struct tm_logger_api *tm_logger_api;

#include "simple_triangle.h"

#include <foundation/api_registry.h>
#include <foundation/allocator.h>
#include <foundation/application.h>
#include <foundation/log.h>

#include <plugins/os_window/os_window.h>

#if defined(TM_OS_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif


struct window_t
{
    struct tm_window_o *window;
};

struct tm_application_o
{
    struct tm_allocator_i allocator;

    struct window_t window;
};


static bool
tick_application(struct tm_application_o *app)
{
    return false;
}

static tm_application_o *
create_application(int argc, char **argv)
{
    const bool USE_END_OF_PAGE_ALLOCATOR = false;
    struct tm_allocator_i *standard_allocator = USE_END_OF_PAGE_ALLOCATOR ? tm_allocator_api->end_of_page : tm_allocator_api->system;
    struct tm_allocator_i a = tm_allocator_api->create_child(standard_allocator, "application");

    struct tm_application_o *app = tm_alloc(&a, sizeof(*app));
    *app = (struct tm_application_o) {
        .allocator = a,
    };

    return app;
}

static void
destroy_application(struct tm_application_o *app)
{
    struct tm_allocator_i a = app->allocator;
    tm_free(&a, app, sizeof(*app));
    tm_allocator_api->destroy_child(&a);
}

struct tm_application_api *tm_application_api = &(struct tm_application_api) {
    .create  = create_application,
    .tick    = tick_application,
    .destroy = destroy_application,
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
    tm_global_api_registry = reg;

    // foundation apis
    tm_allocator_api     = reg->get(TM_ALLOCATOR_API_NAME);
    tm_logger_api        = reg->get(TM_LOGGER_API_NAME);

    // other plugin apis

    tm_logger_api->print(TM_LOG_TYPE_INFO, "Hello!");

    tm_set_or_remove_api(reg, load, TM_APPLICATION_API_NAME, tm_application_api);
}
