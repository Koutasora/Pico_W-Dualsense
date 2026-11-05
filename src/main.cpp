#include "pico/cyw43_arch.h"

// Forward declarations for btstack
#include <memory>
#include "pico_w_registry_policy.h"

// GamepadCore headers
#include "pico_w_btstack.h"
#include "GCore/Interfaces/IPlatformHardware.h"
#include "GCore/Templates/TBasicDeviceRegistry.h"
#include "pico_w_platform.h"
#include "panel_core.h"
#include "web/web_server.h"
#include "usb/usb_panel.h"
using pico_platform = GamepadCore::TGenericHardwareInfo<pico_w_platform_policy>;

inline void initialize_device() {
    printf("Initializing device...\n");
    FDeviceContext Context = {};
    Context.Path = "Bluetooth";
    Context.IsConnected = false;
    Context.DeviceType = EDSDeviceType::DualSense;
    Context.ConnectionType = EDSDeviceConnection::Bluetooth;

    auto& registry = policy_device::get_instance();
    registry.CreateDevice(Context);
}

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("ERROR: Failed to initialize CYW43\n");
        return -1;
    }
    usb_panel_init();   // serial log + panel network + picotool reset over USB

    sleep_ms(2000);

    printf("\n");
    printf("========================================\n");
    printf("   PICO W - BLUETOOTH DISCOVERY\n");
    printf("========================================\n");

    auto HardwareInfo = std::make_unique<pico_platform>();
    IPlatformHardware::SetInstance(std::move(HardwareInfo));
    printf("Hardware initialized OK\n");

    using namespace policy_device;
    initialize_device();
    auto& registry = get_instance();
    printf("Device initialized OK\n");

    init_bluetooth();
    printf("Bluetooth initialized OK\n");

    panel::init();
    web::init();
    printf("Panel initialized OK (%s)\n", PANEL_FW_VERSION);

    // The original demo (buttons triggering rumble, lights and trigger effects)
    // was removed: the pad behaves like a normal controller and everything is
    // tested from the panel instead.
    int blink_cnt = 0;
    while(true) {
        if (auto* gamepad = registry.GetLibrary(0)) {
            if (gamepad->IsConnected()) {
                if (auto* sensors = gamepad->GetIGamepadSensors()) {
                    sensors->EnableMotionSensor(panel::motion_enabled);
                }
                if (auto* touch = gamepad->GetIGamepadTouch()) {
                    touch->EnableTouch(true);
                }
                gamepad->UpdateInput(0.008f);
            }
        }

        // Pico LED blinks while the firmware runs
        if (blink_cnt++ % 100 == 0) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        }
        if (blink_cnt % 100 == 50) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }

        panel::tick();
        web::tick();

        sleep_ms(8);   // ~125 Hz: pad input and panel stay responsive
    }
    return 0;
}
