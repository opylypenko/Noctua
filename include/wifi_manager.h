//wifi_manager.h

#pragma once
#include <Arduino.h>

// Initializes Wi-Fi subsystem defaults (no persistence, no auto-reconnect).
void wifiManagerSetup();

// Connect once using credentials from portalConfig().
// Returns true on successful connection within timeoutMs.
bool wifiConnectOnce(uint32_t timeoutMs);

// Background reconnect state machine (call from main loop).
void wifiManagerLoop();

// Returns true if STA is connected.
bool wifiIsConnected();

// Temporarily disables wifiManagerLoop() actions (prevents reconnect attempts).
void wifiManagerSuspend(bool en);
