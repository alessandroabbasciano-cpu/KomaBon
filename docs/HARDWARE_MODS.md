# 🛠️ KomaBon Hardware Modding & Preparation Guide

This document describes the physical wiring, electrical layout, and low-level silicon configuration required to build a KomaBon e-reader using the **TRMNL 7.5" DIY Kit (Seeed Studio XIAO ESP32-S3 Plus)**.

---

## 1. Electrical & Silicon Constraints

### High-Frequency Octal PSRAM Isolation

The Seeed XIAO ESP32-S3 Plus features 8MB Octal PSRAM clocked at 120MHz over **GPIO33 through GPIO37** (`qio_opi`).

* **Absolute Rule:** Never solder to, tap into, or declare GPIO33–37 in firmware.
* Interfacing with these lines results in an immediate Kernel Panic, Memory Watchdog Reset, or permanent physical damage to the internal memory bus.

### SPI Bus Segregation

The carrier board uses the native SPI0 bus exclusively for the 7.5-inch e-ink panel (`GDEY075T7`) via GPIO4, 7, 9, 10, 38, and 44. To maintain display speed and prevent bus conflicts, the MicroSD runs on a dedicated hardware SPI2 master routed to alternate pads.

---

## 2. Mandatory Step: Burning the `DIS_PAD_JTAG` eFuse

### The Problem

On ESP32-S3 silicon, `GPIO39` (MTCK), `GPIO41` (MTDI), and `GPIO42` (MTMS) are internally routed to the native USB-Serial-JTAG debugging controller. When connected to a computer via USB or reset via software (`rst:0x15 USB_UART_CHIP_RESET`), the debug controller takes over these lines, corrupting SPI transactions and locking the MicroSD card state machine into an unrecoverable fault state (`GO_IDLE_STATE failed` or token errors).

### The Fix

Burning the `DIS_PAD_JTAG` eFuse permanently severs the physical silicon multiplexer linking the JTAG subsystem to those external GPIO pads, granting the SPI2 driver exclusive access.

* **Impact on Device:**
  * Firmware flashing via USB-CDC continues working normally.
  * Serial monitor logging at 115200 baud is completely unaffected.
  * OTA Wi-Fi updates remain fully functional.
  * The only feature permanently disabled is hardware breakpoint debugging via OpenOCD on those specific physical pads.
* **Warning:** eFuses are **One-Time Programmable (OTP)**. Once burned, this operation is physical and cannot be undone.

### Step-by-Step Burn Procedure

1. **Install Espressif Tools:**  
   Open your terminal in the active environment and install the required tools:

   ```powershell
   pip install esptool
   ```

2. **Verify Target Board (Read-Only Check):**  
   Close all active serial monitors and query the chip's eFuse summary:

   ```powershell
   python -m espefuse --port <COM_PORT> summary
   ```

   In the output table under **Jtag fuses**, confirm that the target bit is unburned:

   ```text
   DIS_PAD_JTAG (BLOCK0) = False R/W (0b0)
   ```

3. **Burn the eFuse:**  
   Run the burning command targeting the pad multiplexer:

   ```powershell
   python -m espefuse --port <COM_PORT> burn_efuse DIS_PAD_JTAG
   ```

   When prompted by the terminal with the irreversibility warning, type `BURN` in uppercase and press Enter.

4. **Cold Hardware Boot:**  
   Unplug the USB cable completely, wait 5 seconds, and plug it back in. The SPI2 pads are now permanently freed from debug multiplexing.

---

## 3. Physical Wiring & Pinout Matrix

### Pin Connections

| Module | Signal | Physical Source Pad / Pin | ESP32-S3 GPIO | Recommendation |
| :--- | :--- | :--- | :--- | :--- |
| **MicroSD** | VCC | Pin 12 (VCC_3V3) on XIAO | 3.3V Power | Solid 3.3V line |
| **MicroSD** | GND | Pin 13 (GND) on XIAO | Ground | Common system ground |
| **MicroSD** | MISO | Castellation Pin 10 | GPIO 8 | Internal pull-up enabled in firmware |
| **MicroSD** | SCK | Pad RX1 / NFC1 (Connector J4) | GPIO 41 | Must be twisted with GND lead |
| **MicroSD** | MOSI | Pad TX1 / NFC2 (Connector J4) | GPIO 42 | Keep lead short |
| **MicroSD** | CS | Pin 16 on XIAO (SPI1_CS0 net) | GPIO 39 | Solder directly to castellation (Alt: Pad 1 U6) |
| **Joystick** | SIG | Pad KEY1 Switch (BUTON1 net) | GPIO 2 | Analog ADC1_CH1 ladder |
| **Joystick** | GND | Pad KEY1 Ground | Ground | Connect to joystick ground pad |

### Physical Wiring Diagram

[ XIAO ESP32-S3 PLUS MODULE ]
  Pin 12 (VCC_3V3) ──────────────────────────> MicroSD VCC
  Pin 13 (GND)     ──────────────────────────> MicroSD GND
  Pin 16 (GPIO39)  ──────────────────────────> MicroSD CS
  Castellation Pin 10 (GPIO8)  ──────────────> MicroSD MISO

[ TRMNL CARRIER BOARD PADS ]
  Pad RX1 / NFC1 (GPIO41) ───────────────────> MicroSD SCK  (Twisted with GND)
  Pad TX1 / NFC2 (GPIO42) ───────────────────> MicroSD MOSI
  Pad KEY1 Signal (GPIO2) ───────────────────> 5-Way Joystick Signal (ADC1_CH1)
  Pad KEY1 Ground ───────────────────────────> 5-Way Joystick GND

---

## 4. Signal Integrity & EMI Shielding Guidelines

The high-frequency SPI bus running alongside the switching battery charger PMIC (ETA6003 or SY6974B) requires strict cabling discipline to prevent runtime token errors:

* **Keep Wires Short:** Cut all flying leads so they are **under 5–6 cm** in length. Long leads act as inductive loop antennas that pick up DC-DC switching noise.
* **Twist Clock and Data Lines:** Twist the **SCK (GPIO41)** wire together with a dedicated GND wire from end to end. If possible, also twist **MISO (GPIO8)** with GND.
* **Local Power Decoupling:** Solder a **100nF or 1µF ceramic capacitor** directly across the VCC and GND pins of the MicroSD slot to suppress voltage dips during flash write cycles.
* **Avoid Inductor Proximity:** Route the entire MicroSD wire bundle away from inductor **L1** (marked 2R2 or 1R0) located near the battery connector on the carrier board.
* **Joystick Analog Domain:** Ensure GPIO2 is configured strictly as an analog input (`ADC1_CH1`). Digital input pull-ups on GPIO2 must remain disabled to prevent internal shoot-through leakage currents.
