# 📖 KomaBon

[![CI](https://github.com/alessandroabbasciano-cpu/KomaBon/actions/workflows/ci.yml/badge.svg)](https://github.com/alessandroabbasciano-cpu/KomaBon/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/alessandroabbasciano-cpu/KomaBon)](https://github.com/alessandroabbasciano-cpu/KomaBon/releases/latest)

**KomaBon** (from *Koma*: manga panel, and *Bon*: book) is a custom E-Ink application OS engineered specifically for the Seeed Studio XIAO ESP32-S3 TRMNL 7.5-inch DIY Kit.

This project is a specialized branch built upon the foundational code of Book32 (originally by rolohaun), customized and optimized specifically as a dedicated manga and multi-format e-reader. It features an intelligent dual-file-system architecture, native Wi-Fi web asset management, a hardware calibration wizard for custom input devices, and secure manual OTA updates directly from the device settings.

---

## ⚙️ The Core Engine: Manga & Document Pre-Processing

The true beating heart of KomaBon is its **Universal Conversion Pipeline**, implemented directly in browser-side JavaScript. Because raw manga archives (CBZ/ZIP) or complex documents (PDF, ODT, EPUB) would easily cause an ESP32-S3 microcontroller to run out of memory or choke on heavy image decoding, KomaBon offloads the heavy lifting to the client browser.

### How the Converter Works

1. **Archive & Document Ingestion:** The engine unpacks CBZ/ZIP comic archives, parses PDF pages, or extracts text/images from ODT and EPUB files directly inside the browser.
2. **Smart Bounding-Box Cropping:** Automatically detects active image bounds (`getCropBounds`), stripping away unnecessary white margins to maximize the usable display area on the 7.5-inch panel.
3. **Aspect Ratio & Orientation Alignment:** Automatically handles landscape-to-portrait rotation or centers panels to fit the native **800x480** resolution.
4. **Atkinson Dithering & 1-Bit Packing:** Converts grayscale or full-color images into pure 1-bit black/white bitmaps using **Atkinson dithering**, packing pixels efficiently into custom binary payloads (`.kmb` raw comic files or optimized zero-decoding `.epub` archives).
5. **Native Dual-Thumbnails Injection:** Automatically extracts or generates high-performance 60x80 and 120x160 thumbnails (`cover_thumb.raw`, `cover_main.raw`) for instant library rendering and main menu hero cards without runtime decoding overhead.

---

## 🚀 Key Hardware Specifications

* **Microcontroller:** Seeed Studio XIAO ESP32-S3 Plus (Dual-core Xtensa LX7 @ 240MHz, 8MB Octal PSRAM, 8MB Flash)
* **Display:** 7.5-inch Monochrome E-Ink panel (`GDEY075T7`), 800x480 resolution, pure 1-bit black/white rendering for maximum contrast and instantaneous refresh rates
* **Storage Architecture:**
  * **SystemFS (Internal Flash / LittleFS):** Houses read-only system assets, the embedded web UI, and core configuration templates.
  * **EbookFS (External MicroSD via SPI2):** Dedicated high-capacity external storage for user metadata, configs, and massive manga/EPUB libraries. Fully isolated to protect user data during firmware upgrades.
* **Input System:** Custom 5-way tactile analog joystick interfaced via ADC1 (GPIO2) complemented by dedicated physical fallback buttons (KEY2/KEY3).
* **Power Management:** Rechargeable 2000mAh Li-ion battery managed via advanced power architecture (compatible with ETA6003 and SY6974B PMIC revisions) and monitored via dedicated ADC channels.

---

## 🎮 Controls & Navigation

### 5-Way Analog Joystick (GPIO 2 / ADC1)

* **Center (or KEY1):** Click to Select / Confirm. Long press to return to the main menu.
* **Right:** Next page / Forward navigation.
* **Left:** Previous page. Long press to go back or exit current view.
* **Up / Down:** Scroll vertically or pan through pages and library menus.

### Physical Function Buttons

* **KEY3 (GPIO 5):** Dedicated Back / Abort button (critical for escaping menus and running the Hardware Calibration Wizard).
* **KEY2 (GPIO 3):** Click to trigger an immediate full display refresh, clearing accumulated e-ink ghosting. *(Note: Standby hold functionality on this button is disabled in firmware; deep sleep is managed automatically via idle timers).*

---

## 🔌 Wiring & Pinout Reference

| Module | Function | XIAO ESP32-S3 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **E-Ink Display** | MOSI | GPIO 9 | SPI0 bus |
| E-Ink Display | SCK | GPIO 7 | SPI0 bus |
| E-Ink Display | CS | GPIO 44 | Chip Select |
| E-Ink Display | DC | GPIO 10 | Data/Command |
| E-Ink Display | RST | GPIO 38 | Hard Reset |
| E-Ink Display | BUSY | GPIO 4 | Hardware Busy Line |
| **Physical Inputs** | KEY3 | GPIO 5 | Back / Wake |
| Physical Inputs | KEY2 | GPIO 3 | Refresh / Ghosting Clear |
| Physical Inputs | Joystick Center | GPIO 2 | Shared ADC1 Channel |
| **MicroSD (SPI2)** | CS | GPIO 39 | Initialized High-Z at boot |
| MicroSD (SPI2) | SCK | GPIO 41 | Dedicated SPI2 Bus |
| MicroSD (SPI2) | MOSI | GPIO 42 | Dedicated SPI2 Bus |
| MicroSD (SPI2) | MISO | GPIO 8 | Internal Pull-up Enabled |
| **Power / Battery** | Voltage ADC | GPIO 1 | LiPo level monitoring |
| Power / Battery | PMIC Switch | GPIO 6 | Power Status Line |

---

## 🛠️ Installation & Development

### 1. Browser Flasher (Fastest Update Method)

If KomaBon is already installed, you can update system partitions directly from a desktop browser (Chrome or Edge) using the official [KomaBon Browser Installer](https://alessandroabbasciano-cpu.github.io/KomaBon/). This updates firmware and web assets safely without touching your EbookFS storage or user progress.

### 2. Factory Flashing via PlatformIO (VS Code)

For fresh hardware or local source development, open your terminal in the project root and run:

`git clone https://github.com/alessandroabbasciano-cpu/KomaBon.git`

`cd KomaBon`

`python -m platformio run --target upload`

`python -m platformio run --target uploadfs`

To monitor real-time serial output for debugging:

`python -m platformio device monitor`

---

## 🌐 First Boot & Access Control

1. Power on KomaBon.
2. If Wi-Fi is not configured, connect to the `KomaBon-Setup` access point.
3. Open `192.168.4.1` if the captive portal does not trigger automatically.
4. Select your local network and provide credentials.
5. Access the device's local web interface at `http://<KOMABON_IP>/` to upload books and manage settings.

---

## ✨ Core Features & Architecture

* **Hardware Calibration Wizard:** Built-in routine to map and calibrate the 5-way analog joystick thresholds accurately.
* **Dual-Filesystem Integrity:** Decouples system firmware from user books, ensuring seamless OTA updates without data loss.
* **Universal Pre-processing Pipeline:** Translates CBZ, ZIP, PDF, ODT, and EPUB files directly into lightning-fast 1-bit E-Ink formats.
* **Local Web Management:** Access the device over Wi-Fi to upload, organize, and delete books directly from your browser.
* **Robust Power States:** Intelligent sleep management and battery level indicators tailored for extended reading sessions.

---

## 📦 Partition Notes

KomaBon uses a custom partition table (`partitions_16MB.csv`). The ebook partition is mounted separately from the firmware and web UI filesystem, so normal firmware and `uploadfs` updates do not overwrite user ebook storage.
