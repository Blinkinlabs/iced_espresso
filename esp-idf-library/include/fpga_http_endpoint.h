#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

esp_err_t fpga_http_endpoint_register(httpd_handle_t handle);
