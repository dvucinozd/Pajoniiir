#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_overview_scheduler.h"

static void test_init_sets_empty_budget_and_default_order(void)
{
    ui_overview_scheduler_t scheduler = {
        .main_redraw_budget = 99,
        .deck_order_flip = true,
    };
    uint8_t first = 0xFF;
    uint8_t second = 0xFF;

    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);

    assert(scheduler.main_redraw_budget == 0);
    assert(first == 1);
    assert(second == 2);
}

static void test_single_redraw_budget_allows_exactly_one_consume(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_begin_tick(&scheduler, 1);

    assert(ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(!ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(scheduler.main_redraw_budget == 0);
}

static void test_zero_redraw_budget_allows_no_consume(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_begin_tick(&scheduler, 0);

    assert(!ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(scheduler.main_redraw_budget == 0);
}

static void test_two_redraw_budget_allows_exactly_two_consumes(void)
{
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);
    ui_overview_scheduler_begin_tick(&scheduler, 2);

    assert(ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(!ui_overview_scheduler_try_consume_main_redraw(&scheduler));
    assert(scheduler.main_redraw_budget == 0);
}

static void test_deck_order_alternates_each_call(void)
{
    ui_overview_scheduler_t scheduler;
    uint8_t first = 0;
    uint8_t second = 0;
    ui_overview_scheduler_init(&scheduler);

    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);
    assert(first == 1);
    assert(second == 2);

    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);
    assert(first == 2);
    assert(second == 1);

    ui_overview_scheduler_next_deck_order(&scheduler, 1, 2, &first, &second);
    assert(first == 1);
    assert(second == 2);
}

static void test_null_arguments_are_safe(void)
{
    uint8_t first = 9;
    uint8_t second = 9;
    ui_overview_scheduler_t scheduler;
    ui_overview_scheduler_init(&scheduler);

    ui_overview_scheduler_init(NULL);
    ui_overview_scheduler_begin_tick(NULL, 1);
    assert(!ui_overview_scheduler_try_consume_main_redraw(NULL));

    ui_overview_scheduler_next_deck_order(NULL, 1, 2, &first, &second);
    assert(first == 1);
    assert(second == 2);

    ui_overview_scheduler_next_deck_order(&scheduler, 3, 4, NULL, &second);
    assert(second == 4);

    ui_overview_scheduler_next_deck_order(&scheduler, 5, 6, &first, NULL);
    assert(first == 6);
}

int main(void)
{
    test_init_sets_empty_budget_and_default_order();
    test_single_redraw_budget_allows_exactly_one_consume();
    test_zero_redraw_budget_allows_no_consume();
    test_two_redraw_budget_allows_exactly_two_consumes();
    test_deck_order_alternates_each_call();
    test_null_arguments_are_safe();

    puts("ui_overview_scheduler tests passed");
    return 0;
}
