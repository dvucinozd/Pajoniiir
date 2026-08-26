#include "s3_debug_ap_netif_stage.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    int fail_step;
    int calls[5];
    int resource;
} fixture_t;

static void *fake_create(void *ctx)
{
    fixture_t *f = ctx;
    f->calls[0]++;
    return f->fail_step == 0 ? NULL : &f->resource;
}

static esp_err_t fake_step(void *resource, void *ctx, int step)
{
    fixture_t *f = ctx;
    assert(resource == &f->resource);
    f->calls[step]++;
    return f->fail_step == step ? ESP_FAIL : ESP_OK;
}

static esp_err_t fake_stop(void *r, void *ctx) { return fake_step(r, ctx, 1); }
static esp_err_t fake_set(void *r, void *ctx) { return fake_step(r, ctx, 2); }
static esp_err_t fake_start(void *r, void *ctx) { return fake_step(r, ctx, 3); }

static void fake_destroy(void *resource, void *ctx)
{
    fixture_t *f = ctx;
    assert(resource == &f->resource);
    f->calls[4]++;
}

static s3_debug_ap_netif_ops_t ops_for(fixture_t *fixture)
{
    return (s3_debug_ap_netif_ops_t){
        .create = fake_create,
        .stop_dhcp = fake_stop,
        .set_ip = fake_set,
        .start_dhcp = fake_start,
        .destroy = fake_destroy,
        .ctx = fixture,
    };
}

int main(void)
{
    for (int fail_step = 0; fail_step <= 3; fail_step++) {
        fixture_t fixture = { .fail_step = fail_step };
        s3_debug_ap_netif_ops_t ops = ops_for(&fixture);
        void *published = NULL;
        assert(s3_debug_ap_netif_ensure(&published, &ops) != ESP_OK);
        assert(published == NULL);
        if (fail_step != 0) assert(fixture.calls[4] == 1);

        fixture.fail_step = -1;
        assert(s3_debug_ap_netif_ensure(&published, &ops) == ESP_OK);
        assert(published == &fixture.resource);
        assert(s3_debug_ap_netif_ensure(&published, &ops) == ESP_OK);
    }
    puts("s3_debug_ap_netif_stage tests passed");
    return 0;
}
