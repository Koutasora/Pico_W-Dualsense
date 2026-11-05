#pragma once
// Wi-Fi access point + tiny HTTP server for the panel.
//   GET /            panel page (same file as panel/panel.html)
//   GET /api/state   JSON state
//   GET /api/cmd?q=  run one panel command (URL-encoded)
// The access point runs only when no PC is connected over USB (auto mode).

#include <cstddef>
#include <cstring>
#include <cstdio>

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "tusb.h"

#include "panel_config.h"
#include "panel_core.h"
#include "panel_html.h"   // generated at build time

extern "C" {
#include "dhcpserver.h"
}

namespace web {

static dhcp_server_t dhcp;
static tcp_pcb* listener = nullptr;
static absolute_time_t usb_changed_at;
static bool usb_last = false;
static absolute_time_t next_check;

constexpr int MAX_CONN = 4;
struct Conn {
    bool used;
    tcp_pcb* pcb;
    char req[600];
    int req_len;
    char head[200];
    int head_len;
    int head_sent;
    const uint8_t* body;     // static body (panel page) or dyn
    size_t body_len;
    size_t body_sent;
    char dyn[1200];
};
static Conn conns[MAX_CONN];

inline void conn_free(Conn* c) {
    if (!c->used) return;
    if (c->pcb) {
        tcp_arg(c->pcb, nullptr);
        tcp_recv(c->pcb, nullptr);
        tcp_sent(c->pcb, nullptr);
        tcp_err(c->pcb, nullptr);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    }
    c->pcb = nullptr;
    c->used = false;
}

// Push as much of the reply as fits in the send buffer. Closes when done.
inline void conn_pump(Conn* c) {
    if (!c->pcb) return;
    while (c->head_sent < c->head_len) {
        u16_t room = tcp_sndbuf(c->pcb);
        if (room == 0) break;
        int n = c->head_len - c->head_sent;
        if (n > room) n = room;
        if (tcp_write(c->pcb, c->head + c->head_sent, n, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        c->head_sent += n;
    }
    if (c->head_sent == c->head_len) {
        while (c->body_sent < c->body_len) {
            u16_t room = tcp_sndbuf(c->pcb);
            if (room == 0 || tcp_sndqueuelen(c->pcb) >= TCP_SND_QUEUELEN - 1) break;
            size_t n = c->body_len - c->body_sent;
            if (n > room) n = room;
            if (n > 1400) n = 1400;
            u8_t flags = (c->body == (const uint8_t*)c->dyn) ? TCP_WRITE_FLAG_COPY : 0;
            if (c->body_sent + n < c->body_len) flags |= TCP_WRITE_FLAG_MORE;
            if (tcp_write(c->pcb, c->body + c->body_sent, n, flags) != ERR_OK) break;
            c->body_sent += n;
        }
    }
    tcp_output(c->pcb);
    if (c->head_sent == c->head_len && c->body_sent == c->body_len) conn_free(c);
}

inline int hexval(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

inline void url_decode(const char* in, char* out, size_t n) {
    size_t o = 0;
    while (*in && *in != ' ' && *in != '&' && o + 1 < n) {
        if (*in == '%' && hexval(in[1]) >= 0 && hexval(in[2]) >= 0) {
            out[o++] = (char)(hexval(in[1]) * 16 + hexval(in[2]));
            in += 3;
        } else {
            out[o++] = (*in == '+') ? ' ' : *in;
            in++;
        }
    }
    out[o] = 0;
}

inline void respond(Conn* c, int code, const char* type, const uint8_t* body, size_t len) {
    const char* status = code == 200 ? "200 OK" : code == 503 ? "503 Busy" : "404 Not Found";
    c->head_len = snprintf(c->head, sizeof(c->head),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, type, (unsigned)len);
    c->head_sent = 0;
    c->body = body;
    c->body_len = len;
    c->body_sent = 0;
    conn_pump(c);
}

inline void handle_request(Conn* c) {
    char path[300] = "";
    if (sscanf(c->req, "GET %299s", path) != 1) {
        return respond(c, 404, "text/plain", (const uint8_t*)"", 0);
    }
    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        return respond(c, 200, "text/html; charset=utf-8", panel_html, panel_html_len);
    }
    if (!strcmp(path, "/api/state")) {
        size_t n = panel::copy_state(c->dyn, sizeof(c->dyn));
        return respond(c, 200, "application/json", (const uint8_t*)c->dyn, n);
    }
    if (!strncmp(path, "/api/cmd?q=", 11)) {
        char cmd[panel::CMD_LEN];
        url_decode(path + 11, cmd, sizeof(cmd));
        bool ok = panel::enqueue(cmd);
        int n = snprintf(c->dyn, sizeof(c->dyn), ok ? "{\"ok\":true,\"queued\":true}" : "{\"ok\":false,\"err\":\"busy\"}");
        return respond(c, ok ? 200 : 503, "application/json", (const uint8_t*)c->dyn, n);
    }
    return respond(c, 404, "text/plain", (const uint8_t*)"not found", 9);
}

inline err_t on_sent(void* arg, tcp_pcb*, u16_t) {
    if (arg) conn_pump(static_cast<Conn*>(arg));
    return ERR_OK;
}

inline void on_err(void* arg, err_t) {
    if (!arg) return;
    Conn* c = static_cast<Conn*>(arg);
    c->pcb = nullptr;   // pcb already freed by lwIP
    c->used = false;
}

inline err_t on_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t) {
    Conn* c = static_cast<Conn*>(arg);
    if (!p) {   // remote closed
        if (c) conn_free(c);
        return ERR_OK;
    }
    bool complete = false;
    if (c && c->head_len == 0) {
        int room = (int)sizeof(c->req) - 1 - c->req_len;
        int n = p->tot_len < room ? p->tot_len : room;
        pbuf_copy_partial(p, c->req + c->req_len, n, 0);
        c->req_len += n;
        c->req[c->req_len] = 0;
        complete = strstr(c->req, "\r\n\r\n") || c->req_len >= (int)sizeof(c->req) - 1;
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    if (complete) handle_request(c);   // may close the connection
    return ERR_OK;
}

inline err_t on_accept(void*, tcp_pcb* pcb, err_t e) {
    if (e != ERR_OK || !pcb) return ERR_VAL;
    Conn* c = nullptr;
    for (auto& x : conns) if (!x.used) { c = &x; break; }
    if (!c) { tcp_abort(pcb); return ERR_ABRT; }
    memset(c, 0, offsetof(Conn, dyn));
    c->used = true;
    c->pcb = pcb;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    return ERR_OK;
}

inline void start_http() {
    cyw43_arch_lwip_begin();
    tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb && tcp_bind(pcb, IP_ANY_TYPE, 80) == ERR_OK) {
        listener = tcp_listen_with_backlog(pcb, MAX_CONN);
        tcp_accept(listener, on_accept);
    }
    cyw43_arch_lwip_end();
    printf(listener ? "[WEB] HTTP server on port 80\n" : "[WEB] HTTP server failed\n");
}

inline void ap_start() {
    printf("[WIFI] Starting access point \"%s\"...\n", PANEL_AP_SSID);
    cyw43_arch_enable_ap_mode(PANEL_AP_SSID, PANEL_AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
    ip_addr_t gw, mask;
#define PANEL_IP4_ADDR(dst, ...) IP4_ADDR(dst, __VA_ARGS__)   // expands PANEL_AP_IP first
    PANEL_IP4_ADDR(ip_2_ip4(&gw), PANEL_AP_IP);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);
    cyw43_arch_lwip_begin();
    // The SDK gives the AP 192.168.4.1 by default; replace it with ours.
    netif* ap = &cyw43_state.netif[CYW43_ITF_AP];
    netif_set_addr(ap, ip_2_ip4(&gw), ip_2_ip4(&mask), ip_2_ip4(&gw));
    dhcp_server_init(&dhcp, &gw, &mask);
    printf("[WIFI] Panel: http://%s\n", ip4addr_ntoa(netif_ip4_addr(ap)));
    cyw43_arch_lwip_end();
    panel::wifi_active = true;
}

inline void ap_stop() {
    printf("[WIFI] Stopping access point.\n");
    cyw43_arch_lwip_begin();
    for (auto& c : conns) conn_free(&c);
    dhcp_server_deinit(&dhcp);
    cyw43_arch_lwip_end();
    cyw43_arch_disable_ap_mode();
    panel::wifi_active = false;
}

inline void init() {
    usb_changed_at = get_absolute_time();
    next_check = make_timeout_time_ms(250);
    start_http();
}

// Decides whether the access point should run.
inline void tick() {
    if (!time_reached(next_check)) return;
    next_check = make_timeout_time_ms(250);

    bool usb = tud_mounted() && !tud_suspended();
    if (usb != usb_last) {
        usb_last = usb;
        usb_changed_at = get_absolute_time();
    }
    panel::usb_host = usb;
    int64_t stable_ms = absolute_time_diff_us(usb_changed_at, get_absolute_time()) / 1000;

    bool want = panel::wifi_active;
    if (panel::wifi_mode == panel::WIFI_ON) want = true;
    else if (panel::wifi_mode == panel::WIFI_OFF) want = false;
    else if (usb && stable_ms >= PANEL_USB_ON_MS) want = false;
    else if (!usb && stable_ms >= PANEL_NO_USB_MS) want = true;

    if (want && !panel::wifi_active) ap_start();
    else if (!want && panel::wifi_active) ap_stop();
}

}  // namespace web
