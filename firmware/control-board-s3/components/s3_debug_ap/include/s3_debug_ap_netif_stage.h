#pragma once

#include "esp_err.h"

typedef void *(*s3_debug_ap_netif_create_fn)(void *ctx);
typedef esp_err_t (*s3_debug_ap_netif_step_fn)(void *resource, void *ctx);
typedef void (*s3_debug_ap_netif_destroy_fn)(void *resource, void *ctx);

typedef struct {
    s3_debug_ap_netif_create_fn create;
    s3_debug_ap_netif_step_fn stop_dhcp;
    s3_debug_ap_netif_step_fn set_ip;
    s3_debug_ap_netif_step_fn start_dhcp;
    s3_debug_ap_netif_destroy_fn destroy;
    void *ctx;
} s3_debug_ap_netif_ops_t;

esp_err_t s3_debug_ap_netif_ensure(void **published,
                                   const s3_debug_ap_netif_ops_t *ops);
