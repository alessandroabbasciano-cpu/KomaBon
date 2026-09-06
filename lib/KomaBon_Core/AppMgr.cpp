#include "AppMgr.h"
#include "../../include/Config.h"
#include "WebMgr.h"

AppMgr::AppMgr() : currentApp(nullptr) {}

AppMgr& AppMgr::getInstance() {
    static AppMgr instance;
    return instance;
}

void AppMgr::registerApp(App* app) {
    if (!app) return;
    apps.push_back(app);
}

void AppMgr::switchTo(int index) {
    if (index >= 0 && index < (int)apps.size()) {
        App* targetApp = apps[index];
        if (!targetApp) return;

        // Prevent restarting the application if it is already active
        if (currentApp == targetApp) return;

        // Cleanly terminate the previous application
        if (currentApp) {
            currentApp->stop();
        }

        currentApp = targetApp;

        // Mirror application transition to Serial and Web UI console
        const char* appName = currentApp->getName() ? currentApp->getName() : "Unknown";
        WebMgr::getInstance().sendLogf("AppMgr: Switched to application '%s'\n", appName);

        currentApp->start(); // Launch application lifecycle
        currentApp->draw();  // Render initial application frame
    }
}

void AppMgr::switchTo(const char* name) {
    if (!name) return;

    for (size_t i = 0; i < apps.size(); i++) {
        if (apps[i] && apps[i]->getName() && strcmp(apps[i]->getName(), name) == 0) {
            switchTo((int)i);
            return;
        }
    }
}

void AppMgr::update() {
    if (currentApp) {
        currentApp->update();
    }
}

void AppMgr::draw() {
    if (currentApp) {
        currentApp->draw();
    }
}