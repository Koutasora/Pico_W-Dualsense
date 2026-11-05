#pragma once
// USB for the panel: serial log (stdio over CDC), network adapter (NCM) that
// serves the panel page, and the picotool reset interface. All TinyUSB work
// runs in the cyw43 async context, together with Bluetooth and lwIP.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call once, right after cyw43_arch_init().
void usb_panel_init(void);

// Allow or block the panel network on USB (the serial log is not affected).
void usb_panel_net_allow(bool allow);

#ifdef __cplusplus
}
#endif
