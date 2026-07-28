/*
 * Firmware-only compilation wrapper.
 *
 * flx4_midi_host_refresh_connection_state() needs the USB client handle before
 * the legacy implementation's later tentative definition. A matching static
 * tentative declaration in the same translation unit is valid C and keeps the
 * host-test source path unchanged.
 */
#include "usb/usb_host.h"
static usb_host_client_handle_t s_midi_client_handle;

#include "flx4_midi_host.c"
