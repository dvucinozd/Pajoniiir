#include "control_link.h"

#include <string.h>

int control_link_stub_led_count;
led_id_t control_link_stub_led[128];
uint8_t control_link_stub_state[128];
uint8_t control_link_stub_deck[128];

void control_link_stub_reset_leds(void)
{
    control_link_stub_led_count = 0;
    memset(control_link_stub_led, 0, sizeof(control_link_stub_led));
    memset(control_link_stub_state, 0, sizeof(control_link_stub_state));
    memset(control_link_stub_deck, 0, sizeof(control_link_stub_deck));
}

int control_link_stub_last_led_state(led_id_t led, uint8_t deck)
{
    for (int i = control_link_stub_led_count - 1; i >= 0; i--) {
        if (control_link_stub_led[i] == led &&
            control_link_stub_deck[i] == deck) {
            return control_link_stub_state[i];
        }
    }
    return -1;
}

void control_link_send_led(led_id_t led, uint8_t state)
{
    (void)led;
    (void)state;
}

void control_link_send_led_deck(led_id_t led, uint8_t state, uint8_t deck)
{
    if (control_link_stub_led_count < (int)(sizeof(control_link_stub_led) / sizeof(control_link_stub_led[0]))) {
        control_link_stub_led[control_link_stub_led_count] = led;
        control_link_stub_state[control_link_stub_led_count] = state;
        control_link_stub_deck[control_link_stub_led_count] = deck;
        control_link_stub_led_count++;
    }
}
