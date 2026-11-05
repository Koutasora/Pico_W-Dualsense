# DualSense Panel for Raspberry Pi Pico 2 W

Firmware for the **Raspberry Pi Pico 2 W** that connects to a Sony **DualSense** controller over Bluetooth and serves a control panel in the browser. Open the panel to watch every input live and to test the lightbar, player LEDs, rumble and adaptive triggers. You can also pair, disconnect and unpair the controller from there.

It is a fork of [rafaelvaloto/Pico_W-Dualsense](https://github.com/rafaelvaloto/Pico_W-Dualsense), built on [Gamepad-Core](https://github.com/rafaelvaloto/Gamepad-Core). The original project is a Bluetooth test demo. This fork adds the panel, fixes reconnecting, and makes the controller behave like a normal pad: the buttons no longer trigger demo effects.

## Hardware

- Raspberry Pi **Pico 2 W** (RP2350). The release file does not run on the original Pico W (RP2040).
- Sony DualSense (PS5) controller
- USB cable, and optionally a power bank or battery for Wi-Fi mode

## Install

1. Download `DualSense-Panel-pico2w-<version>.uf2` from [Releases](../../releases).
2. Hold the **BOOTSEL** button on the Pico, plug it into the PC, release the button.
3. Drag the `.uf2` file onto the `RP2350` drive. The Pico restarts with the new firmware.

## Pairing and reconnecting

- **First pairing:** hold **PS + Create** on the controller until the lightbar blinks fast. Pico searches for about 40 seconds after it starts. If that time has passed, click **Search** in the panel.
- **Later:** press **PS**. The controller reconnects by itself. The pairing is stored in Pico's flash and survives firmware updates.
- If the controller was paired with a PS5 or a PC in the meantime, pair it with the Pico again.

## Opening the panel

The panel address is **`http://192.168.20.1`** in both modes.

| Pico is connected to | How to reach the panel |
|---|---|
| A PC over USB | Open `http://192.168.20.1` on that PC. Pico shows up as a small USB network adapter ("Remote NDIS"). Windows uses its built-in driver, and the PC's own internet connection is not affected. |
| A power bank / charger only | Connect your phone or laptop to the Wi-Fi network **`DualSense-Pico`** (password `dualsense`), then open `http://192.168.20.1`. The phone may warn that the network has no internet. That is expected. |

**Time limit.** The panel stays reachable for **5 minutes after the last use**. Opening the page and clicking controls count as use. A tab that only shows the live view does not.

- On USB, the panel network then stops answering. Replug the cable to open it again.
- On Wi-Fi, the radio turns off completely and stops transmitting. Power-cycle the Pico to open it again.
- The panel shows how much time is left.

The serial log (a USB COM port, 115200 baud) is available at all times. The file `panel/panel.html` can also be opened directly in Chrome or Edge: it talks to Pico over that COM port (Web Serial), as a fallback.

## What the panel offers

- **Controller:** connection state, paired address, battery, plus Search, Disconnect and Unpair.
- **Live view:** a drawing of the controller that lights up the pressed buttons, moves the sticks, fills L2/R2 and shows the finger on the touchpad. Exact values are listed below it. The state is pushed about 40 times per second.
- **Light:** lightbar colour, player LEDs and their brightness.
- **Motion:** gyroscope and accelerometer readings, plus a "Zero gyro" step that removes the drift while the controller lies still.
- **Rumble:** left and right motor strength, with a test pulse of a chosen length.
- **Adaptive triggers:** off, resistance (start and force) and presets (GameCube, feedback, bow, gallop, weapon, automatic gun, machine) for L2 and R2 separately.
- **Wi-Fi:** Auto (5-minute window on battery) or Off.
- **Language:** English and Polish, with light and dark themes.

## Configuration

Settings are in [`src/panel_config.h`](src/panel_config.h). Rebuild after changing them.

| Setting | Default | Meaning |
|---|---|---|
| `PANEL_AP_SSID` / `PANEL_AP_PASSWORD` | `DualSense-Pico` / `dualsense` | Wi-Fi network name and password (WPA2, at least 8 characters) |
| `PANEL_AP_IP` / `PANEL_AP_IP_STR` | `192.168.20.1` | Panel address, the same for Wi-Fi and USB |
| `PANEL_WINDOW_MS` | 5 minutes | How long the panel stays reachable after the last use |
| `PANEL_NO_USB_MS` | 4 s | On battery, delay before Wi-Fi starts |
| `PANEL_STREAM_MS` | 20 ms | Live view update interval |
| `PANEL_BT_DEBUG` | off | Dumps every Bluetooth HCI packet to the serial log |

## Building from source

Requirements: [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.1.1 with submodules, Arm GNU Toolchain (`arm-none-eabi-gcc`, tested with 14.2), CMake and Ninja. `picotool` and `pioasm` come from [pico-sdk-tools](https://github.com/raspberrypi/pico-sdk-tools) or are built by the SDK.

1. Clone this fork, branch `panel-web`.
2. Put **Gamepad-Core v1.0.5** into `lib/Gamepad-Core`. Newer versions do not compile for the Pico.

   ```sh
   git clone --branch v1.0.5 --depth 1 https://github.com/rafaelvaloto/Gamepad-Core.git
   mkdir -p Pico_W-Dualsense/lib/Gamepad-Core
   cp -r Gamepad-Core/Source Gamepad-Core/CMakeLists.txt Gamepad-Core/LICENSE Pico_W-Dualsense/lib/Gamepad-Core/
   ```

3. Configure and build for the Pico 2 W:

   ```sh
   export PICO_SDK_PATH=/path/to/pico-sdk
   cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

The firmware is `build/dualsense_test.uf2`. After the build, CMake tries to flash a connected Pico with `picotool`. Without a Pico that last step fails, but the `.uf2` file is already there.

## Changes compared to the original

- **Reconnecting fixed:** on reconnect the DualSense first opens an SDP channel and drops the link when it is refused. Pico now runs an SDP server and accepts the HID channels the controller opens.
- **Pairing no longer erased by mistake:** the saved key used to be deleted on any authentication error. Now it is deleted only when the controller reports the key as missing.
- **Factory calibration read correctly:** the motion calibration is taken from the controller's reply (feature report `0x05`) instead of the request buffer.
- **Builds against Gamepad-Core v1.0.5.**
- **Demo removed:** the original button demo (rumble on Cross and Circle, lights, trigger presets on the D-pad) is gone. All of that is in the panel.
- **USB stack:** USB is handled by the firmware itself (TinyUSB) instead of `pico_stdio_usb`. That gives one cable with a network adapter, the serial log and the `picotool` reset interface. TinyUSB, Bluetooth and lwIP all run in the same CYW43 async context.

## Project layout

| Path | Contents |
|---|---|
| `src/main.cpp` | Startup and main loop |
| `src/pico_w_btstack.h` | Bluetooth: pairing, reconnecting, HID channels, SDP |
| `src/panel_core.h` | Panel commands and JSON state (shared by USB and Wi-Fi) |
| `src/web/` | Wi-Fi access point, HTTP server, DHCP server |
| `src/usb/` | USB descriptors, RNDIS network adapter, serial log, reset interface |
| `panel/panel.html` | The panel page, embedded into the firmware at build time |

## Original author README

<details>
<summary>Show the README of the original project (rafaelvaloto/Pico_W-Dualsense)</summary>

<div align="center">

# 🎮 Pico W + DualSense (PS5)
### Bluetooth Classic Firmware

**A Portable Proof of Concept powered by [Gamepad-Core](https://github.com/rafaelvaloto/Gamepad-Core)**

[![Raspberry Pi Pico W](https://img.shields.io/badge/Pico_W-Compatible-C51A4A?style=for-the-badge&logo=raspberrypi&logoColor=white)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![DualSense](https://img.shields.io/badge/DualSense-Supported-003791?style=for-the-badge&logo=playstation&logoColor=white)](https://www.playstation.com/accessories/dualsense-wireless-controller/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](LICENSE)

**C++ • BTstack • Gamepad-Core**


</div>

---

## 🚀 Project Overview

This project is a **C/C++ firmware** for the **Raspberry Pi Pico W** that establishes a full-featured **Bluetooth Classic connection** with the **Sony DualSense (PS5)** controller. It demonstrates the extreme portability of the **[Gamepad-Core](https://github.com/rafaelvaloto/Gamepad-Core)** library by running the same cross-platform API that powers high-end game engines (Unreal Engine, Godot, O3DE) on a resource-constrained microcontroller.

### 🎯 Why This Project Exists

This firmware was created to **stress-test the portability** of the Gamepad-Core library. It proves that the same library running on powerful desktop machines and game engines can also run smoothly on a **$6 microcontroller with limited resources**.

**Key Achievement**: A unified codebase for DualSense support across:
- 🖥️ Desktop platforms (Windows, Linux, macOS)
- 🎮 Game engines (Unreal Engine, Godot, O3DE)
- 🔧 Microcontrollers (Pico W, ESP32, and more)

---

### 🎥 [Click and watch the example video on YouTube.](https://www.youtube.com/watch?v=GgKDtwfS6v4)


## ✨ Features

### ✅ Currently Implemented

- **Stable Bluetooth Classic Connection**: Reliable pairing and connectivity with DualSense controllers
- **Persistent Pairing**: Stores controller MAC address in Pico W's flash memory for automatic reconnection
- **Full Input Reading**:
  - Extended reports (`0x31`) unlocking all advanced features:
    - All buttons and analog sticks
    - Gyroscope and accelerometer data
    - Touchpad input
    - Battery status
- **Complete Output Features**:
  - 💡 **LED Control**: Lightbar colors and player LED indicators
  - 📳 **Rumble/Haptics**: Vibration motor control (soft and heavy)
  - 🎚️ **Adaptive Triggers**: Full trigger effect system
    - Resistance modes (Feedback, Weapon, Vibration)
    - Dynamic tension effects (Bow, Gallop, Machine)
    - Weapon simulation (GameCube, Semi-Automatic, Automatic)
- **Plug-and-Play Integration**: Powered by Gamepad-Core's abstraction layer

### 🎮 Interactive Test Demo

The firmware includes a comprehensive test suite demonstrating all DualSense features:

```
=======================================================
           DUALSENSE INTEGRATION TEST                  
=======================================================

 [ FACE BUTTONS ]
   (X) Cross    : Heavy Rumble + RED Light
   (O) Circle   : Soft Rumble  + YELLOW Light
   [ ] Square   : Trigger Effect: GAMECUBE (R2)
   /_\ Triangle : Stop All

-------------------------------------------------------

 [ D-PADS & SHOULDERS ]
   [L1]    : Trigger Effect: Gallop (L2)
   [R1]    : Trigger Effect: Machine (R2)
   [UP]    : Trigger Effect: Feedback (Rigid)
   [DOWN]  : Trigger Effect: Bow (Tension)
   [LEFT]  : Trigger Effect: Weapon (Semi)
   [RIGHT] : Trigger Effect: Automatic Gun (Buzz)

=======================================================
```

---

## 🔧 Hardware Requirements

- **Raspberry Pi Pico W** (with wireless capabilities)
- **Sony DualSense Controller** (PS5)
- USB cable for programming the Pico W

---

## 📚 Software Requirements

- **Pico SDK** (with BTstack integrated)
- **Gamepad-Core library** (minimalist embedded version)
- CMake 3.20+
- C++20 compatible compiler

---

## 🛠️ Setup Instructions

### 1. Clone This Repository

```bash
git clone https://github.com/rafaelvaloto/Pico_W-Dualsense.git
cd Pico_W-Dualsense && mkdir -p lib/Gamepad-Core
```

### 2. Download Gamepad-Core (Minimalist Version)

Clone the minimalist version of **Gamepad-Core** to a directory **outside** your Pico project:

```bash
# Clone to a separate dependencies folder (not inside the Pico project)
cd ..
git clone --depth 1 https://github.com/rafaelvaloto/Gamepad-Core.git
```

Your directory structure should look like this:
```
parent-folder/
├── Pico_W-Dualsense/        # Your Pico project
└── Gamepad-Core/            # Gamepad-Core library (separate)
```

### 3. Export for Embedded Systems

Navigate to the Gamepad-Core folder and run the export script, pointing it to your Pico project's `lib` folder:

```bash
cd Gamepad-Core
./export_micro.sh ../Pico_W-Dualsense/lib/Gamepad-Core
```

This script will:
- Copy necessary headers and source files to your Pico project
- Organize files for embedded compilation
- Remove unnecessary dependencies for microcontroller use

### 4. Configure SDK Integration (`gc_config.h`)

Create a `gc_config.h` file at the root of your Pico project to map Gamepad-Core's internal functions to Pico SDK functions:

```cpp name=gc_config.h
//
// Gamepad-Core Configuration for Pico SDK
//
#pragma once

#if defined(GAMEPAD_CORE_EMBEDDED)
    #include "pico/stdlib.h"

    #ifndef gc_sleep_ms
        #define gc_sleep_ms ::sleep_ms
    #endif
#endif
```

### 5. Build the Firmware

```bash
cd Pico_W-Dualsense
mkdir build && cd build
cmake ..
make
```

### 6. Flash to Pico W

1. Hold the **BOOTSEL** button on your Pico W
2. Connect it to your computer via USB
3. Copy the generated `*.uf2` file to the Pico's mass storage device
4. The Pico will automatically reboot and start running the firmware

---

## 7. Serial Debugging and Monitoring

To inspect the Pico W log output and debug DualSense readings in real-time on Linux, you can use `minicom` connected to the USB serial port.

Install `minicom` (if not already installed):

```bash
sudo apt install minicom
```

With the Pico W running the firmware and connected via USB, execute the following command (the default Pico SDK baud rate is 115200):

```bash
sudo minicom -b 115200 -D /dev/ttyACM0
```

*Note: The exact port may vary depending on your system configuration or connection order. To find the correct port, you can use the command `ls /dev/ttyACM*`.*

**Useful Minicom Shortcuts:**
* **Exit:** Press `Ctrl+A`, release, press `X`, and select Yes.
* **Clear screen:** Press `Ctrl+A`, release, press `C`.


## 📖 How It Works

### Bluetooth Connection Flow

1. **Pairing Mode**: On first run, the Pico W enters pairing mode
2. **Controller Discovery**: Put your DualSense in pairing mode (hold PS + Share buttons)
3. **Connection**: The controller connects and the pairing key is stored in flash
4. **Auto-Reconnect**: On subsequent power-ups, the Pico automatically reconnects to the paired controller

### Report ID 0x31 - Advanced Features

The firmware automatically configures the DualSense to use **Report ID 0x31**, which unlocks:
- Full sensor data (gyro, accelerometer)
- Touchpad input
- Battery status
- Support for all output features (haptics, LEDs, adaptive triggers)

### Integration with BTstack

The `packet_handler` in `src/pico_w_platform.h` processes L2CAP packets from the controller:
- **`0x31` packets**: Read with offset 2 (extended input)

Data is copied directly to the `Context->Buffer` of Gamepad-Core, which handles all protocol decoding and abstraction.

### Output Features via Bluetooth

The firmware uses Gamepad-Core's output buffer system to send commands back to the DualSense:
- **LED commands**: Set lightbar RGB colors and player indicators
- **Rumble commands**: Control left/right motors independently
- **Trigger effects**: Configure adaptive triggers with various resistance patterns

All output features use the same `0x31` report format for bidirectional communication.

### Main Loop Architecture

```cpp
    auto HardwareInfo = std::make_unique<pico_platform>();
    IPlatformHardwareInfo::SetInstance(std::move(HardwareInfo));
    printf("Hardware initialized OK\n");

    using namespace policy_device;
    initialize_device();
    auto& registry = get_instance();
    printf("Device initialized OK\n");

    init_bluetooth();
    printf("Bluetooth initialized OK\n");

    int blink_cnt = 0;
    while(true) {
        if (blink_cnt++ % 50 == 0) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        if (blink_cnt % 50 == 25) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

        if (auto* gamepad = registry.GetLibrary(0)) {
            gamepad->UpdateInput(0.016f);
            FInputContext* input = gamepad->GetMutableDeviceContext()->GetInputState();
            if (input->bCross) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                printf("Cross button pressed\n");
            } else if (input->bCircle) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                printf("Circle button pressed\n");
            } else if (input->bSquare) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                printf("Square button pressed\n");
            } else if (input->bTriangle) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                printf("Triangle button pressed\n");
            }
        }
        sleep_ms(16);
    }
```

Library processing (PlugAndPlay and Updates) occurs in the main loop, ensuring operations requiring mutexes or delays don't block the Bluetooth interrupt handler.

---

## 🎮 About Gamepad-Core

**[Gamepad-Core](https://github.com/rafaelvaloto/Gamepad-Core)** is a revolutionary DualSense & DualShock library designed for true cross-platform portability:

### Core Philosophy
- ✅ **Pure C++20**: No external dependencies
- ✅ **Engine Agnostic**: Works with any game engine or framework
- ✅ **Platform Independent**: One codebase for all platforms
- ✅ **Embedded Ready**: Runs on microcontrollers with minimal resources

### Proven Compatibility
- **Game Engines**: Unreal Engine 5, Godot 4, O3DE
- **Operating Systems**: Windows, Linux, macOS, PlayStation
- **Microcontrollers**: Raspberry Pi Pico W, ESP32, and more

### Architecture Highlights
- **Clean Abstraction**: Simple API for complex controller features
- **Buffer Management**: Efficient input/output buffer handling
- **Event System**: Callback-based controller events
- **Minimal Footprint**: Optimized for embedded systems

**[→ Visit Gamepad-Core Repository](https://github.com/rafaelvaloto/Gamepad-Core)**

---

## 🔬 Technical Details

### Supported DualSense Features

| Feature | Status |
|---------|--------|
| Buttons (×17) | ✅ Fully Working |
| Analog Sticks (L/R) | ✅ Fully Working |
| Triggers (L2/R2) | ✅ Fully Working |
| D-Pad | ✅ Fully Working |
| Gyroscope | ✅ Fully Working |
| Accelerometer | ✅ Fully Working |
| Touchpad | ✅ Fully Working |
| Battery Status | ✅ Fully Working |
| Lightbar RGB | ✅ Fully Working |
| Player LED | ✅ Fully Working |
| Rumble Motors | ✅ Fully Working |
| Adaptive Triggers | ✅ Fully Working |
| Audio | ❌ Not planned |

> **Legend**: ✅ = Fully working | ❌ = Not implemented

---

## 🤝 Contributing

Contributions are welcome! Whether it's:
- Optimizing performance
- Adding support for other controllers (DualShock 4, Nintendo Pro, etc.)
- Improving documentation
- Adding new trigger effects
- Implementing audio support

Feel free to open issues or submit pull requests.

---

## 📜 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- **[BTstack](https://github.com/bluekitchen/btstack)**: Bluetooth stack for embedded systems
- **[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)**: Official SDK for RP2040
- **[Gamepad-Core](https://github.com/rafaelvaloto/Gamepad-Core)**: The cross-platform magic behind this project

---

## 📬 Contact & Support

- **Author**: Rafael Valoto
- **Repository Issues**: [Report a Bug](https://github.com/rafaelvaloto/Pico_W-Dualsense/issues)
- **Gamepad-Core Issues**: [Library Support](https://github.com/rafaelvaloto/Gamepad-Core/issues)

---

</details>

## License

MIT, see [LICENSE](LICENSE). The original project is Copyright (c) 2026 Rafael Valoto. The changes in this fork are published under the same license.

Third-party parts:
- [Gamepad-Core](https://github.com/rafaelvaloto/Gamepad-Core) by Rafael Valoto (MIT)
- [Pico SDK](https://github.com/raspberrypi/pico-sdk), [BTstack](https://github.com/bluekitchen/btstack), [TinyUSB](https://github.com/hathach/tinyusb) and [lwIP](https://savannah.nongnu.org/projects/lwip/), under their own licenses
- `src/web/dhcpserver.c`: from the Raspberry Pi pico-examples, originally from MicroPython (MIT)
- The controller line drawing in the panel is the "Dualsense" vector by **Alex Martynov** (Tech And Electronics collection) from [SVG Repo](https://www.svgrepo.com/), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Modified: split into separate parts, recoloured for the light and dark themes, with L2/R2 bars, a Mute button and player LED dots added.
