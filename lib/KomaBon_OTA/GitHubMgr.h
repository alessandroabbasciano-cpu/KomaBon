#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct UpdateInfo {
    bool available;
    String version;
    String firmwareUrl;
    String filesystemUrl;
    String notes;
    bool hasFirmware;
    bool hasFilesystem;
    String firmwareSha256;
    String filesystemSha256;
    String firmwareEd25519Sig;
    String filesystemEd25519Sig;
};

class GitHubMgr {
  public:
    static GitHubMgr& getInstance();

    void init();
    UpdateInfo checkUpdate(const char* currentVersion);
    bool performFirmwareUpdate(const char* url, bool restartAfter = true, int step = 0, int totalSteps = 0,
                               const char* expectedSha256 = nullptr,
                               const char* expectedEd25519Sig = nullptr);
    bool performFilesystemUpdate(const char* url, bool restartAfter = true, int step = 0, int totalSteps = 0,
                                 const char* expectedSha256 = nullptr,
                                 const char* expectedEd25519Sig = nullptr);
    bool performFullUpdate(const char* currentVersion);
    void triggerUpdate(const char* currentVersion);

  private:
    GitHubMgr();
    bool downloadAndFlash(const char* url, int partition, const char* label, bool restartAfter, int step,
                          int totalSteps, const char* expectedSha256, const char* expectedEd25519Sig);
};