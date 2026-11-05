#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"
#include "class/net/net_device.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/cyw43_arch.h"
#include "pico/async_context.h"
#include "pico/bootrom.h"
#include "pico/usb_reset_interface.h"
#include "hardware/watchdog.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#include "panel_config.h"
#include "usb_panel.h"

// Locally administered MAC for the USB network (host side); Pico uses the same with the last bit flipped.
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

static async_context_t *ctx;
static async_at_time_worker_t worker;
static struct netif usb_netif;
static struct pbuf *rx_frame;
static volatile bool net_allowed = true;
static bool started = false;

// ---------------------------------------------------------------------------
// Reset interface (same protocol as the Pico SDK, used by "picotool reboot")
// ---------------------------------------------------------------------------
static uint8_t reset_itf;

static void resetd_init(void) {}
static void resetd_reset(uint8_t rhport) { (void)rhport; reset_itf = 0; }

static uint16_t resetd_open(uint8_t rhport, tusb_desc_interface_t const *d, uint16_t max_len) {
    (void)rhport;
    TU_VERIFY(d->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC &&
              d->bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
              d->bInterfaceProtocol == RESET_INTERFACE_PROTOCOL, 0);
    TU_VERIFY(max_len >= sizeof(tusb_desc_interface_t), 0);
    reset_itf = d->bInterfaceNumber;
    return sizeof(tusb_desc_interface_t);
}

static bool resetd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *r) {
    (void)rhport;
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (r->wIndex != reset_itf) return false;
    if (r->bRequest == RESET_REQUEST_BOOTSEL) {
        int gpio = (r->wValue & 0x100) ? (r->wValue >> 9u) : -1;
        bool active_low = r->wValue & 0x200;
        rom_reset_usb_boot_extra(gpio, r->wValue & 0x7f, active_low);   // does not return
    }
    if (r->bRequest == RESET_REQUEST_FLASH) {
        watchdog_reboot(0, 0, 100);
        return true;
    }
    return false;
}

static bool resetd_xfer_cb(uint8_t rhport, uint8_t ep, xfer_result_t res, uint32_t n) {
    (void)rhport; (void)ep; (void)res; (void)n;
    return true;
}

static const usbd_class_driver_t app_drivers[] = {
    {
        .init = resetd_init,
        .reset = resetd_reset,
        .open = resetd_open,
        .control_xfer_cb = resetd_control_xfer_cb,
        .xfer_cb = resetd_xfer_cb,
        .sof = NULL,
    },
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = (uint8_t)(sizeof(app_drivers) / sizeof(app_drivers[0]));
    return app_drivers;
}

// ---------------------------------------------------------------------------
// stdio over CDC (printf / getchar). Every call takes the async context lock,
// so TinyUSB is never used from two places at once.
// ---------------------------------------------------------------------------
static void cdc_out_chars(const char *buf, int len) {
    if (!started) return;
    async_context_acquire_lock_blocking(ctx);
    if (tud_cdc_connected()) {
        int idle = 0;
        while (len > 0) {
            uint32_t n = tud_cdc_write(buf, (uint32_t)len);
            buf += n;
            len -= (int)n;
            if (len > 0) {
                tud_task();
                tud_cdc_write_flush();
                if (n == 0 && ++idle > 500) break;   // host not reading: drop the rest
            }
        }
        tud_cdc_write_flush();
    }
    async_context_release_lock(ctx);
}

static int cdc_in_chars(char *buf, int len) {
    if (!started) return PICO_ERROR_NO_DATA;
    int rc = PICO_ERROR_NO_DATA;
    async_context_acquire_lock_blocking(ctx);
    if (tud_cdc_available()) {
        int n = (int)tud_cdc_read(buf, (uint32_t)len);
        if (n > 0) rc = n;
    }
    async_context_release_lock(ctx);
    return rc;
}

static stdio_driver_t cdc_driver = {
    .out_chars = cdc_out_chars,
    .in_chars = cdc_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

// ---------------------------------------------------------------------------
// RNDIS network <-> lwIP
// ---------------------------------------------------------------------------
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (rx_frame) return false;   // previous frame not processed yet
    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) {
            pbuf_take(p, src, size);
            rx_frame = p;
        }
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    (void)arg;
    struct pbuf *p = (struct pbuf *)ref;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void) {
    if (rx_frame) {
        pbuf_free(rx_frame);
        rx_frame = NULL;
    }
}

static err_t usb_linkoutput(struct netif *n, struct pbuf *p) {
    (void)n;
    for (int i = 0; i < 1000; i++) {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        tud_task();   // let the previous frame go out
    }
    return ERR_MEM;
}

static err_t usb_netif_init(struct netif *n) {
    n->mtu = 1500;
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    n->name[0] = 'u';
    n->name[1] = 's';
    n->linkoutput = usb_linkoutput;
    n->output = etharp_output;
    return ERR_OK;
}

static void usb_work(async_context_t *c, async_at_time_worker_t *w) {
    tud_task();

    if (rx_frame) {
        struct pbuf *p = rx_frame;
        rx_frame = NULL;
        if (net_allowed) {
            if (usb_netif.input(p, &usb_netif) != ERR_OK) pbuf_free(p);
        } else {
            pbuf_free(p);   // panel time window is over: ignore traffic
        }
        tud_network_recv_renew();
    }

    // The USB network link is up only while a PC is connected and the panel is allowed.
    bool up = tud_mounted() && net_allowed;
    if (up != netif_is_link_up(&usb_netif)) {
        if (up) netif_set_link_up(&usb_netif);
        else netif_set_link_down(&usb_netif);
    }

    async_context_add_at_time_worker_in_ms(c, w, 1);
}

#define USB_IP4_ADDR(dst, ...) IP4_ADDR(dst, __VA_ARGS__)

void usb_panel_init(void) {
    ctx = cyw43_arch_async_context();
    tud_init(0);

    async_context_acquire_lock_blocking(ctx);
    ip4_addr_t ip, mask, gw;
    USB_IP4_ADDR(&ip, PANEL_AP_IP);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);
    usb_netif.hwaddr_len = 6;
    memcpy(usb_netif.hwaddr, tud_network_mac_address, 6);
    usb_netif.hwaddr[5] ^= 0x01;
    netif_add(&usb_netif, &ip, &mask, &gw, NULL, usb_netif_init, ethernet_input);
    netif_set_up(&usb_netif);
    started = true;
    async_context_release_lock(ctx);

    stdio_set_driver_enabled(&cdc_driver, true);

    worker.do_work = usb_work;
    async_context_add_at_time_worker_in_ms(ctx, &worker, 1);
}

void usb_panel_net_allow(bool allow) {
    net_allowed = allow;
}
