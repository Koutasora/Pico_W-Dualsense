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

// Auto mode: Wi-Fi turns on after this long without a USB host,
// and off after the USB host has been present for PANEL_USB_ON_MS.
#define PANEL_NO_USB_MS   4000
#define PANEL_USB_ON_MS   1000
