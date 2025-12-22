//wifi_manager.cpp

#include <ESP8266WiFi.h>
#include <user_interface.h>

#include "io_ui.h"
#include "noctua_portal.h"

// ============================================================
// Tuning
// ============================================================

static const uint32_t WIFI_RETRY_EVERY_MS   = 5000;
static const uint32_t WIFI_RETRY_TIMEOUT_MS = 20000;

static const uint8_t  AUTO_AP_AFTER_FAILS = 3;
static const uint32_t AUTO_AP_KEEP_MS = 60000;
static const uint32_t AUTO_AP_RECHECK_MS = AUTO_AP_KEEP_MS;

// ============================================================
// Internal state
// ============================================================

static bool gSuspend = false;

static uint32_t gLastAttemptMs = 0;
static uint32_t gAttemptStartMs = 0;
static bool gAttempting = false;

static uint8_t gConsecutiveFails = 0;
static bool gAutoApStarted = false;
static uint32_t gAutoApStopDueMs = 0;

// Station event tracking (for diagnostics + DHCP recovery)
static volatile uint32_t gStaConnectedMs = 0;
static volatile uint8_t gStaConnectedCh = 0;
static volatile uint32_t gGotIpMs = 0;
static volatile bool gGotIp = false;

// ============================================================
// Internal helpers
// ============================================================

static void wifiApplyDefaults() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // Disable power saving features that may cause connection drops
  wifi_set_sleep_type(NONE_SLEEP_T);

  // Some routers pick channels 12/13 on 2.4GHz (common in EU).
  // Ensure the SDK allows channels 1-13 so STA can associate reliably.
  // NOTE: do this every time. Some SDK paths can reset country during mode
  // transitions/reconnects, and a one-time set can lead to sporadic failures.
  wifi_country_t c;
  memset(&c, 0, sizeof(c));
  strcpy(c.cc, "EU");
  c.schan = 1;
  c.nchan = 13;
  c.policy = WIFI_COUNTRY_POLICY_MANUAL;
  (void)wifi_set_country(&c);
}

static const char* wifiStatusText(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "?";
  }
}

static void wifiDumpDiag(const char* tag) {
  const wl_status_t st = WiFi.status();
  const IPAddress ip = WiFi.localIP();
  const uint8_t sdkSt = wifi_station_get_connect_status();
  Serial.printf("[WiFi][%s] st=%d(%s) ssid='%s' ch=%d rssi=%d ip=%s gw=%s\n",
                tag ? tag : "diag",
                (int)st,
                wifiStatusText(st),
                WiFi.SSID().c_str(),
                WiFi.channel(),
                WiFi.RSSI(),
                ip.toString().c_str(),
                WiFi.gatewayIP().toString().c_str());
  Serial.printf("[WiFi][%s] sdk_station_status=%u (0=IDLE 1=CONNECTING 2=WRONG_PW 3=NO_AP 4=FAIL 5=GOT_IP)\n",
                tag ? tag : "diag",
                (unsigned)sdkSt);
}

static void wifiBeginFromConfig() {
  const auto& cfg = portalConfig();

  // Ensure we're in DHCP mode (clears any stale static config).
  WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
}

// ============================================================
// Public API
// ============================================================

void wifiManagerSuspend(bool en) {
  gSuspend = en;

  if (gSuspend) {
    // Reset reconnect state so we don't keep "attempting" after resume.
    gAttempting = false;
    gConsecutiveFails = 0;
    gAutoApStopDueMs = 0;
    ioSetStaBlinkEnabled(false);
  }
}

void wifiManagerSetup() {
  wifiApplyDefaults();
  // Intentionally do not force WIFI_STA here.
  // The main loop / portal may temporarily use WIFI_AP_STA.

  // Register event handlers once for better diagnostics.
  static WiFiEventHandler onDisconnected;
  static WiFiEventHandler onGotIp;
  static WiFiEventHandler onConnected;

  // Map common disconnect reasons (not exhaustive).
  auto reasonText = [](uint8_t r) -> const char* {
    switch (r) {
      case 2: return "AUTH_EXPIRE";
      case 4: return "ASSOC_EXPIRE";
      case 5: return "ASSOC_TOOMANY";
      case 6: return "NOT_AUTHED";
      case 7: return "NOT_ASSOCED";
      case 8: return "ASSOC_LEAVE";
      case 11: return "BEACON_TIMEOUT";
      case 13: return "NO_AP_FOUND";
      case 15: return "HANDSHAKE_TIMEOUT";
      case 201: return "NO_AP_FOUND"; // some cores use 201+
      case 202: return "AUTH_FAIL";
      case 203: return "ASSOC_FAIL";
      case 204: return "HANDSHAKE_TIMEOUT";
      default: return "?";
    }
  };

  onConnected = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& evt) {
    gStaConnectedMs = millis();
    gStaConnectedCh = evt.channel;
    gGotIp = false;
    Serial.printf("[WiFi] connected to '%s' ch=%u\n", evt.ssid.c_str(), (unsigned)evt.channel);
  });

  onGotIp = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& evt) {
    gGotIp = true;
    gGotIpMs = millis();
    Serial.printf("[WiFi] got IP: %s gw=%s\n",
                  evt.ip.toString().c_str(),
                  evt.gw.toString().c_str());
  });

  onDisconnected = WiFi.onStationModeDisconnected([reasonText](const WiFiEventStationModeDisconnected& evt) {
    gGotIp = false;
    Serial.printf("[WiFi] disconnected from '%s' reason=%u(%s)\n",
                  evt.ssid.c_str(),
                  (unsigned)evt.reason,
                  reasonText(evt.reason));
  });
}

bool wifiConnectOnce(uint32_t timeoutMs) {
  if (!portalHasStaConfig()) {
    Serial.println("WiFi config invalid: missing SSID");
    return false;
  }

  wifiApplyDefaults();

  WiFi.mode(WIFI_STA);
  // Do not erase config on disconnect; just drop the link.
  WiFi.disconnect(false);
  delay(50);
  yield();

  ioSetStaBlinkEnabled(true);
  wifiBeginFromConfig();

  bool dhcpRestarted = false;
  bool staRestarted = false;

  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    // Allow the user to force AP mode immediately while we're connecting.
    // This avoids being stuck until timeout during boot/connect attempts.
    if (ioBootPressedOnce()) {
      Serial.println("✅ BOOT -> AP mode (during STA connect)");
      ioSetStaBlinkEnabled(false);
      portalStartAP();
      return false;
    }

    // If we are associated (connected event seen) but DHCP never completes,
    // restart DHCP client once. Some APs/firmware combos get stuck here.
    const uint32_t assocMs = gStaConnectedMs;
    if (!dhcpRestarted && assocMs != 0 && assocMs >= t0 && !gGotIp && (millis() - assocMs) > 7000) {
      dhcpRestarted = true;
      Serial.println("[WiFi] assoc but no IP -> restart DHCP");
      wifi_station_dhcpc_stop();
      delay(50);
      wifi_station_dhcpc_start();
    }

    // Escalation: if DHCP still doesn't complete, restart STA stack once.
    // This is heavy but has proven to unstick certain AP/DHCP combos.
    if (dhcpRestarted && !staRestarted && assocMs != 0 && assocMs >= t0 && !gGotIp && (millis() - assocMs) > 15000) {
      staRestarted = true;
      Serial.println("[WiFi] assoc but still no IP -> restart STA stack");
      wifiDumpDiag("sta_restart_before");

      WiFi.disconnect(false);
      delay(150);
      WiFi.mode(WIFI_OFF);
      delay(250);
      wifiApplyDefaults();
      WiFi.mode(WIFI_STA);
      delay(150);
      wifiBeginFromConfig();
    }

    delay(100);
    yield();
    portalLoop();  // keep HTTP/DNS responsive while connecting
  }

  ioSetStaBlinkEnabled(false);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  wifiDumpDiag("connect_once_fail");
  Serial.printf("❌ WiFi connect failed (status=%d)\n", (int)WiFi.status());
  return false;
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void wifiManagerLoop() {
  if (gSuspend) return;

  // No credentials => nothing to do
  if (!portalHasStaConfig()) {
    gAttempting = false;
    gConsecutiveFails = 0;
    gAutoApStopDueMs = 0;
    ioSetStaBlinkEnabled(false);
    return;
  }

  // Connected => clear state
  if (wifiIsConnected()) {
    gAttempting = false;
    gConsecutiveFails = 0;
    ioSetStaBlinkEnabled(false);

    // If AP was started automatically due to repeated reconnect failures,
    // keep it for a short window after STA comes back, then shut it down.
    if (gAutoApStarted && portalIsAPRunning()) {
      if (gAutoApStopDueMs == 0) {
        gAutoApStopDueMs = millis() + AUTO_AP_KEEP_MS;
      } else if ((int32_t)(millis() - gAutoApStopDueMs) >= 0) {
        const uint8_t staNum = (uint8_t)WiFi.softAPgetStationNum();
        if (staNum == 0) {
          portalStopAP();
          gAutoApStarted = false;
          gAutoApStopDueMs = 0;
        } else {
          // Keep AP alive while someone is connected.
          gAutoApStopDueMs = millis() + AUTO_AP_RECHECK_MS;
        }
      }
    } else {
      gAutoApStopDueMs = 0;
    }
    return;
  }

  // Not connected => cancel any pending auto-stop window.
  gAutoApStopDueMs = 0;

  const uint32_t now = millis();

  // If an attempt is in progress, wait up to WIFI_RETRY_TIMEOUT_MS
  if (gAttempting) {
    // If we got connected asynchronously, stop the attempt immediately.
    if (wifiIsConnected()) {
      gAttempting = false;
      ioSetStaBlinkEnabled(false);
      return;
    }

    yield();
    portalLoop();

    // DHCP recovery for background attempts too.
    static uint32_t lastAttemptStart = 0;
    static bool dhcpKickedThisAttempt = false;
    static bool staRestartedThisAttempt = false;
    if (lastAttemptStart != gAttemptStartMs) {
      lastAttemptStart = gAttemptStartMs;
      dhcpKickedThisAttempt = false;
      staRestartedThisAttempt = false;
    }

    const uint32_t assocMs = gStaConnectedMs;
    if (!dhcpKickedThisAttempt && assocMs != 0 && assocMs >= gAttemptStartMs && !gGotIp && (now - assocMs) > 7000) {
      dhcpKickedThisAttempt = true;
      Serial.println("[WiFi] (bg) assoc but no IP -> restart DHCP");
      wifi_station_dhcpc_stop();
      delay(50);
      wifi_station_dhcpc_start();
    }

    if (dhcpKickedThisAttempt && !staRestartedThisAttempt && assocMs != 0 && assocMs >= gAttemptStartMs && !gGotIp && (now - assocMs) > 15000) {
      staRestartedThisAttempt = true;
      Serial.println("[WiFi] (bg) assoc but still no IP -> restart STA stack");
      wifiDumpDiag("bg_sta_restart_before");

      WiFi.disconnect(false);
      delay(150);
      WiFi.mode(WIFI_OFF);
      delay(250);
      wifiApplyDefaults();
      WiFi.mode(WIFI_STA);
      delay(150);
      wifiBeginFromConfig();
    }

    if (now - gAttemptStartMs > WIFI_RETRY_TIMEOUT_MS) {
      gAttempting = false;
      ioSetStaBlinkEnabled(false);
      wifiDumpDiag("reconnect_timeout");
      Serial.println("WiFi reconnect timeout");

      if (gConsecutiveFails < 255) gConsecutiveFails++;
      if (gConsecutiveFails >= AUTO_AP_AFTER_FAILS) {
        if (!portalIsAPRunning()) {
          Serial.println("⚠️ WiFi reconnect failed multiple times -> starting AP");
          portalStartAP();
          gAutoApStarted = true;
        }
      }
    }
    return;
  }

  // Start a new attempt every WIFI_RETRY_EVERY_MS
  if (now - gLastAttemptMs < WIFI_RETRY_EVERY_MS) return;

  gLastAttemptMs = now;
  gAttemptStartMs = now;
  gAttempting = true;

  // Properly reset WiFi state before reconnecting
  // This helps when router changes channels or has other config changes
  Serial.println("WiFi reconnect attempt...");
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(50);
  
  ioSetStaBlinkEnabled(true);
  wifiBeginFromConfig();
}
