#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// USB device: serial log (CDC) + network adapter for the panel (NCM)
// + Raspberry Pi reset interface (picotool reboot into BOOTSEL).

#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)
#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             1
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  2048
#define CFG_TUD_CDC_EP_BUFSIZE  64

// Network adapter: RNDIS (Windows loads its built-in driver for it).
// NCM was tried first, but the Windows NCM driver refused to start (Code 10).
#define CFG_TUD_ECM_RNDIS       1
#define CFG_TUD_NCM             0

#define CFG_TUD_VENDOR          0

#endif
