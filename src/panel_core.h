#pragma once
// Panel core: one text command set and one JSON state, shared by
// the USB serial link (Web Serial) and the Wi-Fi web server.
//
// Commands (one per line):
//   state | stream 0|1
//   light R G B            player MASK(0-31) BRIGHT(0 high,1 mid,2 low)
//   rumble L R MS          trig l|r|b MODE [A B]
//   motion 0|1             calib | calib reset
//   pair | disconnect | unpair
//   wifi auto|on|off
//
// Serial replies: "@S {json}" for state, "@R {json}" for command results.
// Everything else on the serial port is the normal log.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <initializer_list>

#include "pico/stdlib.h"
#include "pico/critical_section.h"
#include "pico/cyw43_arch.h"
#include "pico/async_context.h"
#include "tusb.h"

#include "panel_config.h"
#include "pico_w_registry_policy.h"
#include "pico_w_btstack.h"
#include "pico_w_flash_ptr.h"

namespace panel {

enum WifiMode { WIFI_AUTO = 0, WIFI_ON = 1, WIFI_OFF = 2 };

static critical_section_t cs;
static char state_json[1100];
static size_t state_len = 0;

constexpr int QUEUE_LEN = 8;
constexpr int CMD_LEN = 128;
static char queue[QUEUE_LEN][CMD_LEN];
static int q_head = 0;
static int q_count = 0;

static bool motion_enabled = false;
static bool stream_enabled = false;
static int wifi_mode = WIFI_AUTO;
static bool wifi_active = false;   // written by web::tick
static bool usb_host = false;      // written by web::tick

static uint8_t light_rgb[3] = {0, 0, 255};
static uint8_t player_mask = 0;
static uint8_t player_bright = 0;
static uint8_t rumble_l = 0;
static uint8_t rumble_r = 0;
static absolute_time_t rumble_until;
static bool rumble_active = false;
static char trig_mode[2][12] = {"off", "off"};

// Gyro zeroing (user offset on top of the factory calibration, raw units)
static float user_gyro_offset[3] = {0, 0, 0};
static int calib_frames = 0;
static double calib_sum[3] = {0, 0, 0};
static bool calib_prev_motion = false;
static const char* calib_status = "none";   // none | running | done | moving | nopad
constexpr int CALIB_FRAMES = 90;            // ~1.5 s at 60 Hz

static char serial_line[CMD_LEN];
static int serial_len = 0;
static absolute_time_t next_state;

inline IGamepadBase* pad() {
    auto& reg = policy_device::get_instance();
    IGamepadBase* g = reg.GetLibrary(0);
    return (g && g->IsConnected()) ? g : nullptr;
}

inline bool saved_pair(char* mac_out, size_t n) {
    const auto* cfg = reinterpret_cast<const device_config_t*>(XIP_BASE + FLASH_TARGET_OFFSET);
    if (cfg->exists != CONFIG_VALID_MARKER) return false;
    bd_addr_t a;
    memcpy(a, cfg->mac, 6);
    snprintf(mac_out, n, "%s", bd_addr_to_str(a));
    return true;
}

struct BtLock {
    BtLock() { async_context_acquire_lock_blocking(cyw43_arch_async_context()); }
    ~BtLock() { async_context_release_lock(cyw43_arch_async_context()); }
};

inline bool enqueue(const char* cmd) {
    bool ok = false;
    critical_section_enter_blocking(&cs);
    if (q_count < QUEUE_LEN) {
        int idx = (q_head + q_count) % QUEUE_LEN;
        strncpy(queue[idx], cmd, CMD_LEN - 1);
        queue[idx][CMD_LEN - 1] = 0;
        q_count++;
        ok = true;
    }
    critical_section_exit(&cs);
    return ok;
}

inline size_t copy_state(char* out, size_t n) {
    critical_section_enter_blocking(&cs);
    size_t len = state_len < n ? state_len : n;
    memcpy(out, state_json, len);
    critical_section_exit(&cs);
    return len;
}

inline const char* wifi_mode_name() {
    return wifi_mode == WIFI_ON ? "on" : wifi_mode == WIFI_OFF ? "off" : "auto";
}

inline void build_state() {
    char buf[sizeof(state_json)];
    IGamepadBase* g = pad();
    char mac[20] = "";
    bool has_saved = saved_pair(mac, sizeof(mac));
    const char* bt = g ? "connected" : inquiry_active ? "searching" : has_saved ? "waiting" : "idle";

    uint32_t b = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0, l2 = 0, r2 = 0, bat = -1;
    float gx = 0, gy = 0, gz = 0, ax = 0, ay = 0, az = 0, tx = 0, ty = 0;
    int touch = 0;
    if (g) {
        FInputContext* in = g->GetMutableDeviceContext()->GetInputState();
        const bool bits[] = {
            in->bCross, in->bCircle, in->bSquare, in->bTriangle,
            in->bLeftShoulder, in->bRightShoulder, in->bLeftTriggerThreshold, in->bRightTriggerThreshold,
            in->bShare, in->bStart, in->bLeftStick, in->bRightStick,
            in->bPSButton, in->bTouch, in->bMute,
            in->bDpadUp, in->bDpadDown, in->bDpadLeft, in->bDpadRight};
        for (size_t i = 0; i < sizeof(bits); i++) if (bits[i]) b |= (1u << i);
        lx = in->LeftAnalog.X; ly = in->LeftAnalog.Y;
        rx = in->RightAnalog.X; ry = in->RightAnalog.Y;
        l2 = in->LeftTriggerAnalog; r2 = in->RightTriggerAnalog;
        bat = in->BatteryLevel;
        gx = in->Gyroscope.X; gy = in->Gyroscope.Y; gz = in->Gyroscope.Z;
        ax = in->Accelerometer.X; ay = in->Accelerometer.Y; az = in->Accelerometer.Z;
        touch = in->bIsTouching ? (int)in->TouchFingerCount : 0;
        tx = in->TouchPosition.X; ty = in->TouchPosition.Y;
    }

    int n = snprintf(buf, sizeof(buf),
        "{\"v\":\"%s\",\"bt\":\"%s\",\"mac\":\"%s\",\"bat\":%.0f,\"b\":%lu,"
        "\"ls\":[%.3f,%.3f],\"rs\":[%.3f,%.3f],\"l2\":%.3f,\"r2\":%.3f,"
        "\"tp\":[%d,%.0f,%.0f],\"gy\":[%.2f,%.2f,%.2f],\"ac\":[%.3f,%.3f,%.3f],"
        "\"mo\":%d,\"cal\":{\"f\":%d,\"st\":\"%s\",\"off\":[%.1f,%.1f,%.1f]},"
        "\"light\":[%u,%u,%u],\"pl\":[%u,%u],\"tr\":[\"%s\",\"%s\"],"
        "\"wifi\":{\"mode\":\"%s\",\"on\":%d,\"ssid\":\"%s\"},\"usb\":%d}",
        PANEL_FW_VERSION, bt, mac, bat, (unsigned long)b,
        lx, ly, rx, ry, l2, r2,
        touch, tx, ty, gx, gy, gz, ax, ay, az,
        motion_enabled ? 1 : 0, factory_calibration_loaded ? 1 : 0, calib_status,
        user_gyro_offset[0], user_gyro_offset[1], user_gyro_offset[2],
        light_rgb[0], light_rgb[1], light_rgb[2], player_mask, player_bright,
        trig_mode[0], trig_mode[1],
        wifi_mode_name(), wifi_active ? 1 : 0, PANEL_AP_SSID, usb_host ? 1 : 0);
    if (n < 0 || n >= (int)sizeof(buf)) return;

    critical_section_enter_blocking(&cs);
    memcpy(state_json, buf, n);
    state_len = n;
    critical_section_exit(&cs);
}

inline bool apply_trigger(IGamepadBase* g, EDSGamepadHand hand, const char* mode, int a, int b) {
    IGamepadTrigger* t = g->GetIGamepadTrigger();
    if (!t) return false;
    std::vector<uint8_t> buf(10, 0);
    auto custom = [&](std::initializer_list<uint8_t> v) {
        size_t i = 0;
        for (uint8_t x : v) buf[i++] = x;
        t->SetCustomTrigger(hand, buf);
    };
    // Presets below are the ones from the original demo (main.cpp).
    if (!strcmp(mode, "off")) t->StopTrigger(hand);
    else if (!strcmp(mode, "res")) t->SetResistance((uint8_t)a, (uint8_t)b, hand);
    else if (!strcmp(mode, "gamecube")) t->SetGameCube(hand);
    else if (!strcmp(mode, "feedback")) custom({0x21, 0xfe, 0x03, 0xf8, 0xff, 0xff, 0x3f});
    else if (!strcmp(mode, "bow")) custom({0x22, 0x02, 0x01, 0x3f});
    else if (!strcmp(mode, "gallop")) custom({0x23, 0x82, 0x00, 0xf7, 0x02});
    else if (!strcmp(mode, "weapon")) custom({0x25, 0x08, 0x01, 0x07});
    else if (!strcmp(mode, "autogun")) t->SetMachineGun26(0xed, 0x03, 0x02, 0x09, hand);
    else if (!strcmp(mode, "machine")) custom({0x27, 0x80, 0x02, 0x3a, 0x0a, 0x04});
    else return false;
    return true;
}

inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// Runs one command. Must be called from the main loop.
inline void exec(const char* line, char* out, size_t outlen) {
    char cmd[16] = "", a1[16] = "", a2[16] = "";
    int n1 = 0, n2 = 0, n3 = 0;
    sscanf(line, "%15s", cmd);
    auto ok = [&]() { snprintf(out, outlen, "{\"ok\":true,\"cmd\":\"%s\"}", cmd); };
    auto err = [&](const char* e) { snprintf(out, outlen, "{\"ok\":false,\"cmd\":\"%s\",\"err\":\"%s\"}", cmd, e); };

    if (!strcmp(cmd, "stream")) {
        stream_enabled = sscanf(line, "%*s %d", &n1) == 1 && n1 == 1;
        return ok();
    }
    if (!strcmp(cmd, "wifi")) {
        if (sscanf(line, "%*s %15s", a1) != 1) return err("args");
        if (!strcmp(a1, "auto")) wifi_mode = WIFI_AUTO;
        else if (!strcmp(a1, "on")) wifi_mode = WIFI_ON;
        else if (!strcmp(a1, "off")) wifi_mode = WIFI_OFF;
        else return err("args");
        return ok();
    }
    if (!strcmp(cmd, "motion")) {
        if (sscanf(line, "%*s %d", &n1) != 1) return err("args");
        motion_enabled = n1 == 1;
        return ok();
    }

    BtLock lock;
    IGamepadBase* g = pad();

    if (!strcmp(cmd, "pair")) {
        if (g) return err("connected");
        if (!inquiry_active) start_pairing_inquiry();
        return ok();
    }
    if (!strcmp(cmd, "disconnect")) {
        if (acl_handle == HCI_CON_HANDLE_INVALID) return err("nopad");
        gap_disconnect(acl_handle);
        return ok();
    }
    if (!strcmp(cmd, "unpair")) {
        flash_clear_config();
        if (acl_handle != HCI_CON_HANDLE_INVALID) gap_disconnect(acl_handle);  // disconnect handler starts a new search
        else if (!inquiry_active) start_pairing_inquiry();
        return ok();
    }
    if (!strcmp(cmd, "calib")) {
        if (sscanf(line, "%*s %15s", a1) == 1 && !strcmp(a1, "reset")) {
            if (g) {
                FGamepadCalibration& c = g->GetMutableDeviceContext()->Calibration;
                c.GyroBiasX -= user_gyro_offset[0];
                c.GyroBiasY -= user_gyro_offset[1];
                c.GyroBiasZ -= user_gyro_offset[2];
            }
            user_gyro_offset[0] = user_gyro_offset[1] = user_gyro_offset[2] = 0;
            calib_status = "none";
            return ok();
        }
        if (!g) { calib_status = "nopad"; return err("nopad"); }
        calib_prev_motion = motion_enabled;
        motion_enabled = true;
        calib_sum[0] = calib_sum[1] = calib_sum[2] = 0;
        calib_frames = CALIB_FRAMES;
        calib_status = "running";
        return ok();
    }

    // Output commands below need a connected pad.
    if (!g) return err("nopad");

    if (!strcmp(cmd, "light")) {
        if (sscanf(line, "%*s %d %d %d", &n1, &n2, &n3) != 3) return err("args");
        light_rgb[0] = clamp255(n1); light_rgb[1] = clamp255(n2); light_rgb[2] = clamp255(n3);
        if (auto* l = g->GetIGamepadLightbar()) l->SetLightbar({light_rgb[0], light_rgb[1], light_rgb[2], 0});
        g->UpdateOutput();
        return ok();
    }
    if (!strcmp(cmd, "player")) {
        if (sscanf(line, "%*s %d %d", &n1, &n2) != 2) return err("args");
        player_mask = (uint8_t)(n1 & 0x1f);
        player_bright = (uint8_t)(n2 < 0 ? 0 : n2 > 2 ? 2 : n2);
        if (auto* l = g->GetIGamepadLightbar()) l->SetPlayerLed(static_cast<EDSPlayer>(player_mask), player_bright);
        g->UpdateOutput();
        return ok();
    }
    if (!strcmp(cmd, "rumble")) {
        if (sscanf(line, "%*s %d %d %d", &n1, &n2, &n3) != 3) return err("args");
        rumble_l = clamp255(n1); rumble_r = clamp255(n2);
        int ms = n3 < 50 ? 50 : n3 > 5000 ? 5000 : n3;
        if (auto* r = g->GetIGamepadRumbles()) r->SetVibration(rumble_l, rumble_r);
        g->UpdateOutput();
        rumble_until = make_timeout_time_ms(ms);
        rumble_active = true;
        return ok();
    }
    if (!strcmp(cmd, "trig")) {
        n1 = n2 = 0;
        if (sscanf(line, "%*s %15s %15s %d %d", a1, a2, &n1, &n2) < 2) return err("args");
        bool left = !strcmp(a1, "l") || !strcmp(a1, "b");
        bool right = !strcmp(a1, "r") || !strcmp(a1, "b");
        if (!left && !right) return err("args");
        if (left && !apply_trigger(g, EDSGamepadHand::Left, a2, clamp255(n1), clamp255(n2))) return err("mode");
        if (right && !apply_trigger(g, EDSGamepadHand::Right, a2, clamp255(n1), clamp255(n2))) return err("mode");
        if (left) snprintf(trig_mode[0], sizeof(trig_mode[0]), "%s", a2);
        if (right) snprintf(trig_mode[1], sizeof(trig_mode[1]), "%s", a2);
        g->UpdateOutput();
        return ok();
    }
    return err("unknown");
}

inline void handle_serial_line(const char* line) {
    if (!strcmp(line, "state")) {
        build_state();
        static char buf[sizeof(state_json) + 1];
        size_t n = copy_state(buf, sizeof(buf) - 1);
        buf[n] = 0;
        printf("@S %s\n", buf);
        return;
    }
    char out[160];
    exec(line, out, sizeof(out));
    printf("@R %s\n", out);
}

inline void init() {
    critical_section_init(&cs);
    next_state = make_timeout_time_ms(50);
    build_state();
}

// Called every main loop iteration, after the pad input was updated.
inline void tick() {
    // Serial input (non-blocking)
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r') continue;
        if (c == '\n') {
            serial_line[serial_len] = 0;
            if (serial_len) handle_serial_line(serial_line);
            serial_len = 0;
        } else if (serial_len < CMD_LEN - 1) {
            serial_line[serial_len++] = (char)c;
        }
    }

    // Commands from the web server
    while (true) {
        char cmd[CMD_LEN];
        bool have = false;
        critical_section_enter_blocking(&cs);
        if (q_count > 0) {
            memcpy(cmd, queue[q_head], CMD_LEN);
            q_head = (q_head + 1) % QUEUE_LEN;
            q_count--;
            have = true;
        }
        critical_section_exit(&cs);
        if (!have) break;
        char out[160];
        exec(cmd, out, sizeof(out));
    }

    // Motion sensors (the original demo forced them off every frame)
    if (auto* g = pad()) {
        if (auto* s = g->GetIGamepadSensors()) s->EnableMotionSensor(motion_enabled);
    }

    // Timed rumble
    if (rumble_active && time_reached(rumble_until)) {
        rumble_active = false;
        BtLock lock;
        if (IGamepadBase* g = pad()) {
            if (auto* r = g->GetIGamepadRumbles()) r->SetVibration(0, 0);
            g->UpdateOutput();
        }
    }

    // Gyro zeroing: average the gyro while the pad lies still
    if (calib_frames > 0) {
        IGamepadBase* g = pad();
        if (!g) {
            calib_frames = 0;
            calib_status = "nopad";
            motion_enabled = calib_prev_motion;
        } else {
            FInputContext* in = g->GetMutableDeviceContext()->GetInputState();
            if (calib_frames < CALIB_FRAMES - 10) {   // skip the first frames after enabling sensors
                calib_sum[0] += in->Gyroscope.X;
                calib_sum[1] += in->Gyroscope.Y;
                calib_sum[2] += in->Gyroscope.Z;
            }
            if (--calib_frames == 0) {
                const int count = CALIB_FRAMES - 10;
                const float m[3] = {(float)(calib_sum[0] / count), (float)(calib_sum[1] / count), (float)(calib_sum[2] / count)};
                motion_enabled = calib_prev_motion;
                if (fabsf(m[0]) > 30 || fabsf(m[1]) > 30 || fabsf(m[2]) > 30) {
                    calib_status = "moving";
                } else {
                    FGamepadCalibration& cal = g->GetMutableDeviceContext()->Calibration;
                    const float f[3] = {cal.GyroFactorX, cal.GyroFactorY, cal.GyroFactorZ};
                    float d[3];
                    for (int i = 0; i < 3; i++) d[i] = f[i] != 0.0f ? m[i] / f[i] : 0.0f;
                    cal.GyroBiasX += d[0];
                    cal.GyroBiasY += d[1];
                    cal.GyroBiasZ += d[2];
                    for (int i = 0; i < 3; i++) user_gyro_offset[i] += d[i];
                    calib_status = "done";
                    printf("[CAL] Gyro zeroed: %.2f %.2f %.2f deg/s\n", m[0], m[1], m[2]);
                }
            }
        }
    }

    // State snapshot + serial stream (20 Hz)
    if (time_reached(next_state)) {
        next_state = make_timeout_time_ms(50);
        build_state();
        if (stream_enabled) {
            if (!tud_cdc_connected()) {
                stream_enabled = false;
            } else {
                static char buf[sizeof(state_json) + 1];
                size_t n = copy_state(buf, sizeof(buf) - 1);
                buf[n] = 0;
                printf("@S %s\n", buf);
            }
        }
    }
}

}  // namespace panel

static void panel_apply_user_calibration(FGamepadCalibration& Calibration) {
    Calibration.GyroBiasX += panel::user_gyro_offset[0];
    Calibration.GyroBiasY += panel::user_gyro_offset[1];
    Calibration.GyroBiasZ += panel::user_gyro_offset[2];
}
