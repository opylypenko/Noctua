#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP.h>
#include <LittleFS.h>
#include <WiFiClient.h>

#include "api_client.h"
#include "io_ui.h"
#include "noctua_portal.h"
#include "wifi_manager.h"

// ============================================================
// Project settings
// ============================================================

static const char* SETUP_AP_SSID = "Noctua";
static const char* SETUP_AP_PASS = "";

static const uint32_t WIFI_BOOT_GRACE_MS = 60000;
static const uint32_t PING_INTERVAL_MS = 90000;
static const uint32_t INTERNET_CHECK_INTERVAL_MS = 30000;

#ifndef NOCTUA_LED_PIN
static const int LED_PIN = 16;
#else
static const int LED_PIN = NOCTUA_LED_PIN;
#endif

#ifndef NOCTUA_LED_ACTIVE_LOW
static const bool LED_ACTIVE_LOW = false;  // default for ESP-01 setup
#else
static const bool LED_ACTIVE_LOW = (NOCTUA_LED_ACTIVE_LOW != 0);
#endif

#ifndef NOCTUA_BOOT_PIN
static const int BOOT_PIN = 0;
#else
static const int BOOT_PIN = NOCTUA_BOOT_PIN;
#endif

// ============================================================
// Runtime
// ============================================================

static uint32_t gLastPingMs = 0;
static bool gWasStaConnected = false;

static uint32_t gLastInternetCheckMs = 0;
static bool gInternetOk = false;

static bool gReconfigInProgress = false;
static uint32_t gLastReconfigMs = 0;
static const uint32_t RECONFIG_COOLDOWN_MS = 1500;

static const uint32_t RTC_RESET_CFG_MAGIC = 0x4E435452;  // 'NCTR'

static void clearConfigIfRequestedOnBoot() {
  uint32_t magic = 0;
  if (!ESP.rtcUserMemoryRead(0, (uint32_t*)&magic, sizeof(magic))) return;
  if (magic != RTC_RESET_CFG_MAGIC) return;

  // Clear the flag first to avoid reboot loops if FS ops fail.
  magic = 0;
  (void)ESP.rtcUserMemoryWrite(0, (uint32_t*)&magic, sizeof(magic));

  Serial.println("Clearing config (requested)");

  // Some LittleFS operations may take long enough to trip the ESP8266 watchdog.
  // This path is only used for the explicit factory-reset action.
  ESP.wdtDisable();

  bool fsOk = LittleFS.begin();
  if (!fsOk) {
    // If mount fails (corruption), format once and retry.
    (void)LittleFS.format();
    fsOk = LittleFS.begin();
  }

  if (fsOk) {
    if (LittleFS.exists("/noctua.cfg")) {
      (void)LittleFS.remove("/noctua.cfg");
    }
    LittleFS.end();
  }

  // Reboot into a clean state.
  ESP.restart();
  while (true) {
    delay(0);
  }
}

static bool checkInternetConnection() {
  // Lightweight outbound connectivity probe.
  // Important: avoid hostname DNS here. DNS resolution can occasionally block
  // long enough to look like a device hang on ESP8266.
  // Cloudflare DNS IP (should be reachable on most networks).
  const IPAddress probeIp(1, 1, 1, 1);

  WiFiClient client;
  client.setTimeout(1500);
  yield();
  const bool ok = client.connect(probeIp, 53);
  yield();
  if (ok) {
    client.stop();
    return true;
  }
  client.stop();
  return false;
}

// Apply current portalConfig() to Wi-Fi without reboot.
static void applyConfigNoReboot() {
  Serial.println("Applying new config (no reboot)");

  gReconfigInProgress = true;
  gLastReconfigMs = millis();
  portalClearConfigDirty();

  // Apply LED setting immediately.
  ioSetLedEnabled(!portalConfig().ledDisabled);

  // Prevent background reconnect loop from touching WiFi during transition
  wifiManagerSuspend(true);

  // Stop AP first (if running) to avoid AP/STA churn
  if (portalIsAPRunning()) portalStopAP();

  // Hard reset STA state
  WiFi.disconnect(true);
  delay(200);
  yield();

  WiFi.mode(WIFI_STA);
  delay(100);
  yield();

  // Allow wifi_manager again and do a controlled connect attempt
  wifiManagerSuspend(false);
  const bool ok = wifiConnectOnce(15000);
  if (!ok) portalStartAP();

  gReconfigInProgress = false;
}

void setup() {
  Serial.begin(115200);
  // Keep SDK debug output off (it's very noisy).
  Serial.setDebugOutput(false);
  delay(500);

  Serial.println("\n=== Noctua ===");

  Serial.printf("reset_reason: %s\n", ESP.getResetReason().c_str());
  Serial.printf("reset_info: %s\n", ESP.getResetInfo().c_str());
  Serial.printf("sdk: %s boot_ver=%u\n", ESP.getSdkVersion(), (unsigned)ESP.getBootVersion());
  Serial.printf("chip_id: %06X flash_id: %08X\n", ESP.getChipId(), ESP.getFlashChipId());
  Serial.printf("heap: %u\n", (unsigned)ESP.getFreeHeap());

  clearConfigIfRequestedOnBoot();

  ioSetup(LED_PIN, LED_ACTIVE_LOW, BOOT_PIN);

  // Portal/web server is always available (STA or AP)
  portalSetup(SETUP_AP_SSID, SETUP_AP_PASS);

  // Load config into in-memory struct
  memset(&portalConfig(), 0, sizeof(NoctuaConfig));
  const bool loaded = portalLoadConfig(portalConfig());

  // Apply LED config (default: enabled)
  ioSetLedEnabled(!portalConfig().ledDisabled);

  Serial.printf("loaded=%u ssid='%s' pass_len=%u ckey_len=%u apass_len=%u\n",
                (unsigned)loaded,
                portalConfig().wifiSsid,
                (unsigned)strlen(portalConfig().wifiPass),
                (unsigned)strlen(portalConfig().channelKey),
                (unsigned)strlen(portalConfig().adminPass));

  wifiManagerSetup();

  // Boot decision:
  // - No SSID (or failed load) => start AP mode
  // - Otherwise try STA connect
  if (!loaded || !portalHasStaConfig()) {
    Serial.println("⚠️ No SSID -> AP mode");
    portalStartAP();
  } else {
    Serial.println("✅ SSID found -> connecting...");
    const bool ok = wifiConnectOnce(WIFI_BOOT_GRACE_MS);
    if (!ok) portalStartAP();
  }

  // Ping schedule starts only after Wi-Fi is connected.
  // Avoid immediate ping on boot/reconnect to prevent bursts on frequent power cycles.
  gWasStaConnected = wifiIsConnected();
  gLastPingMs = millis();

  // Start countdown from full interval once we're connected.
  if (wifiIsConnected() && portalHasAppConfig()) {
    portalSetNextPingInSeconds((int)(PING_INTERVAL_MS / 1000));
  } else {
    portalSetNextPingInSeconds(-1);
  }

  // Internet status is checked lazily from loop; start unknown/false.
  gInternetOk = false;
  portalClearInternetStatus();
  gLastInternetCheckMs = 0;
}

void loop() {
  yield();

  // Keep portal responsive in all modes
  portalLoop();

  // Apply new config ASAP (avoid races with reconnect/ping)
  if (!gReconfigInProgress && portalIsConfigDirty()) {
    const uint32_t now = millis();
    if (now - gLastReconfigMs >= RECONFIG_COOLDOWN_MS) {
      applyConfigNoReboot();
    } else {
      // If we get repeated dirty events too quickly, just ignore until cooldown passes.
      portalClearConfigDirty();
    }
  }

  // LED state
  ioSetApBlinkEnabled(portalIsAPRunning());

  // Heartbeat only in normal operation: STA connected + AP off.
  ioSetHeartbeatEnabled(!gReconfigInProgress && !portalConfig().ledDisabled && wifiIsConnected() && !portalIsAPRunning());
  ioLoop();

  // BOOT button: toggle AP
  if (ioBootPressedOnce()) {
    if (portalIsAPRunning()) {
      Serial.println("✅ BOOT -> AP off");
      portalStopAP();
    } else {
      Serial.println("✅ BOOT -> AP mode");
      portalStartAP();
    }
  }

  // Background reconnect (disabled during apply)
  if (!gReconfigInProgress) {
    wifiManagerLoop();
  }

  // Track Wi-Fi connection transitions to delay pings after reconnect.
  const bool staConnectedNow = wifiIsConnected();
  if (staConnectedNow && !gWasStaConnected) {
    gWasStaConnected = true;
    gLastPingMs = millis();

    // Force an Internet re-check after reconnect.
    portalClearInternetStatus();
    gLastInternetCheckMs = 0;
  } else if (!staConnectedNow && gWasStaConnected) {
    gWasStaConnected = false;
    gInternetOk = false;
    portalClearInternetStatus();
    gLastInternetCheckMs = 0;
  }

  const uint32_t now = millis();

  // Cached Internet status (every 30s, only when Wi-Fi connected)
  if (!gReconfigInProgress && wifiIsConnected()) {
    if (gLastInternetCheckMs == 0 || (now - gLastInternetCheckMs >= INTERNET_CHECK_INTERVAL_MS)) {
      gLastInternetCheckMs = now;
      gInternetOk = checkInternetConnection();
      portalSetInternetStatus(gInternetOk);
    }
  }

  // Publish countdown to next ping for UI.
  int nextPingInS = -1;
  if (!gReconfigInProgress && wifiIsConnected() && portalHasAppConfig()) {
    const uint32_t elapsed = now - gLastPingMs;
    if (elapsed >= PING_INTERVAL_MS) {
      nextPingInS = 0;
    } else {
      const uint32_t remainingMs = PING_INTERVAL_MS - elapsed;
      nextPingInS = (int)((remainingMs + 999) / 1000);
    }
  }
  portalSetNextPingInSeconds(nextPingInS);

  // Backend ping (every 90s)
  if (!gReconfigInProgress && wifiIsConnected() && (now - gLastPingMs >= PING_INTERVAL_MS)) {
    if (portalHasAppConfig()) {
      (void)apiPing();
    } else {
      Serial.println("ℹ️ skip ping: no channelKey");
    }
    gLastPingMs = now;
  }

  delay(10);
}
