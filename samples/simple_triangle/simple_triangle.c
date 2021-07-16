#define USE_D3D11_BACKEND

struct tm_api_registry_api *tm_global_api_registry;

struct tm_allocator_api *tm_allocator_api;
struct tm_error_api *tm_error_api;
struct tm_localizer_api *tm_localizer_api;
struct tm_logger_api *tm_logger_api;
struct tm_os_api *tm_os_api;
struct tm_path_api *tm_path_api;
struct tm_plugins_api *tm_plugins_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;

struct tm_os_window_api *tm_os_window_api;
struct tm_dxc_shader_compiler_api *tm_dxc_shader_compiler_api;
struct tm_renderer_init_api *tm_renderer_init_api;
#if defined(USE_D3D11_BACKEND)
struct tm_d3d11_api *tm_d3d11_api;
#else
struct tm_vulkan_api *tm_vulkan_api;
#endif

struct tm_renderer_command_buffer_api *tm_cmd_buf_api;
struct tm_renderer_resource_command_buffer_api *tm_res_buf_api;


#include "simple_triangle.h"

#include <foundation/api_registry.h>
#include <foundation/allocator.h>
#include <foundation/application.h>
#include <foundation/carray.inl>
#include <foundation/error.h>
#include <foundation/localizer.h>
#include <foundation/log.h>
#include <foundation/os.h>
#include <foundation/path.h>
#include <foundation/plugin.h>
#include <foundation/string.inl>
#include <foundation/temp_allocator.h>

#include <plugins/dxc_shader_compiler/dxc_compiler.h>
#include <plugins/os_window/os_window.h>
#include <plugins/renderer/nil_render_backend.h>
#include <plugins/renderer/render_backend.h>
// #include <plugins/renderer/render_command_buffer.h>
#include <plugins/renderer/renderer.h>

#if defined(USE_D3D11_BACKEND)
#include <plugins/d3d11_render_backend/d3d11_render_backend.h>
#else
#include <plugins/vulkan_render_backend/vulkan_render_backend.h>
#endif

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

#if defined(USE_D3D11_BACKEND)
    struct tm_d3d11_backend_i *d3d11_backend;
#else
    struct tm_vulkan_backend_i *vulkan_backend;
#endif
    struct tm_renderer_backend_i *render_backend;
    uint32_t device_affinity;
    TM_PAD(4);

    struct window_t window;
};


static void
init_renderer_plugin(struct tm_allocator_i *allocator)
{
    const uint64_t user_data_size = 8 * 1024;
    tm_renderer_init_api->init(allocator, user_data_size);

    struct tm_renderer_api *tm_renderer_api = tm_global_api_registry->get(TM_RENDERER_API_NAME);
    tm_cmd_buf_api = tm_renderer_api->tm_renderer_command_buffer_api;
    tm_res_buf_api = tm_renderer_api->tm_renderer_resource_command_buffer_api;
}

static void
shutdown_renderer_plugin(void)
{
    tm_renderer_init_api->shutdown();
}

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

#if defined(USE_D3D11_BACKEND)

static void
setup_render_backend(struct tm_application_o *app, bool vulkan_validation_layer)
{
    if (!tm_d3d11_api->create_backend)
    {
        struct tm_nil_renderer_backend_api *tm_nil_renderer_backend_api = tm_global_api_registry->get(TM_NIL_RENDER_BACKEND_API_NAME);
        app->render_backend = tm_nil_renderer_backend_api->create(&app->allocator);
        return;
    }

    app->d3d11_backend = tm_d3d11_api->create_backend(&app->allocator, tm_error_api->def);

    app->d3d11_backend->init(app->d3d11_backend->inst);

    app->render_backend = app->d3d11_backend->agnostic_render_backend(app->d3d11_backend->inst);

    // Expose abstract render backend interfaces to the API registry.
    tm_add_or_remove_implementation(tm_global_api_registry, true, TM_RENDER_BACKEND_INTERFACE_NAME, app->render_backend);
}

static void
shutdown_render_backend(struct tm_application_o *app)
{
    if (!app->d3d11_backend)
    {
        struct tm_nil_renderer_backend_api *tm_nil_renderer_backend_api = tm_global_api_registry->get(TM_NIL_RENDER_BACKEND_API_NAME);
        tm_nil_renderer_backend_api->destroy(app->render_backend);
        return;
    }

    //app->d3d11_backend->destroy_devices(app->d3d11_backend->inst, &app->device_affinity, 1);
    app->d3d11_backend->shutdown(app->d3d11_backend->inst);
    tm_add_or_remove_implementation(tm_global_api_registry, false, TM_RENDER_BACKEND_INTERFACE_NAME, app->render_backend);
    tm_d3d11_api->destroy_backend(app->d3d11_backend);
}

#else

static void
setup_render_backend(struct tm_application_o *app, bool vulkan_validation_layer)
{
    if (!tm_vulkan_api->create_backend)
    {
        struct tm_nil_renderer_backend_api *tm_nil_renderer_backend_api = tm_global_api_registry->get(TM_NIL_RENDER_BACKEND_API_NAME);
        app->render_backend = tm_nil_renderer_backend_api->create(&app->allocator);
        return;
    }

    // Creates Vulkan backend, sets up host memory allocator and enumerates available vulkan instance layers and externsions.
    app->vulkan_backend = tm_vulkan_api->create_backend(&app->allocator, tm_error_api->def);

    // Creates the vulkan instance. Request swap chain support and optionally enable "VK_LAYER_LUNARG_standard_validation"
    app->vulkan_backend->init(app->vulkan_backend->inst, TM_VULKAN_INIT_FLAG_SWAPCHAIN | (vulkan_validation_layer ? TM_VULKAN_INIT_FLAG_VALIDATION : 0));

    {
        // Setup a single vulkan device. Prioritize to run it on a discrete GPU if available, else fallback to integrated GPU.
        tm_vulkan_device_id wanted_device = { 0 };

        uint32_t flags = TM_VULKAN_DEVICE_FLAG_DISCRETE;
        uint32_t num_devices = app->vulkan_backend->num_physical_devices(app->vulkan_backend->inst, flags);
        if (num_devices == 0)
        {
            flags = TM_VULKAN_DEVICE_FLAG_INTEGRATED;
            num_devices = app->vulkan_backend->num_physical_devices(app->vulkan_backend->inst, flags);
        }
        num_devices = tm_min(num_devices, 1);

        app->vulkan_backend->physical_device_id(app->vulkan_backend->inst, 0, flags, &wanted_device);

        app->vulkan_backend->create_devices(app->vulkan_backend->inst, &wanted_device, 1, &app->device_affinity, 0);
        app->render_backend = app->vulkan_backend->agnostic_render_backend(app->vulkan_backend->inst);
    }

    // Expose abstract render backend interfaces to the API registry.
    tm_add_or_remove_implementation(tm_global_api_registry, true, TM_RENDER_BACKEND_INTERFACE_NAME, app->render_backend);
}

static void
shutdown_render_backend(struct tm_application_o *app)
{
    if (!app->vulkan_backend)
    {
        struct tm_nil_renderer_backend_api *tm_nil_renderer_backend_api = tm_global_api_registry->get(TM_NIL_RENDER_BACKEND_API_NAME);
        tm_nil_renderer_backend_api->destroy(app->render_backend);
        return;
    }

    app->vulkan_backend->destroy_devices(app->vulkan_backend->inst, &app->device_affinity, 1);
    app->vulkan_backend->shutdown(app->vulkan_backend->inst);
    tm_add_or_remove_implementation(tm_global_api_registry, false, TM_RENDER_BACKEND_INTERFACE_NAME, app->render_backend);
    tm_vulkan_api->destroy_backend(app->vulkan_backend);
}

#endif

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

    // Initialize the render plugin, setup APIs
    init_renderer_plugin(&app->allocator);

    // Initialize and setup the truth
    // TODO

    // Setup render backend and create device
    const bool vulkan_validation = false;
    setup_render_backend(app, vulkan_validation);

    // Setup DXC Shader compiler
    tm_dxc_shader_compiler_api->init();

    // Load shaders
#if 0
    struct tm_renderer_resource_command_buffer_o *res_buf = 0;
    struct tm_renderer_backend_i *rb = app->render_backend;
    rb->create_resource_command_buffers(rb->inst, &res_buf, 1);
#endif

    // Create default window and initialize swap chain.
    setup_initial_window(app, (void*)0);

    return app;
}

static void
destroy_application(struct tm_application_o *app)
{
    tm_dxc_shader_compiler_api->shutdown();

    shutdown_render_backend(app);
    shutdown_renderer_plugin();

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
    tm_error_api               = reg->get(TM_ERROR_API_NAME);
    tm_localizer_api           = reg->get(TM_LOCALIZER_API_NAME);
    tm_logger_api              = reg->get(TM_LOGGER_API_NAME);
    tm_os_api                  = reg->get(TM_OS_API_NAME);
    tm_path_api                = reg->get(TM_PATH_API_NAME);
    tm_plugins_api             = reg->get(TM_PLUGINS_API_NAME);
    tm_temp_allocator_api      = reg->get(TM_TEMP_ALLOCATOR_API_NAME);

    // other plugin apis
    tm_dxc_shader_compiler_api = reg->get(TM_DXC_SHADER_COMPILER_API_NAME);
    tm_os_window_api           = reg->get(TM_OS_WINDOW_API_NAME);
    tm_renderer_init_api       = reg->get(TM_RENDERER_INIT_API_NAME);
#if defined(USE_D3D11_BACKEND)
    tm_d3d11_api               = reg->get(TM_D3D11_API_NAME);
#else
    tm_vulkan_api              = reg->get(TM_VULKAN_API_NAME);
#endif

    tm_set_or_remove_api(reg, load, TM_APPLICATION_API_NAME, tm_application_api);
}
