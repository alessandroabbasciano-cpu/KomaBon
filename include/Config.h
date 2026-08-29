#pragma once

// System Information
// Major.Minor.Patch, always with all three components: release.yml only triggers
// on v[0-9]+.[0-9]+.[0-9]+ tags and compares the tag with this value character
// by character. "1.10" here (or a v1.10 tag) would not publish any release.
#define SYSTEM_VERSION "0.1.0-beta"
#define DEVICE_NAME "KomaBon"

// Offline management hotspot (SoftAP). When the device can't reach a known
// WiFi network, the main menu broadcasts this network so a phone can connect
// directly and reach the web interface at 192.168.4.1 (no router needed).
#define AP_SSID "KomaBon"

// Pin Definitions for Seeed XIAO ESP32-S3 (TRMNL 7.5" OG DIY Kit)
#define PIN_BAT_VOLT 1
#define PIN_VBAT_SWITCH 6
#define VBAT_SWITCH_LEVEL HIGH
// v1.10.4: reverts the v1.10.3 swap. That swap was a mistake - see the
// postmortem in docs/plans/2026-07-26-key1-key3-standby-diagnostics.md. This
// is the kit wiki's nominal assignment (KEY1=GPIO2, KEY2=GPIO3, KEY3=GPIO5),
// confirmed correct by the user's own first log capture: a clean KEY3/GPIO5
// press producing INPUT_NEXT, captured before any pin change ever shipped.
// ==========================================
// INPUT PINS (Joystick & Buttons)
// ==========================================
#define JOY_ADC_PIN 2      // ADC1_CH1: Analog Joystick & Physical KEY1
#define PIN_BUTTON_BACK 5  // "KEY3" button - now used as Back button
#define PIN_BUTTON_SLEEP 3 // "KEY2" button - full refresh (click) or standby

// Display Pins (TRMNL 7.5" OG DIY Kit)
#define EPD_SCK 7
#define EPD_MOSI 9
#define EPD_MISO -1
#define EPD_CS 44
#define EPD_DC 10
#define EPD_RST 38
#define EPD_BUSY 4

// ==========================================
// MICRO SD CARD PINS (SPI2 Hardware)
// ==========================================
#define SD_CS_PIN 39
#define SD_SCK_PIN 41
#define SD_MOSI_PIN 42
#define SD_MISO_PIN 8

#define SD_INIT_FREQ 400000   // 400 kHz for the initial security handshake
#define SD_FAST_FREQ 16000000 // 16 MHz for data transfer

// Display Settings (Portrait 7.5")
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 800
#define FONT_SIZE_DEFAULT 28 // Default font size (maps to FreeSans18pt GFX font)

// Boot diagnostics
// Set to 1 when debugging partition/filesystem issues. Keeping this off makes
// normal startup quieter and avoids walking the ebook filesystem every boot.
#define KOMABON_VERBOSE_BOOT_LOG 0

// Manual standby (KEY2 long press, ~1.5s — see StandbyGuard.h). At 0, only
// the automatic idle sleep stays active (BatteryMgr's inactivity timeout,
// configurable in Settings); holding KEY2 no longer puts the device to
// sleep, but a short click still triggers a full refresh. Set to 1 to bring
// the manual long-press standby back.
#define KOMABON_KEY2_STANDBY_ENABLED 0

// Battery calibration
// Fully charged LiPo cells should read 4.20V. The previous value here (1.075)
// compensated for analogRead()'s raw, uncalibrated 12-bit-to-voltage math,
// which read a known-full pack around 3.91V. BatteryMgr now reads the ADC via
// analogReadMilliVolts(), which applies the ESP32-S3's factory eFuse
// calibration curve and removes most of that error on its own, so the trim
// factor needed here should be much smaller. Re-check against a multimeter on
// real hardware and adjust — this default is a starting point, not measured.
#define BATTERY_VOLTAGE_CALIBRATION 1.0f
#define BATTERY_EMPTY_VOLTAGE 3.00f
#define BATTERY_FULL_VOLTAGE 4.20f

// GitHub OTA Config
// IMPORTANT: this fork publishes its own releases. Do not revert these to the
// upstream repository or the device will check for updates
// against a repo that this firmware does not track.
#define GITHUB_REPO "alessandroabbasciano-cpu/KomaBon"
#define GITHUB_USER "alessandroabbasciano-cpu"
