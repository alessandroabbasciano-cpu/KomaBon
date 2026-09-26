#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Core/InputMgr.h"
#include "../KomaBon_Core/FontMgr.h"
#include "../KomaBon_Core/BookMeta.h"
#include "../KomaBon_Web/WebMgr.h"
#include "../KomaBon_Core/DeviceCred.h"
#include "../KomaBon_Core/KomaBonFS.h"
#include "AppReader/AppReader.h"
#include "AppReader/EpubLoader.h"
#include "AppReader/KBReader.h"
#include "../../include/Config.h"
#include "../../include/NetworkState.h"
#include <WiFi.h>

struct MenuDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

// Dynamic bounds calculation adaptive for Portrait (480x800) and Landscape (800x480)
static MenuDirtyRect menuItemRect(int index, int screenW, int screenH, int numApps) {
    bool isPortrait = screenH > screenW;
    int ROW_HEIGHT = isPortrait ? 80 : 50;
    int numAppItems = numApps - 1;
    int START_Y = screenH - 70 - (numAppItems * ROW_HEIGHT); // Anchored to bottom above footer

    if (index == 0) {
        int wy = isPortrait ? 110 : 50;
        int wh = isPortrait ? 220 : 175;
        return {10, wy, screenW - 20, wh};
    }

    int idx = index - 1;
    int y = START_Y + idx * ROW_HEIGHT;
    return {10, y - 5, screenW - 20, ROW_HEIGHT + 10};
}

static MenuDirtyRect unionRect(MenuDirtyRect a, MenuDirtyRect b) {
    int x1 = std::min(a.x, b.x);
    int y1 = std::min(a.y, b.y);
    int x2 = std::max(a.x + a.w, b.x + b.w);
    int y2 = std::max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

static bool isReaderActive() {
    App* current = AppMgr::getInstance().getCurrentApp();
    return current && strcmp(current->getName(), "eReader") == 0;
}

void AppMainMenu::loadResumeData() {
    String lastKey = ProgressStore::getInstance().lastBook();
    if (lastKey.length() > 0) {
        _hasResume = true;
        _lastBookTitle = lastKey;

        BookProgress prog;
        if (ProgressStore::getInstance().get(lastKey, prog)) {
            _lastBookPage = prog.globalPage;
        }
    } else {
        _hasResume = false;
    }
}

void AppMainMenu::startHotspot() {
    if (_hotspotActive) return;
    if (isReaderActive()) return;

    Serial.println("Main menu: starting KomaBon management hotspot (offline)");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, WebMgr::devicePassword());
    delay(100);
    WebMgr::getInstance().connectWiFi();
    WebMgr::getInstance().startServer();
    _hotspotActive = true;

    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = !_firstDraw;
    _needsRedraw = true;
}

void AppMainMenu::stopHotspot() {
    if (!_hotspotActive) return;

    Serial.println("Main menu: stopping management hotspot");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    WebMgr::getInstance().stopNetwork();
    _hotspotActive = false;
}

void AppMainMenu::start() {
    loadResumeData();
    selectedIndex = _hasResume ? 0 : 1;

    _needsRedraw = true;
    _firstDraw = true;
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;

    _lastBatteryPoll = millis();
    _lastBatteryStatus = BatteryMgr::getInstance().refreshNow();

    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
}

void AppMainMenu::stop() {
    stopHotspot();
}

void AppMainMenu::forceRedraw() {
    _firstDraw = true;
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
    _needsRedraw = true;
}

void AppMainMenu::handleInput(InputAction action) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    int minSelectable = _hasResume ? 0 : 1;
    int maxSelectable = apps.size() - 1;

    if (action == INPUT_NEXT || action == INPUT_RIGHT) {
        selectedIndex++;
        if (selectedIndex > maxSelectable) selectedIndex = minSelectable;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    } else if (action == INPUT_PREV || action == INPUT_LEFT) {
        selectedIndex--;
        if (selectedIndex < minSelectable) selectedIndex = maxSelectable;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    } else if (action == INPUT_SELECT) {
        if (selectedIndex == 0 && _hasResume) {
            for (App* app : apps) {
                if (app && strcmp(app->getName(), "Bookshelf") == 0) {
                    AppReader* reader = static_cast<AppReader*>(app);
                    reader->resumeSavedBookOnStart();
                }
            }
            ProgressStore::getInstance().setResumeOnBoot(true);
            appMgr.switchTo(1);
        } else if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    }
}

void AppMainMenu::update() {
    unsigned long now = millis();

    if (now - _lastBatteryPoll >= 10000) {
        _lastBatteryPoll = now;
        BatteryStatus status = BatteryMgr::getInstance().refreshNow();
        bool changed = status.charging != _lastBatteryStatus.charging ||
                       status.percentage != _lastBatteryStatus.percentage ||
                       fabsf(status.voltage - _lastBatteryStatus.voltage) >= 0.03f;
        if (changed) {
            _lastBatteryStatus = status;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = !_firstDraw;
            _needsRedraw = true;
        }
    }
}

void AppMainMenu::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    int16_t screenW = display.width();
    int16_t screenH = display.height();
    bool isPortrait = screenH > screenW;

    if (_firstDraw) {
        if (millis() < 8000) {
            display.setPartialWindow(0, 0, display.width(), display.height());
        } else {
            display.setFullWindow();
        }
        _firstDraw = false;
    } else if (_selectionOnlyRedraw) {
        MenuDirtyRect dirty = unionRect(menuItemRect(_previousSelectedIndex, screenW, screenH, apps.size()),
                                        menuItemRect(selectedIndex, screenW, screenH, apps.size()));
        dirty.x = std::max(0, dirty.x);
        dirty.y = std::max(0, dirty.y);
        if (dirty.x + dirty.w > screenW) dirty.w = screenW - dirty.x;
        if (dirty.y + dirty.h > screenH) dirty.h = screenH - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else if (_batteryOnlyRedraw) {
        display.setPartialWindow(screenW - 150, 0, 150, 42);
    } else if (_footerOnlyRedraw) {
        display.setPartialWindow(0, screenH - 70, screenW, 70);
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
    }

    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // --- 1. HEADER ---
        fontMgr.drawText(display, "KomaBon", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int komaBonWidth = fontMgr.getTextWidth("KomaBon", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + komaBonWidth, 35, FONT_SIZE_SMALL, GxEPD_BLACK);

        BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

        // --- 2. WIDGET: CURRENTLY READING (Elegant Hero Card) ---
        if (_hasResume) {
            int wx = 15;
            int wy = isPortrait ? 110 : 50;
            int ww = screenW - 30;
            int wh = isPortrait ? 220 : 175;

            // Clear Background
            display.fillRect(wx, wy, ww, wh, GxEPD_WHITE);

            // Left Sidebar Highlight
            if (selectedIndex == 0) {
                display.fillRect(wx, wy + 10, 8, wh - 20, GxEPD_BLACK);
            }

            int textLeftOffset = wx + 24;
            fontMgr.drawText(display, "Currently Reading", textLeftOffset, wy + 24, FONT_SIZE_BODY,
                             GxEPD_BLACK);
            display.drawFastHLine(textLeftOffset, wy + 32, 120, GxEPD_BLACK);

            String realFilename = findFilenameForOriginal(_lastBookTitle);
            if (realFilename.length() == 0) realFilename = _lastBookTitle;

            String baseName = realFilename;
            int dot = baseName.lastIndexOf('.');
            if (dot > 0) baseName = baseName.substring(0, dot);
            String coverPath = "/covers/" + baseName + ".cover";

            if (!SystemFS.exists(coverPath)) {
                if (!SystemFS.exists("/covers")) SystemFS.mkdir("/covers");
                if (realFilename.endsWith(".kmb")) {
                    KBReader* kb = new KBReader();
                    if (kb->open(("/ebooks/" + realFilename).c_str()) || kb->open(realFilename.c_str())) {
                        uint16_t w = kb->getWidth();
                        uint16_t h = kb->getHeight();
                        size_t bufSize = static_cast<size_t>(w + 7) / 8 * h;
                        uint8_t* pageBuf = (uint8_t*)ps_malloc(bufSize);
                        if (pageBuf && kb->readPage(0, pageBuf)) {
                            uint8_t cover[2400] = {0};
                            for (int ty = 0; ty < 160; ty++) {
                                int sy = ty * h / 160;
                                for (int tx = 0; tx < 120; tx++) {
                                    int sx = tx * w / 120;
                                    int srcByte = sy * ((w + 7) / 8) + (sx / 8);
                                    int srcBit = 7 - (sx % 8);
                                    if (pageBuf[srcByte] & (1 << srcBit)) {
                                        cover[ty * 15 + (tx / 8)] |= (1 << (7 - (tx % 8)));
                                    }
                                }
                            }
                            File f = SystemFS.open(coverPath, "w");
                            if (f) {
                                f.write(cover, 2400);
                                f.close();
                            }
                        }
                        if (pageBuf) free(pageBuf);
                    }
                    delete kb;
                } else {
                    EpubLoader* epub = new EpubLoader();
                    if (epub->open(("/ebooks/" + realFilename).c_str()) || epub->open(realFilename.c_str())) {
                        size_t coverSize = 0;
                        uint8_t* coverData = epub->getRawZipData("cover_main.raw", &coverSize);
                        if (coverData && coverSize == 2400) {
                            File f = SystemFS.open(coverPath, "w");
                            if (f) {
                                f.write(coverData, 2400);
                                f.close();
                            }
                        }
                        if (coverData) free(coverData);
                    }
                    delete epub;
                }
            }

            int coverX = textLeftOffset;
            int coverY = wy + 45;
            int coverW = 120;
            int coverH = 160;

            bool coverDrawn = false;
            if (SystemFS.exists(coverPath)) {
                File f = SystemFS.open(coverPath, "r");
                if (f) {
                    uint8_t coverBuf[2400];
                    if (f.read(coverBuf, 2400) == 2400) {
                        coverDrawn = true;
                        display.drawBitmap(coverX, coverY, coverBuf, coverW, coverH, GxEPD_BLACK);
                    }
                    f.close();
                }
            }

            if (!coverDrawn) {
                display.drawRect(coverX, coverY, coverW, coverH, GxEPD_BLACK);
                fontMgr.drawText(display, "No Cover", coverX + 22, coverY + 75, FONT_SIZE_BODY, GxEPD_BLACK);
            }

            int textX = coverX + coverW + 25;
            int textMaxWidth = wx + ww - textX - 10;

            String cleanTitle = _lastBookTitle;
            if (cleanTitle.lastIndexOf('.') > 0) {
                cleanTitle = cleanTitle.substring(0, cleanTitle.lastIndexOf('.'));
            }
            cleanTitle.replace('_', ' ');

            int dashPos = cleanTitle.indexOf(" - ");
            String author = "";
            String bookName = cleanTitle;

            if (dashPos != -1) {
                author = cleanTitle.substring(0, dashPos);
                bookName = cleanTitle.substring(dashPos + 3);
            }

            if (author.length() > 38) author = author.substring(0, 35) + "...";

            int authorY = coverY + 20;
            if (dashPos != -1) {
                fontMgr.drawText(display, author.c_str(), textX, authorY, FONT_SIZE_BODY, GxEPD_BLACK);
            }

            int titleY = dashPos != -1 ? (authorY + 26) : (coverY + 34);
            int maxLines = 2;
            int lineCount = 0;
            String currentLine = "";
            int pos = 0;
            int bookNameLen = bookName.length();

            while (pos < bookNameLen && lineCount < maxLines) {
                int nextSpace = bookName.indexOf(' ', pos);
                if (nextSpace == -1) nextSpace = bookNameLen;
                String word = bookName.substring(pos, nextSpace);
                String testLine = currentLine.length() > 0 ? currentLine + " " + word : word;

                if (fontMgr.getTextWidthBold(testLine.c_str(), FONT_SIZE_MENU) > textMaxWidth &&
                    currentLine.length() > 0) {
                    fontMgr.drawTextBold(display, currentLine.c_str(), textX, titleY, FONT_SIZE_MENU,
                                         GxEPD_BLACK);
                    titleY += 26;
                    lineCount++;
                    currentLine = word;
                    if (lineCount >= maxLines) break;
                } else {
                    currentLine = testLine;
                }
                pos = nextSpace + 1;
                if (pos > bookNameLen) break;
            }

            if (currentLine.length() > 0 && lineCount < maxLines) {
                if (pos < bookNameLen && currentLine.length() > 3) {
                    currentLine = currentLine.substring(0, currentLine.length() - 3) + "...";
                }
                fontMgr.drawTextBold(display, currentLine.c_str(), textX, titleY, FONT_SIZE_MENU,
                                     GxEPD_BLACK);
            }

            int pageY = coverY + 140;
            char pageInfo[32];
            snprintf(pageInfo, sizeof(pageInfo), "Page %d", _lastBookPage);
            fontMgr.drawText(display, pageInfo, textX, pageY, FONT_SIZE_BODY, GxEPD_BLACK);
        }

        // --- 3. VERTICAL LIST APPS (Bottom Anchored) ---
        int ROW_HEIGHT = isPortrait ? 80 : 50;
        int numAppItems = apps.size() - 1;
        int START_Y = screenH - 70 - (numAppItems * ROW_HEIGHT);

        for (size_t i = 1; i < apps.size(); i++) {
            App* app = apps[i];
            int idx = i - 1;
            int y = START_Y + idx * ROW_HEIGHT;
            int x = 35;

            display.fillRect(15, y - 5, screenW - 30, ROW_HEIGHT, GxEPD_WHITE);

            // Left Sidebar Highlight
            if ((int)i == selectedIndex) {
                display.fillRect(15, y + 3, 6, ROW_HEIGHT - 10, GxEPD_BLACK);
            }

            const uint8_t* icon = app->getIconImage();
            int iconSize = isPortrait ? 60 : 42;

            if (icon) {
                for (int cy = 0; cy < iconSize; cy++) {
                    int srcY = (cy * 160) / iconSize;
                    for (int cx = 0; cx < iconSize; cx++) {
                        int srcX = (cx * 160) / iconSize;
                        int byteIdx = (srcY * 160 + srcX) / 8;
                        int bitIdx = 7 - ((srcY * 160 + srcX) % 8);

                        if (icon[byteIdx] & (1 << bitIdx)) {
                            display.drawPixel(x + cx, y + cy, GxEPD_BLACK);
                        }
                    }
                }
            }

            // Visual Override for "Web Transfer"
            String dispName = app->getName();
            if (dispName == "Web Transfer") dispName = "Web UI";

            int textY = y + (iconSize / 2) + 8;
            fontMgr.drawText(display, dispName.c_str(), x + iconSize + 25, textY, FONT_SIZE_BODY,
                             GxEPD_BLACK);
        }

        // --- 4. FOOTER ---
        fontMgr.drawTextCentered(display, "Joy: Move  |  Center: Select", screenH - 22, FONT_SIZE_SMALL,
                                 GxEPD_BLACK);

    } while (display.nextPage());
}