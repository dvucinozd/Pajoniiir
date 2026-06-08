#pragma once

#include "esp_err.h"
#include "panel_io.h"

// Initialise TinyUSB MIDI device and start the MIDI TX/RX task.
// panel_event_queue: the queue returned by panel_io_init().
// LED commands received from the USB host are applied to panel LEDs directly.
esp_err_t midi_compat_init(QueueHandle_t panel_event_queue);

// Translate one panel event to the corresponding XDJ100SX MIDI message(s)
// and send them via USB MIDI. Safe to call from any task.
void midi_compat_process_event(const panel_event_t *ev);
