#include <assert.h>
#include <stdio.h>

#include "ui_performance_target.h"

static void test_defaults_to_deck_1(void)
{
    ui_performance_target_t target;
    ui_performance_target_init(&target);

    assert(ui_performance_target_get(&target) == 0);
    assert(ui_performance_target_is_active(&target, 0));
    assert(!ui_performance_target_is_active(&target, 1));
}

static void test_selects_deck_2_and_rejects_invalid_deck(void)
{
    ui_performance_target_t target;
    ui_performance_target_init(&target);

    ui_performance_target_set(&target, 1);
    assert(ui_performance_target_get(&target) == 1);
    assert(ui_performance_target_is_active(&target, 1));

    ui_performance_target_set(&target, 42);
    assert(ui_performance_target_get(&target) == 1);
}

int main(void)
{
    test_defaults_to_deck_1();
    test_selects_deck_2_and_rejects_invalid_deck();

    puts("ui_performance_target tests passed");
    return 0;
}
