# 📖 Why KomaBon? (Fork Origin)

This project is a specialized branch of the excellent Book32 (originally by rolohaun, heavily optimized by qzte).

While Book32 is a fantastic general-purpose EPUB reader, KomaBon (from Koma: manga panel, and Bon: book) will be engineered specifically as a dedicated Manga reader. Designed to work in tandem with offline Python pre-processing tools that slice and dither manga pages into raw 800x480 1-bit images, bypassing the ESP32's processing limits.

**Custom Hardware Roadmap:**

* ✅ **Navigation:** Replaced the stock single-button layout with a custom 5-way SMD tactile joystick and custom ADC polling.
* ✅ **Storage:** Added an external SPI2 MicroSD module to host large image libraries safely without touching PSRAM pins.

Massive thanks to the original authors for laying down the perfect e-ink foundation!

## KomaBon

[![CI](https://github.com/alessandroabbasciano-cpu/KomaBon/actions/workflows/ci.yml/badge.svg)](https://github.com/alessandroabbasciano-cpu/KomaBon/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/alessandroabbasciano-cpu/KomaBon)](https://github.com/alessandroabbasciano-cpu/KomaBon/releases/latest)

KomaBon is a custom E-Ink application OS for the Seeed Studio XIAO ESP32-S3 TRMNL 7.5 inch OG DIY kit. It includes an EPUB reader and a local web interface for books, settings, and OTA updates.

## Hardware

* MCU: Seeed Studio XIAO ESP32-S3
* Display: 7.5 inch E-Ink panel, 800 x 480
* Storage: External MicroSD via SPI2 + Internal Flash (firmware, web UI, ebook partitions)
* Input: 5-way analog joystick + physical fallback buttons
* Battery: LiPo voltage monitoring

## Controls

**Joystick (5-way Analog on GPIO 2):**

* **Center (or KEY1):** Click: Select / OK. Long press: Return to the main menu.
* **Right:** Click: Move to the next page / Forward.
* **Left:** Click: Move to the previous page. Long press: Go back / Exit menu.
* **Up:** Click: Scroll up / Pan up.
* **Down:** Click: Scroll down / Pan down.

**KEY3 (GPIO 5):**

* Click / Long press: Dedicated Back / Abort (Essential for Joystick Calibration wizard).

**KEY2 (GPIO 3):**

* Click: Force a full display refresh, clearing accumulated e-ink ghosting.
* Long press: Enter standby. Press KEY3 to wake.

## Wiring

| Module | Function | XIAO ESP32-S3 Pin |
| --- | --- | --- |
| **E-Ink** | VCC | 3V3 |
| E-Ink | GND | GND |
| E-Ink | DIN (MOSI) | GPIO 9 |
| E-Ink | CLK (SCK) | GPIO 7 |
| E-Ink | CS | GPIO 44 |
| E-Ink | DC | GPIO 10 |
| E-Ink | RST | GPIO 38 |
| E-Ink | BUSY | GPIO 4 |
| **Inputs** | KEY3 (Back/Wake) | GPIO 5 |
| Inputs | KEY2 (Refresh/Sleep) | GPIO 3 |
| Inputs | KEY1 / Joy Center | GPIO 2 (Shared ADC) |
| Inputs | Joystick Output | GPIO 2 (Analog 0-3.3V) |
| **Battery** | Voltage ADC | GPIO 1 |
| Battery | Switch | GPIO 6 |
| **MicroSD** | CS | GPIO 39 |
| MicroSD | SCK | GPIO 41 |
| MicroSD | MOSI | GPIO 42 |
| MicroSD | MISO | GPIO 8 |

## Install From A Browser

If KomaBon is already flashed and you just want to update it, the quickest
path is the [KomaBon Browser Installer](https://alessandroabbasciano-cpu.github.io/KomaBon/). It
works in desktop Chrome or Edge with a data-capable USB cable and does not
require PlatformIO or a local development environment. It only writes the
firmware and web UI partitions — uploaded ebooks and settings are untouched.

Flashing brand-new or blank hardware for the first time still needs
PlatformIO; see below.

## Install PlatformIO

The easiest path is Visual Studio Code plus the PlatformIO extension.

1. Install Visual Studio Code.
2. Install the PlatformIO IDE extension.
3. Install Git if it is not already installed.
4. Clone this repo:

```powershell
git clone https://github.com/alessandroabbasciano-cpu/KomaBon.git
cd KomaBon
```

You can also use PlatformIO from the command line:

```powershell
python -m pip install platformio
```

## Flash KomaBon To The TRMNL Kit

Connect the XIAO ESP32-S3 to your computer over USB. From the repo folder, flash
the firmware:

```powershell
python -m platformio run --target upload
```

Then flash the web interface filesystem:

```powershell
python -m platformio run --target uploadfs
```

The ebook storage partition is separate. These commands update firmware and the
web UI, but they do not erase uploaded ebooks.

On a brand-new board, the firmware creates the ebook filesystem on first boot.
After the first successful boot, the web interface should report roughly 10 MB
of ebook storage. If it reports 0 bytes, confirm that the firmware was flashed
from this project so the custom `partitions_16MB.csv` partition table was
installed.

To watch boot logs:

```powershell
python -m platformio device monitor
```

If upload fails because the board is not in bootloader mode, hold BOOT, tap
RESET, then run the upload command again.

## First Boot

1. Power on KomaBon.
2. If WiFi is not configured, connect to the `KomaBon-Setup` access point.
3. Open `192.168.4.1` if the setup portal does not open automatically.
4. Choose your WiFi network and enter the password.
5. After connection, KomaBon shows its IP address on the main menu.
6. Open `http://<KOMABON_IP>/` in a browser to manage books and settings.

## Access Control

The web interface asks for no login: every endpoint is open to any
client that can reach the device on port 80. The only remaining barrier is the
network itself — your router's password on the home LAN, or the WPA2 passphrase
shown on the e-ink footer while the `KomaBon` hotspot is up. Do not expose the
device to an untrusted network or forward port 80 to it.

## OTA Updates

KomaBon uses the public GitHub release feed:

```text
https://github.com/alessandroabbasciano-cpu/KomaBon/releases/latest
```

No GitHub personal access token is required. Every release published by
`.github/workflows/release.yml` includes:

* `firmware.bin`
* `littlefs.bin`
* a SHA-256 checksum for each asset in the release notes

The device downloads those public release assets directly when you run an update
from the web interface or the device menu, and refuses to install an asset
whose checksum is missing or does not match — see [Releases](https://github.com/alessandroabbasciano-cpu/KomaBon/releases)
for the published versions. A brand-new board flashed by USB with an older
firmware picks up subsequent releases over the air automatically; there is no
separate bootstrap step.

## Useful PlatformIO Commands

Build firmware:

```powershell
python -m platformio run
```

Build the web UI filesystem image:

```powershell
python -m platformio run --target buildfs
```

Flash firmware:

```powershell
python -m platformio run --target upload
```

Flash web UI:

```powershell
python -m platformio run --target uploadfs
```

Open serial monitor:

```powershell
python -m platformio device monitor
```

## Features

* **NEW:** Built-in Hardware Calibration Wizard for the 5-way analog joystick.
* Polished boot screen with E-Ink progress feedback.
* EPUB reader with per-book reading progress and boot resume.
* Library state export/import (progress, book names and manual order).
* Library menu optimized for E-Ink.
* Local web interface for uploading and deleting books.
* Battery indicator and charging status.
* Public GitHub OTA firmware and web UI updates.

## Partition Notes

KomaBon uses a custom partition table. The ebook partition is mounted separately
from the firmware and web UI filesystem, so normal firmware and `uploadfs`
updates do not overwrite user ebook storage.

Fresh hardware setup uses three pieces:

* `python -m platformio run --target upload` flashes the bootloader, firmware,
  and the custom partition table.
* `python -m platformio run --target uploadfs` flashes the 1 MB LittleFS web UI
  partition named `spiffs`.
* The 10 MB `ebooks` partition is not flashed by PlatformIO. KomaBon formats it
  automatically the first time it sees that partition is blank.
