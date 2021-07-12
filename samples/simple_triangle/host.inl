
#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/job_system.h>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/memory_tracker.h>
#include <foundation/os.h>
#include <foundation/plugin.h>
#include <foundation/task_system.h>
#include <foundation/temp_allocator.h>
#include <foundation/unicode.h>

#include <string.h>

#ifdef TM_LINKS_HOST

typedef struct run_application_t
{
    struct tm_application_api *tm_application_api;
    int argc;
    TM_PAD(4);
    char **argv;
} run_application_t;

static void run_application(const run_application_t *data)
{
    struct tm_application_api *api = data->tm_application_api;
    tm_application_o *app = api->create(data->argc, data->argv);
    if (!app)
        return;

    while (api->tick(app))
        tm_plugins_api->check_hot_reload();
    api->destroy(app);
}

#ifdef TM_OS_WINDOWS
#include <windows.h>
// Do not remove this comment. If you do, clang-format will sort the includes and we may run into compilation errors.
#include <ConsoleApi.h>
#include <stdio.h>

bool attached = false;

static void attach_console(void)
{
    // Try to detect if we're running under mingw, in that case we want to use stdout and not
    // AttachConsole(), otherwise we won't get any output.
    const char *msystem = getenv("MSYSTEM");
    if (!msystem) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CON", "w", stdout);
            freopen("CON", "w", stderr);
            attached = true;
        }
    }
}

// Solution from: https://www.tillett.info/2013/05/13/how-to-create-a-windows-program-that-works-as-both-as-a-gui-and-console-application/
static void send_enter_key(void)
{
    INPUT ip;
    // Set up a generic keyboard event.
    ip.type = INPUT_KEYBOARD;
    ip.ki.wScan = 0; // hardware scan code for key
    ip.ki.time = 0;
    ip.ki.dwExtraInfo = 0;

    // Send the "Enter" key
    ip.ki.wVk = 0x0D; // virtual-key code for the "Enter" key
    ip.ki.dwFlags = 0; // 0 for key press
    SendInput(1, &ip, sizeof(INPUT));

    // Release the "Enter" key
    ip.ki.dwFlags = KEYEVENTF_KEYUP; // KEYEVENTF_KEYUP for key release
    SendInput(1, &ip, sizeof(INPUT));
}

static void free_console(void)
{
    if (attached && GetConsoleWindow() == GetForegroundWindow())
        send_enter_key();
}
#else
static void attach_console(void)
{
}
static void free_console(void)
{
}
#endif

int run(int argc, char *argv[])
{
    attach_console();

    // Check for --hot-reload flag.
    bool hot_reload_plugins = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--hot-reload"))
            hot_reload_plugins = true;
    }

    tm_allocator_i allocator = tm_allocator_api->create_child(tm_allocator_api->system, "host");
    tm_init_global_api_registry(&allocator);
    tm_register_all_foundation_apis(tm_global_api_registry);

    // Currently we are limiting the number of worker threads in the job system to 8 to avoid the overhead caused by the fiber pinning feature.
    struct tm_job_system_api *job_system = tm_create_job_system(tm_os_api->thread, tm_min(tm_os_api->info->num_logical_processors(), 8), 128, 128 * 1024);
    tm_global_api_registry->set(TM_JOB_SYSTEM_API_NAME, job_system, sizeof(*job_system));

    struct tm_task_system_api *task_system = tm_create_task_system(&allocator, tm_max(tm_os_api->info->num_logical_processors() / 2, 1));
    tm_global_api_registry->set(TM_TASK_SYSTEM_API_NAME, task_system, sizeof(*task_system));

    // Load the main DLL.
    const char *exe_path = tm_os_api->system->exe_path(argv[0]);
    {
        TM_INIT_TEMP_ALLOCATOR(ta);
        tm_plugins_api->load(tm_plugins_api->app_dllpath(ta, exe_path, main_dll), hot_reload_plugins);
        TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
    }

    // Get the application interface
    struct tm_application_api *api = tm_global_api_registry->get(TM_APPLICATION_API_NAME);
    if (api->create) {
        // TODO: OS X requires the event loop to be run from the "main thread". For now, I just run
        //       run_application from the main thread instead of from a fiber. But we should figure out
        //       how to do this in a better way, so we don't need special code for this everywhere.
        run_application_t run = { .tm_application_api = api, .argc = argc, .argv = argv };
        if (TM_IS_DEFINED(TM_NO_MAIN_FIBER))
            run_application(&run);
        else {
            tm_jobdecl_t j = { .task = (void (*)(void *))run_application, .data = &run, .pin_thread_handle = job_system->pin_thread_handle(0) };
            tm_atomic_counter_o *completed = job_system->run_jobs(&j, 1);
            job_system->wait_for_counter_and_free_no_fiber(completed);
        }
    } else
        TM_LOG("Could not find main DLL `%s` next to exe `%s`", main_dll, exe_path);

    tm_destroy_task_system();
    tm_destroy_job_system(job_system);
    tm_shutdown_global_api_registry(&allocator);
    tm_allocator_api->destroy_child(&allocator);

    free_console();

    tm_memory_tracker_api->check_for_leaked_scopes();

    return 0;
}

#ifdef TM_OS_WINDOWS

#include <shellapi.h>

int main(int argc, char *argv[])
{
    TM_INIT_TEMP_ALLOCATOR(ta);
    wchar_t *cmd_line = GetCommandLineW();
    wchar_t **w_argv = CommandLineToArgvW(cmd_line, &argc);
    for (int i = 0; i < argc; ++i)
        argv[i] = (char *)tm_unicode_api->utf16_to_utf8((const uint16_t *)w_argv[i], ta);

    run(argc, argv);
    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}
#else

// TODO: On Unixes, `argv` doesn't have a fixed encoding:
//
// * File names inserted by tabbing or globbing use the exact byte sequence of characters of the
//   file name (which might not be UTF-8 and might not be in the current locale).
//
// * Input entered by the user uses the current locale.
//
// Not sure exactly how we want to handle this, since we assume in many places that all strings
// are UTF-8 and file names on Unix may use a locale that is not even *representable* as UTF-8.
// Might not be a big issue, since we don't depend a lot on command-line parameters anyway, but
// it's something to be aware of.
//
// Reference: https://stackoverflow.com/questions/5408730/what-is-the-encoding-of-argv
int main(int argc, char *argv[])
{
    return run(argc, argv);
}

#endif

#endif
