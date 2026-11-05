#pragma once

// Panel settings. Change and rebuild.
#define PANEL_FW_VERSION "1.0.0-panel"

// Wi-Fi access point started when no PC is connected over USB.
#define PANEL_AP_SSID     "DualSense-Pico"
#define PANEL_AP_PASSWORD "dualsense"   // WPA2, at least 8 characters
#define PANEL_AP_IP       192, 168, 20, 1
#define PANEL_AP_IP_STR   "192.168.20.1"

// Uncomment to dump every Bluetooth HCI packet to the serial log (debugging).
// #define PANEL_BT_DEBUG

// The panel is reachable for this long after start or after the last use,
// separately on Wi-Fi and on USB. Then Wi-Fi turns off (radio silent) and the
// USB network stops answering. Power-cycle / replug to open it again.
#define PANEL_WINDOW_MS   (5 * 60 * 1000)

// Live view: state snapshot / push interval for the panel (ms).
#define PANEL_STREAM_MS   20

// On battery, Wi-Fi starts after this long without a PC on USB.
#define PANEL_NO_USB_MS   4000
