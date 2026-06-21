# Claude Buddy

A standalone Claude AI device built on an M5Stack AtomS3U. Plug it into any computer with internet — it automatically opens a browser with a full chat interface. No software to install on the host machine.

![LED: orange = connecting, blue = ready, rainbow = thinking, green = done]

---

## What it does

- **Plug-and-play** — connects to WiFi, opens a browser tab automatically on Mac/Windows/Linux
- **Web chat UI** — clean dark-theme interface at `http://claudebuddy.local`
- **Serial terminal fallback** — works on any device with a UART (Raspberry Pi, other microcontrollers, etc.)
- **Claude Sonnet** — uses `claude-sonnet-4-6` for real coding help, not just quick answers
- **Rainbow LED** — spins through all colors while Claude is thinking, so you know it's working
- **No hardcoded WiFi** — captive portal on first boot lets you enter credentials from any phone or laptop

---

## Hardware

- [M5Stack AtomS3U](https://shop.m5stack.com/products/atoms3u) (~$15)
  - ESP32-S3 chip
  - USB-A male plug (plugs directly into any computer)
  - WS2812B RGB LED (GPIO 35)
  - Button (GPIO 41)
  - No screen needed

That's it. One component.

---

## Setup

### 1. Install arduino-cli

```bash
brew install arduino-cli   # Mac
# or download from https://arduino.github.io/arduino-cli/
```

### 2. Add M5Stack board package

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32
```

### 3. Install libraries

```bash
arduino-cli lib install "WiFiManager"
arduino-cli lib install "ArduinoJson"
arduino-cli lib install "FastLED"
```

### 4. Add your API key

Open `ClaudeBuddy.ino` and replace:
```cpp
const char* CLAUDE_API_KEY = "PUT_UR_APIKEY_HERE";
```
with your key from [console.anthropic.com](https://console.anthropic.com).

> **Never commit your real API key.** Keep `PUT_UR_APIKEY_HERE` in the repo.

### 5. Compile and upload

Find your port first:
```bash
arduino-cli board list
```

Then compile and upload (replace `/dev/cu.usbmodem1101` with your port):
```bash
arduino-cli compile --upload \
  --fqbn m5stack:esp32:m5stack_atoms3 \
  --build-property "build.usb_mode=0" \
  --build-property "build.cdc_on_boot=0" \
  --port /dev/cu.usbmodem1101 \
  --libraries "$HOME/Documents/Arduino/libraries" \
  ClaudeBuddy.ino
```

On **Windows**, port will be something like `COM3`. On **Linux**, `/dev/ttyUSB0` or `/dev/ttyACM0`.

---

## First boot

1. Plug in the AtomS3U — LED turns **orange**
2. It creates a WiFi hotspot called **`Claude-Buddy-Setup`**
3. Connect your phone or laptop to that network
4. Open a browser and go to **`http://192.168.4.1`**
5. Enter your home/office WiFi credentials
6. Device saves them and connects — LED turns **blue**

WiFi credentials are saved to flash. On all future boots it connects automatically and the LED goes straight to blue in a few seconds.

---

## Usage

### Web UI (recommended)
When the device boots and connects to WiFi, it automatically opens a browser tab to `http://claudebuddy.local`. If that doesn't work, look at the serial output for the IP address and open `http://192.168.x.x` directly.

The web UI has full chat history, code formatting, and works on any browser.

### Serial terminal (works on anything)
Connect at 115200 baud:

```bash
# Mac/Linux
screen /dev/cu.usbmodem1101 115200

# or with any serial monitor (Arduino IDE, PuTTY, minicom, etc.)
```

Type a message and press Enter. Claude responds directly in the terminal.

---

## LED guide

| Color | Meaning |
|-------|---------|
| Orange | Connecting to WiFi / captive portal active |
| Blue solid | Ready, waiting for input |
| Rainbow spin | Thinking — waiting for Claude's response |
| Green flash | Response received |
| Red | Error (WiFi failed, API error) |

---

## Button

| Press | Action |
|-------|--------|
| Tap | Clear conversation history |
| Hold 3 seconds | Wipe saved WiFi (captive portal opens on next boot) |

---

## Changing WiFi networks

Hold the button for 3 seconds until the LED turns red. The device restarts, creates the `Claude-Buddy-Setup` hotspot again, and you enter new credentials through the portal.

---

## Notes

- The web UI auto-open uses USB HID keyboard emulation to type the browser shortcut. On **Mac** it uses Spotlight (`Cmd+Space`), **Windows** uses the Run dialog (`Win+R`), **Linux** uses `Ctrl+Alt+T`. If it doesn't work, just type `http://claudebuddy.local` manually in any browser.
- The device needs WiFi to reach the Claude API. The host computer does not — it just connects to the device's local web server.
- Conversation history is kept in RAM (lost on unplug). Button tap also clears it.
- Max response length is 8192 tokens (~6000 words), enough for full code files.
- Model can be changed by editing `CLAUDE_MODEL` in the sketch.

---

## Built with

- [M5Stack AtomS3U](https://shop.m5stack.com/products/atoms3u)
- [Arduino ESP32 core (M5Stack fork)](https://github.com/m5stack/M5Stack) 3.3.7
- [WiFiManager](https://github.com/tzapu/WiFiManager) 2.0.17
- [ArduinoJson](https://arduinojson.org/) 7.4.3
- [FastLED](https://fastled.io/) 3.10.4
- [Claude API](https://www.anthropic.com/) — `claude-sonnet-4-6`
