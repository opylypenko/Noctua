//noctua_portal.h

#pragma once
#include <Arduino.h>

// ============================================================
// Types
// ============================================================

struct NoctuaConfig {
  char wifiSsid[33];
  char wifiPass[65];
  char channelKey[65];
  char adminPass[65];

  // If true, LED is completely disabled (off in all modes).
  bool ledDisabled;
};

// ============================================================
// Config access (in-memory)
// ============================================================

// Returns reference to the active config stored in RAM.
NoctuaConfig& portalConfig();

// ============================================================
// Config validation helpers
// ============================================================

// Returns true if STA (Wi-Fi) configuration is present.
bool portalHasStaConfig();

// Returns true if application configuration is present (channel key).
bool portalHasAppConfig();

// ============================================================
// Config persistence
// ============================================================

// Load config from filesystem into provided struct.
// Returns true if at least one known key was found and applied.
bool portalLoadConfig(NoctuaConfig& cfg);

// Save config to filesystem.
bool portalSaveConfig(const NoctuaConfig& cfg);

// ============================================================
// Runtime config apply (no reboot)
// ============================================================

// Mark config as changed and pending apply.
void portalMarkConfigDirty();

// Returns true if config needs to be applied.
bool portalIsConfigDirty();

// Clear dirty flag after successful apply.
void portalClearConfigDirty();

// ============================================================
// Runtime diagnostics (optional UI)
// ============================================================

// Updates "Last ping" timestamp + result shown in portal pages.
void portalSetPingStatus(bool ok);
// Set ping error message (response body or description). Pass empty string to clear.
void portalSetPingError(const char* error);

// Countdown (in seconds) until the next scheduled ping attempt.
// Pass -1 if unknown / not applicable.
void portalSetNextPingInSeconds(int seconds);

// Updates API host reachability shown in portal pages.
// This is a lower-level signal than ping: it means we could reach the API host
// and read a valid HTTP status line (even if it's not 200).
void portalSetHostReachable(bool ok);

// Updates "Internet" status shown in portal pages.
// Semantics: basic outbound connectivity (separate from API host reachability).
void portalSetInternetStatus(bool ok);

// Clears Internet status to "unknown".
void portalClearInternetStatus();

// ============================================================
// Portal / Web / AP lifecycle
// ============================================================

// Initialize portal (HTTP server, routes, AP credentials).
void portalSetup(const char* apName, const char* apPass);

// Process HTTP/DNS requests (call from main loop).
void portalLoop();

// Start captive portal AP mode.
void portalStartAP();

// Stop captive portal AP mode.
void portalStopAP();

// Returns true if AP is currently running.
bool portalIsAPRunning();
