//api_client.h

#pragma once
#include <Arduino.h>

// Last error for diagnostics (optional).
enum class ApiError : uint8_t {
  None = 0,
  NoHost,
  DnsFailed,
  NoWiFi,
  NoAppConfig,
  ConnectFailed,
  WriteFailed,
  ReadTimeout,
  BadStatusLine,
  Non200,
};

// Returns last error set by apiPing()/future API calls.
ApiError apiLastError();

// Returns a short constant string for the last error.
const char* apiLastErrorText();

// Sends a ping request using portalConfig().channelKey.
// Returns true if server responded with HTTP 200.
bool apiPing();
