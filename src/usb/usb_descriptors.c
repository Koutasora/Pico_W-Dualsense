// USB descriptors: CDC (log) + RNDIS (panel network) + Raspberry Pi reset interface.
// The Microsoft OS 2.0 descriptor makes Windows load its built-in RNDIS driver
// for the network part and WinUSB for the reset interface.

#include <string.h>
#include "tusb.h"
#include "class/net/net_device.h"
#include "pico/unique_id.h"
#include "pico/usb_reset_interface.h"

// The network function must come first: Windows' RNDIS driver expects
// interfaces 0 and 1 in a composite device.
enum {
    ITF_NCM = 0,
    ITF_NCM_DATA,
    ITF_CDC,
    ITF_CDC_DATA,
    ITF_RESET,
    ITF_TOTAL
};

enum {
    STR_LANG = 0,
    STR_MANUF,
    STR_PRODUCT,
    STR_SERIAL,
    STR_CDC,
    STR_NCM,
    STR_MAC,
    STR_RESET,
    STR_COUNT
};

#define EP_NCM_NOTIF 0x81
#define EP_NCM_OUT   0x02
#define EP_NCM_IN    0x82
#define EP_CDC_NOTIF 0x83
#define EP_CDC_OUT   0x04
#define EP_CDC_IN    0x84

#define RESET_DESC_LEN 9
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_RNDIS_DESC_LEN + RESET_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,                 // 2.1: required for the BOS descriptor
    .bDeviceClass = TUSB_CLASS_MISC,  // composite device with IADs
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x2E8A,               // Raspberry Pi
    .idProduct = 0x0009,              // Pico SDK CDC (RP2350)
    .bcdDevice = 0x0206,           // differs from the stock firmware so Windows re-reads the descriptors
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

static const uint8_t desc_config[CONFIG_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_LEN, 0x80, 250),
    TUD_RNDIS_DESCRIPTOR(ITF_NCM, STR_NCM, EP_NCM_NOTIF, 8, EP_NCM_OUT, EP_NCM_IN, CFG_TUD_NET_ENDPOINT_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    // Reset interface (no endpoints), handled by the driver in usb_panel.c
    9, TUSB_DESC_INTERFACE, ITF_RESET, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
    RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, STR_RESET,
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config;
}

// ---- BOS + Microsoft OS 2.0 descriptor set ----
#define MS_OS_20_VENDOR_CODE 1
#define MS_OS_20_LEN (10 + 8 + 28 + 156)
#define BOS_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_LEN, MS_OS_20_VENDOR_CODE),
};

const uint8_t *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

static const uint8_t desc_ms_os_20[MS_OS_20_LEN] = {
    // Set header
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_LEN),
    // Configuration subset
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_LEN - 10),

    // Function subset: network -> RNDIS (compatible ID RNDIS, sub ID 5162001)
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NCM, 0, U16_TO_U8S_LE(28),
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'R', 'N', 'D', 'I', 'S', 0x00, 0x00, 0x00,
    '5', '1', '6', '2', '0', '0', '1', 0x00,

    // Function subset: reset interface -> WinUSB (same GUID as the Pico SDK uses, so picotool finds it)
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_RESET, 0, U16_TO_U8S_LE(156),
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0,
    U16_TO_U8S_LE(0x0080), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0001), U16_TO_U8S_LE(0x0028),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0,
    'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 0, 0,
    U16_TO_U8S_LE(0x004E),
    '{', 0, 'b', 0, 'c', 0, '7', 0, '3', 0, '9', 0, '8', 0, 'c', 0, '1', 0, '-', 0,
    '7', 0, '3', 0, 'c', 0, 'd', 0, '-', 0, '4', 0, 'c', 0, 'b', 0, '7', 0, '-', 0,
    '9', 0, '8', 0, 'b', 0, '8', 0, '-', 0, '9', 0, '1', 0, '3', 0, 'a', 0, '8', 0,
    'f', 0, 'c', 0, 'a', 0, '7', 0, 'b', 0, 'f', 0, '6', 0, '}', 0, 0, 0,
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_LEN, "MS OS 2.0 descriptor size");

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == MS_OS_20_VENDOR_CODE && request->wIndex == 7) {
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, MS_OS_20_LEN);
    }
    return false;
}

// ---- Strings ----
extern uint8_t tud_network_mac_address[6];

static const char *const strings[STR_COUNT] = {
    [STR_MANUF] = "Raspberry Pi",
    [STR_PRODUCT] = "DualSense Panel",
    [STR_CDC] = "DualSense Panel log",
    [STR_NCM] = "DualSense Panel network",
    [STR_RESET] = "Reset",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[40];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    char mac[13];
    const char *s;
    unsigned n = 0;

    if (index == STR_LANG) {
        desc[1] = 0x0409;
        n = 1;
    } else {
        if (index == STR_SERIAL) {
            if (!serial[0]) pico_get_unique_board_id_string(serial, sizeof(serial));
            s = serial;
        } else if (index == STR_MAC) {
            for (unsigned i = 0; i < 6; i++) {
                mac[i * 2] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 4) & 0xf];
                mac[i * 2 + 1] = "0123456789ABCDEF"[tud_network_mac_address[i] & 0xf];
            }
            mac[12] = 0;
            s = mac;
        } else if (index < STR_COUNT && strings[index]) {
            s = strings[index];
        } else {
            return NULL;
        }
        while (s[n] && n < 39) {
            desc[1 + n] = (uint8_t)s[n];
            n++;
        }
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc;
}
