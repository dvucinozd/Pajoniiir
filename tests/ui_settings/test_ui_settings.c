#include <assert.h>
#include <stdio.h>
#include "ui_settings.h"

static void test_force_poll_always_allows_refresh(void)
{
    assert(ui_settings_should_poll(1000, 999, true, 1000));
}

static void test_first_poll_and_interval_gate(void)
{
    assert(ui_settings_should_poll(1000, 0, false, 1000));
    assert(!ui_settings_should_poll(1500, 1000, false, 1000));
    assert(ui_settings_should_poll(2000, 1000, false, 1000));
}

int main(void)
{
    test_force_poll_always_allows_refresh();
    test_first_poll_and_interval_gate();

    puts("ui_settings tests passed");
    return 0;
}
