# Claude Buddy

A standalone Claude AI device built on an M5Stack AtomS3U. Plug it into any computer with internet — it shows up as both a USB chat device (web UI) and a USB flash drive with easy-launch scripts. No software to install on the host machine.

![LED: orange = connecting, blue = ready, rainbow = thinking, green = done]

---

## What it does

- **USB drive** — appears as a flash drive with `Start Here.htm`, `Launch Mac.sh`, `Launch Windows.bat`, and `Launch Linux.sh`
- **Web chat UI** — full chat interface at `http://claudebuddy.local` (works on any browser including Raspberry Pi)
- **Code execution** — companion app lets Claude actually run Python, Bash, and Node.js on your computer (▶ Run button)
- **Chat history** — conversation saved to `~/.claudebuddy/history.json` on the host; plug back into the same computer and your history is restored. New computer = fresh start automatically.
- **Serial terminal** — works on any device with a UART (Raspberry Pi, other microcontrollers, headless Linux)
- **Claude Sonnet** — uses `claude-sonnet-4-6` for real coding help
- **Rainbow LED** — cycles through all colors while Claude is thinking
- **Portable WiFi** — captive portal on first boot lets you enter credentials from any phone or laptop

---

## Quick Start (when plugged in)

### Option A — USB drive (recommended)

1. A **Claude Buddy** USB drive appears on your computer
2. Open the launcher for your OS:
   - **Mac:** Right-click `Launch Mac.sh` → Open With → Terminal (or open Terminal and drag the file in)
   - **Windows:** Double-click `Launch Windows.bat`
   - **Linux:** Open terminal, `bash /path/to/"Launch Linux.sh"`
   - **Any OS:** Double-click `Start Here.htm` for a browser with instructions
3. A browser opens to `http://claudebuddy.local` — start chatting!

### Option B — Open browser directly

Just open `http://claudebuddy.local` or `http://[device IP]` in any browser on the same WiFi.

### Option C — Raspberry Pi / headless Linux

```bash
# Serial terminal (always works)
screen /dev/ttyUSB0 115200
# or
screen /dev/ttyACM0 115200

# Or open browser (Chromium is pre-installed on Pi OS)
chromium-browser http://claudebuddy.local
```

---

## ⚠️ Python 3 required for code execution & history

The companion app (which enables ▶ Run buttons and saves chat history) requires Python 3.

| OS | Status |
|----|--------|
| **macOS** | Python 3 is pre-installed (or `brew install python3`) |
| **Linux / Pi** | Usually pre-installed. If not: `sudo apt install python3` |
| **Windows** | **NOT pre-installed.** Download from [python.org/downloads](https://www.python.org/downloads/) — check "Add Python to PATH" during install |

Without Python 3, the device still works — you just won't get the Run button or history persistence. The web UI and serial terminal work on any computer regardless.

---

## Chat History

When the companion is running:
- Your conversation is saved to `~/.claudebuddy/history.json` on the host computer after each reply
- Plug the device into the **same computer** again → companion loads your previous conversation automatically
- Plug into a **new computer** → fresh start (no history file there)
- Press the button on the device → clears current conversation and the history file

---

## Hardware

- [M5Stack AtomS3U](https://shop.m5stack.com/products/atoms3u) (~$15)
  - ESP32-S3 chip
  - USB-A male plug (plugs directly into any computer)
  - WS2812B RGB LED (GPIO 35)
  - Button (GPIO 41)

That's it. One component.

---

## Setup (developer)

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

### 5. (Optional) Regenerate the USB drive image

If you want to change the files on the USB drive:
```bash
python3 tools/make_disk.py > disk_image.h
```

### 6. Compile and upload

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

> **Port busy?** If the port is in use by a `screen` session, kill it: `pkill screen`

---

## First boot

1. Plug in the AtomS3U — LED turns **orange**
2. It creates a WiFi hotspot called **`Claude-Buddy-Setup`**
3. Connect your phone or laptop to that network
4. Open a browser and go to **`http://192.168.4.1`**
5. Enter your home/office WiFi credentials
6. Device saves them and connects — LED turns **blue**

WiFi credentials are saved to flash. On all future boots it connects automatically.

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
| Tap | Clear conversation and history file |
| Hold 3 seconds | Wipe saved WiFi (captive portal opens on next boot) |

---

## Notes

- The device needs WiFi to reach the Claude API. The host computer only needs to be on the same WiFi network.
- Conversation history in RAM is limited to 16 messages (oldest dropped when full). The disk history file has no limit.
- Works in countries/networks that can reach `api.anthropic.com`. If that's blocked (e.g., some networks), the API calls will fail.
- Max response length is 8192 tokens (~6000 words).

---

## Built with

- [M5Stack AtomS3U](https://shop.m5stack.com/products/atoms3u)
- [Arduino ESP32 core (M5Stack fork)](https://github.com/m5stack/M5Stack) 3.3.7
- [WiFiManager](https://github.com/tzapu/WiFiManager) 2.0.17
- [ArduinoJson](https://arduinojson.org/) 7.4.3
- [FastLED](https://fastled.io/) 3.10.4
- [Claude API](https://www.anthropic.com/) — `claude-sonnet-4-6`
