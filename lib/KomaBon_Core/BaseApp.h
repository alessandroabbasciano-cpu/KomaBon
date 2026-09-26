#pragma once
#include <Arduino.h>

class App {
  public:
    virtual ~App() {}

    // Called when the app is first loaded/switched to
    virtual void start() {}

    // Called every loop iteration
    virtual void update() {}

    // Called when screen updates needed
    virtual void draw() {}

    // Called when app is being switched away from
    virtual void stop() {}

    // Called when the display changed underneath the app (e.g. rotation flip)
    // and the current screen must be fully repainted without losing state.
    virtual void forceRedraw() {}

    // Apply a new reading font size (points). Only the reader acts on this;
    // other apps ignore it. Lets the web layer drive it through the App*
    // interface without depending on reader internals.
    virtual void applyFontSize(int pt) {}
    virtual void applyFontFamily(int family) {}

    // The system battery indicator performs a partial refresh in the upper-right
    // corner, driven directly from the main loop without going through the app.
    // Screens that occupy the full area (like the reader page) return false so
    // that no status icon appears over the reading content.
    virtual bool allowsSystemStatusIndicator() {
        return true;
    }

    // Metadata
    virtual const char* getName() = 0;
    // Returns font icon char (if using font) or empty
    virtual const char* getIcon() {
        return "";
    }
    // Returns bitmap icon (if using bitmap) or nullptr
    virtual const uint8_t* getIconImage() {
        return nullptr;
    }
};
