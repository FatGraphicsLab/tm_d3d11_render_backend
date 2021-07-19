#pragma once

#include <foundation/api_types.h>

extern struct tm_api_registry_api *tm_global_api_registry;

extern struct tm_allocator_api *tm_allocator_api;
extern struct tm_error_api *tm_error_api;
extern struct tm_localizer_api *tm_localizer_api;
extern struct tm_logger_api *tm_logger_api;
extern struct tm_os_api *tm_os_api;
extern struct tm_path_api *tm_path_api;
extern struct tm_plugins_api *tm_plugins_api;
extern struct tm_temp_allocator_api *tm_temp_allocator_api;
extern struct tm_the_truth_api *tm_the_truth_api;

extern struct tm_os_window_api *tm_os_window_api;
extern struct tm_dxc_shader_compiler_api *tm_dxc_shader_compiler_api;
extern struct tm_renderer_init_api *tm_renderer_init_api;
extern struct tm_shader_api *tm_shader_api;
extern struct tm_shader_declaration_api *tm_shader_declaration_api;
extern struct tm_shader_repository_api *tm_shader_repository_api;
extern struct tm_shader_system_api *tm_shader_system_api;

#if defined(USE_D3D11_BACKEND)
extern struct tm_d3d11_api *tm_d3d11_api;
#else
extern struct tm_vulkan_api *tm_vulkan_api;
#endif

extern struct tm_renderer_command_buffer_api *tm_cmd_buf_api;
extern struct tm_renderer_resource_command_buffer_api *tm_res_buf_api;
