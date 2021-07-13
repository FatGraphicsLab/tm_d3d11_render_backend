struct tm_api_registry_api *tm_global_api_registry;

struct tm_allocator_api *tm_allocator_api;
struct tm_localizer_api *tm_localizer_api;
struct tm_logger_api *tm_logger_api;
struct tm_os_api *tm_os_api;
struct tm_path_api *tm_path_api;
struct tm_plugins_api *tm_plugins_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;

struct tm_os_window_api *tm_os_window_api;
struct tm_dxc_shader_compiler_api *tm_dxc_shader_compiler_api;


#include "simple_triangle.h"

#include <foundation/api_registry.h>
#include <foundation/allocator.h>
#include <foundation/application.h>
#include <foundation/carray.inl>
#include <foundation/localizer.h>
#include <foundation/log.h>
#include <foundation/os.h>
#include <foundation/path.h>
#include <foundation/plugin.h>
#include <foundation/string.inl>
#include <foundation/temp_allocator.h>

#include <plugins/dxc_shader_compiler/dxc_compiler.h>
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
    // Run message pump for all window
    tm_os_window_api->update_window(app->window.window);

    // Slow down update rate when app is out of focus
    struct tm_window_status_t win_status = tm_os_window_api->status(app->window.window);
    const bool app_has_focus = win_status.has_focus || win_status.is_under_cursor;
    if (!app_has_focus)
        tm_os_api->thread->sleep(0.25f);

    if (tm_os_window_api->has_user_requested_close(app->window.window, true))
        return false;

    return true;
}

static struct window_t *
create_window(struct tm_application_o *app, void *res_buf, tm_rect_t r, bool center_on_screen)
{
    struct window_t *win = &app->window;

    enum tm_os_window_style window_style = TM_OS_WINDOW_STYLE_VISIBLE | TM_OS_WINDOW_STYLE_DPI_SCALING_AWARE;
    if (center_on_screen)
        window_style |= TM_OS_WINDOW_STYLE_CENTERED;

    win->window = tm_os_window_api->create_window("The Machinery - Simple Triangle", r, window_style, 0);

    return win;
}

static void
setup_initial_window(struct tm_application_o *app, void *res_buf)
{
    tm_rect_t rect = { 100, 100, 1440, 900 };
    create_window(app, res_buf, rect, true);
}

static tm_application_o *
create_application(int argc, char **argv)
{
#if defined(TM_OS_WINDOWS)
    HINSTANCE hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32)
    {
        typedef BOOL(WINAPI* SetProcessDPIAwareFn)(void);
        SetProcessDPIAwareFn fn = (SetProcessDPIAwareFn)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (fn) fn();
        FreeLibrary(hUser32);
    }
#endif

    bool hot_reload_plugins = false;

    // Attempt to load plugins
    const char *exe_path = tm_os_api->system->exe_path(argv[0]);
    {
        TM_INIT_TEMP_ALLOCATOR(ta);

        const tm_str_t exe_dir = tm_path_api->directory(tm_str(exe_path));
        const tm_str_t plugin_dir = tm_path_api->join(exe_dir, tm_str("plugins"), ta);
        const char **plugins = tm_plugins_api->enumerate(tm_cstring(plugin_dir, ta), ta);
        for (const char **p = plugins; p != tm_carray_end(plugins); ++p)
            tm_plugins_api->load(*p, hot_reload_plugins);

        TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
    }
    tm_global_api_registry->log_missing_apis();

    const bool USE_END_OF_PAGE_ALLOCATOR = false;
    struct tm_allocator_i *standard_allocator = USE_END_OF_PAGE_ALLOCATOR ? tm_allocator_api->end_of_page : tm_allocator_api->system;
    struct tm_allocator_i a = tm_allocator_api->create_child(standard_allocator, "application");

    struct tm_application_o *app = tm_alloc(&a, sizeof(*app));
    *app = (struct tm_application_o) {
        .allocator = a,
    };

    // Setup DXC Shader compiler
    tm_dxc_shader_compiler_api->init();

    // Create default window and initialize swap chain.
    setup_initial_window(app, (void*)0);

    return app;
}

static void
destroy_application(struct tm_application_o *app)
{
    tm_dxc_shader_compiler_api->shutdown();

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
    tm_allocator_api           = reg->get(TM_ALLOCATOR_API_NAME);
    tm_localizer_api           = reg->get(TM_LOCALIZER_API_NAME);
    tm_logger_api              = reg->get(TM_LOGGER_API_NAME);
    tm_os_api                  = reg->get(TM_OS_API_NAME);
    tm_path_api                = reg->get(TM_PATH_API_NAME);
    tm_plugins_api             = reg->get(TM_PLUGINS_API_NAME);
    tm_temp_allocator_api      = reg->get(TM_TEMP_ALLOCATOR_API_NAME);

    // other plugin apis
    tm_dxc_shader_compiler_api = reg->get(TM_DXC_SHADER_COMPILER_API_NAME);
    tm_os_window_api           = reg->get(TM_OS_WINDOW_API_NAME);

    tm_set_or_remove_api(reg, load, TM_APPLICATION_API_NAME, tm_application_api);
}
