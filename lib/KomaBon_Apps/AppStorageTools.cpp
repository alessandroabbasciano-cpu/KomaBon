#include "AppStorageTools.h"
#include "icon_storage.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../KomaBon_Core/FontMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Core/SDMgr.h"
#include "../KomaBon_Core/KomaBonFS.h"
#include "../KomaBon_Core/ProgressStore.h"
#include <LittleFS.h>

static const int LIST_START_Y = 130;
static const int ROW_HEIGHT = 48;
static const int TOTAL_ACTIONS = 5;

static const char* ACTIONS[TOTAL_ACTIONS] = {"Remount MicroSD", "Restart Device", "Reset Progress (JSON Fix)",
                                             "Clear Covers Cache", "Back to Main Menu"};

struct StorageDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static StorageDirtyRect storageRowRect(int index, int screenW, int startY) {
    int y = startY + index * ROW_HEIGHT;
    return {12, y - 30, screenW - 24, ROW_HEIGHT - 8};
}

static StorageDirtyRect unionStorageRect(StorageDirtyRect a, StorageDirtyRect b) {
    int x1 = std::min(a.x, b.x);
    int y1 = std::min(a.y, b.y);
    int x2 = std::max(a.x + a.w, b.x + b.w);
    int y2 = std::max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

const uint8_t* AppStorageTools::getIconImage() {
    return icon_storage_bits;
}

void AppStorageTools::start() {
    _selectedIndex = 0;
    _previousSelectedIndex = 0;
    _needsRedraw = true;
    _selectionOnlyRedraw = false;
    _firstDraw = true;
    _statusMessage = "";

    InputMgr::getInstance().setCallback(
        std::bind(&AppStorageTools::handleInput, this, std::placeholders::_1));
}

void AppStorageTools::stop() {
    // Release active input callback
}

void AppStorageTools::forceRedraw() {
    _firstDraw = true;
    _selectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppStorageTools::handleInput(InputAction action) {
    if (action == INPUT_NEXT || action == INPUT_RIGHT) {
        _previousSelectedIndex = _selectedIndex;
        _selectedIndex = (_selectedIndex + 1) % TOTAL_ACTIONS;
        _selectionOnlyRedraw = true;
        _needsRedraw = true;
    } else if (action == INPUT_PREV || action == INPUT_LEFT) {
        _previousSelectedIndex = _selectedIndex;
        _selectedIndex = (_selectedIndex - 1 + TOTAL_ACTIONS) % TOTAL_ACTIONS;
        _selectionOnlyRedraw = true;
        _needsRedraw = true;
    } else if (action == INPUT_SELECT) {
        executeSelection();
    } else if (action == INPUT_BACK) {
        AppMgr::getInstance().switchTo("Main Menu");
    }
}

void AppStorageTools::executeSelection() {
    switch (_selectedIndex) {
        case 0:
            remountSD();
            break;
        case 1:
            cleanReboot();
            break;
        case 2:
            resetReadingProgress();
            break;
        case 3:
            purgeThumbnails();
            break;
        case 4:
            AppMgr::getInstance().switchTo("Main Menu");
            break;
    }
}

void AppStorageTools::remountSD() {
    bool ok = SDMgr::getInstance().remountManual();

    if (ok) {
        // Re-synchronize reading records from the newly mounted filesystem
        ProgressStore::getInstance().reload();

        size_t total = KomaBonStorage::getTotalBytes() / (1024 * 1024);
        _statusMessage = "SD Online: " + String((uint32_t)total) + " MB Total";
    } else {
        _statusMessage = "Mount Failed. Disconnect USB or restart.";
    }

    _selectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppStorageTools::cleanReboot() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        FontMgr::getInstance().drawTextCentered(display, "Restarting KomaBon...", display.height() / 2,
                                                FONT_SIZE_BODY, GxEPD_BLACK);
    } while (display.nextPage());

    delay(250);
    ESP.restart();
}

void AppStorageTools::resetReadingProgress() {
    bool r1 = EbookFS.remove("/reader_progress.json");
    bool r2 = SystemFS.remove("/reader_progress.json");
    bool r3 = SystemFS.remove("/page_totals.json");

    if (r1 || r2 || r3) {
        _statusMessage = "Progress and totals JSON reset.";
    } else {
        _statusMessage = "No progress files found.";
    }

    _selectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppStorageTools::purgeThumbnails() {
    File root = SystemFS.open("/covers");
    int count = 0;
    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            String path = file.path();
            file.close();
            SystemFS.remove(path);
            count++;
            file = root.openNextFile();
        }
        root.close();
    }

    _statusMessage = "Purged " + String(count) + " cached covers.";
    _selectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppStorageTools::update() {
    // Passive update
}

void AppStorageTools::drawHeader(const char* title) {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

    font.drawText(display, title, 20, 45, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
    display.drawLine(20, 62, display.width() - 20, 62, GxEPD_BLACK);
}

void AppStorageTools::drawFooter(const char* hint) {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int h = display.height();

    if (_statusMessage.length() > 0) {
        font.drawTextCentered(display, _statusMessage.c_str(), h - 50, FONT_SIZE_SMALL, GxEPD_BLACK);
    }
    font.drawTextCentered(display, hint, h - 22, FONT_SIZE_SMALL, GxEPD_BLACK);
}

void AppStorageTools::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    int w = display.width();
    int actionsStartY = LIST_START_Y + (ROW_HEIGHT * 3) + 20;

    if (_selectionOnlyRedraw && !_firstDraw) {
        StorageDirtyRect dirty = unionStorageRect(storageRowRect(_previousSelectedIndex, w, actionsStartY),
                                                  storageRowRect(_selectedIndex, w, actionsStartY));
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        display.setFullWindow();
        _firstDraw = false;
    }

    _selectionOnlyRedraw = false;
    _previousSelectedIndex = _selectedIndex;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        drawHeader("Storage Tools");

        // --- 1. TELEMETRY PANEL (Settings Style) ---
        int y = LIST_START_Y;
        bool sdMounted = SDMgr::getInstance().isMounted();
        String fsType = (EbookFSPtr == &SD) ? "MicroSD (SPI2)" : "Internal Flash";
        size_t usedMB = KomaBonStorage::getUsedBytes() / (1024 * 1024);
        size_t totalMB = KomaBonStorage::getTotalBytes() / (1024 * 1024);
        String capStr = String((uint32_t)usedMB) + " / " + String((uint32_t)totalMB) + " MB";

        font.drawText(display, "Storage:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
        font.drawText(display, fsType.c_str(), 170, y, FONT_SIZE_BODY, GxEPD_BLACK);
        y += ROW_HEIGHT;

        font.drawText(display, "Hardware:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
        font.drawText(display, sdMounted ? "Mounted (Online)" : "Offline / Bus Error", 170, y, FONT_SIZE_BODY,
                      GxEPD_BLACK);
        y += ROW_HEIGHT;

        font.drawText(display, "Capacity:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
        font.drawText(display, capStr.c_str(), 170, y, FONT_SIZE_BODY, GxEPD_BLACK);

        // --- 2. ACTIONS LIST (Settings Double-Box Highlighting) ---
        for (int i = 0; i < TOTAL_ACTIONS; i++) {
            int ay = actionsStartY + i * ROW_HEIGHT;

            if (i == _selectedIndex) {
                display.drawRect(12, ay - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
                display.drawRect(13, ay - 29, w - 26, ROW_HEIGHT - 10, GxEPD_BLACK);
            }

            font.drawText(display, ACTIONS[i], 26, ay, FONT_SIZE_BODY, GxEPD_BLACK);
        }

        drawFooter("Up/Down: move  |  Center: select  |  Hold Left: exit");

    } while (display.nextPage());
}