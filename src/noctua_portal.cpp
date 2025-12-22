//noctua_portal.cpp

#include "noctua_portal.h"

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESP.h>
#include <Updater.h>

#include "noctua_i18n.h"

// ============================================================
// Globals / constants
// ============================================================

static ESP8266WebServer gServer(80);
static DNSServer gDns;

static NoctuaConfig gCfg;

static bool gApRunning = false;
static bool gConfigDirty = false;

static char gApSsid[33] = "";
static char gApPass[65] = "";

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);
static const uint16_t DNS_PORT = 53;

static const char* CFG_PATH = "/noctua.cfg";

static uint32_t gLoginExpireMs = 0;
static const uint32_t LOGIN_TIMEOUT_MS = 10 * 60 * 1000; // 10 minutes

static uint32_t gLastPingMs = 0;
static bool gLastPingOk = false;
static char gLastPingError[192] = {0};

static int gNextPingInS = -1;

static bool gHostReachable = false;
static bool gInternetOk = false;
static bool gInternetKnown = false;

static bool gResetConfigPending = false;
static uint32_t gResetConfigDueMs = 0;

static bool gSaveAndRebootPending = false;
static uint32_t gSaveAndRebootDueMs = 0;

static bool gOtaRebootPending = false;
static uint32_t gOtaRebootDueMs = 0;

static uint32_t gOtaBytesReceived = 0;
static int gOtaLastError = 0;
static char gOtaLastErrorMsg[96] = {0};

static void otaSetError(const __FlashStringHelper* msg) {
  gOtaLastError = (int)Update.getError();
  strncpy(gOtaLastErrorMsg, (const char*)msg, sizeof(gOtaLastErrorMsg) - 1);
  gOtaLastErrorMsg[sizeof(gOtaLastErrorMsg) - 1] = '\0';
}

static void otaClearError() {
  gOtaBytesReceived = 0;
  gOtaLastError = 0;
  gOtaLastErrorMsg[0] = 0;
}

static const uint32_t RTC_RESET_CFG_MAGIC = 0x4E435452; // 'NCTR'

static void safeRestart() {
  // ESP-01 uses GPIO0 as a boot strap pin. If it's LOW during reset, the chip
  // enters UART download mode (boot mode (1,x)) and won't boot firmware.
  pinMode(0, INPUT_PULLUP);
  delay(20);
  yield();
  ESP.restart();
}

static void markResetConfigOnNextBoot() {
  uint32_t magic = RTC_RESET_CFG_MAGIC;
  (void)ESP.rtcUserMemoryWrite(0, (uint32_t*)&magic, sizeof(magic));
}

// ============================================================
// Forward declarations (internal)
// ============================================================

static bool fsBeginWithFormatFallback();
static bool fsBeginNoFormat();
static void copyToBuf(char* dst, size_t dstSize, const String& src);

static bool isLoggedIn();
static bool isAdminPasswordSet();
static bool authRequired();
static bool isAuthorized();

static String htmlEscape(const String& s);
static String css();
static String pageShell(const String& title, const String& body);
static String fmtUptime();
static String wifiStatusText();
static const char* wifiStatusCode();
static String internetText();
static String statusLine();

static void sendRedirect(const char* location);

static void handleRoot();
static void handleStatusJson();
static void handleLoginGet();
static void handleLoginPost();
static void handleAdmin();
static void handleSave();
static void handleResetConfigGet();
static void handleResetConfig();
static void handleUpdateGet();
static void handleUpdatePost();
static void handleUpdateUpload();
static void handleNotFound();

static void sendRebootingPage();

// ============================================================
// Public: config flags / helpers
// ============================================================

NoctuaConfig& portalConfig() { return gCfg; }

bool portalHasStaConfig() { return strlen(gCfg.wifiSsid) > 0; }
bool portalHasAppConfig() { return strlen(gCfg.channelKey) > 0; }

void portalMarkConfigDirty() { gConfigDirty = true; }
bool portalIsConfigDirty() { return gConfigDirty; }
void portalClearConfigDirty() { gConfigDirty = false; }

// Runtime status setters (used for portal UI diagnostics)
void portalSetPingStatus(bool ok) {
  gLastPingMs = millis();
  gLastPingOk = ok;
}

void portalSetPingError(const char* error) {
  if (error && error[0]) {
    strncpy(gLastPingError, error, sizeof(gLastPingError) - 1);
    gLastPingError[sizeof(gLastPingError) - 1] = '\0';
  } else {
    gLastPingError[0] = '\0';
  }
}

void portalSetNextPingInSeconds(int seconds) {
  gNextPingInS = seconds;
}

void portalSetHostReachable(bool ok) { gHostReachable = ok; }

void portalSetInternetStatus(bool ok) {
  gInternetOk = ok;
  gInternetKnown = true;
}

void portalClearInternetStatus() {
  gInternetOk = false;
  gInternetKnown = false;
}

// ============================================================
// Internal: FS helper
// ============================================================

static bool fsBeginWithFormatFallback() {
  static bool formattedThisBoot = false;

  if (LittleFS.begin()) return true;

  if (formattedThisBoot) {
    Serial.println("❌ [FS] LittleFS.begin() failed again (format already attempted)");
    return false;
  }

  Serial.println("⚠️ [FS] LittleFS.begin() failed -> formatting once...");
  formattedThisBoot = true;

  if (!LittleFS.format()) return false;
  return LittleFS.begin();
}

static bool fsBeginNoFormat() {
  return LittleFS.begin();
}

// ============================================================
// Internal: auth
// ============================================================

static bool isLoggedIn() {
  return (millis() < gLoginExpireMs);
}

static bool isAdminPasswordSet() { return strlen(gCfg.adminPass) > 0; }
static bool authRequired() { return isAdminPasswordSet(); }

static bool isAuthorized() {
  if (!authRequired()) return true;
  return isLoggedIn();
}

// ============================================================
// Internal: HTML helpers
// ============================================================

static String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '&') o += F("&amp;");
    else if (c == '<') o += F("&lt;");
    else if (c == '>') o += F("&gt;");
    else if (c == '\"') o += F("&quot;");
    else o += c;
  }
  return o;
}

// Simple JSON string escaper (no surrounding quotes)
static String jsonEscape(const char* s) {
  String o;
  if (!s) return o;
  for (size_t i = 0; s[i] != 0; ++i) {
    const char c = s[i];
    if (c == '"') o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\t') o += "\\t";
    else o += c;
  }
  return o;
}

static String css() {
  return F(
    "html,body{height:100%;}"
    "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Inter,Arial,sans-serif;"
    "background:#f5f5f7;color:#111;}"
    ".wrap{max-width:620px;margin:0 auto;padding:16px 14px 24px;}"
    ".card{background:#fff;border-radius:16px;padding:14px 14px 10px;"
    "box-shadow:0 8px 20px rgba(0,0,0,.06);border:1px solid rgba(0,0,0,.05);}"
    "h1{font-size:20px;letter-spacing:-.2px;margin:0 0 4px;}"
    "p{margin:6px 0 10px;color:#444;line-height:1.35;}"
    ".row{display:flex;gap:10px;flex-wrap:wrap;}"
    ".field{flex:1 1 220px;min-width:0;}"
    "label{display:block;font-size:12px;color:#666;margin:8px 0 4px;min-width:0;}"
    "input{width:100%;box-sizing:border-box;border-radius:12px;border:1px solid rgba(0,0,0,.12);"
    "padding:10px 10px;font-size:14px;outline:none;background:#fff;}"
    "input[type=checkbox]{width:auto;padding:0;border:0;border-radius:0;}"
    "input:focus{border-color:rgba(0,0,0,.25);}"
    ".btn{margin-top:10px;display:inline-block;border:0;border-radius:12px;"
    "padding:10px 12px;font-size:14px;cursor:pointer;background:#111;color:#fff;}"
    ".btn2{background:#fff;color:#111;border:1px solid rgba(0,0,0,.12);}"
    ".btnDanger{background:#fff;color:#b00020;border:1px solid #b00020;}"
    ".pill{display:inline-block;padding:5px 9px;border-radius:999px;font-size:12px;"
    "background:#f2f2f7;color:#111;border:1px solid rgba(0,0,0,.06);}"
    ".muted{color:#666;font-size:12px;}"
    ".corner{position:fixed;right:12px;bottom:10px;}"
    ".sep{height:1px;background:rgba(0,0,0,.06);margin:10px 0;}"
    "code{font-family:inherit;font-size:inherit;line-height:inherit;background:#f2f2f7;padding:2px 6px;border-radius:8px;}"
    ".stOk{color:#0a7a2f;}"
    ".stBad{color:#b00020;}"
    ".stWarn{color:#b38600;}"
    ".spinner{display:inline-block;width:14px;height:14px;border:2px solid rgba(255,255,255,.3);border-top-color:#fff;border-radius:50%;margin-right:8px;vertical-align:middle;animation:spin 1s linear infinite;}"
    ".overlay{position:fixed;inset:0;display:none;align-items:center;justify-content:center;background:rgba(0,0,0,0.5);z-index:9999;}"
    ".overlay .spinner{width:40px;height:40px;border-width:4px;border-color:rgba(255,255,255,0.25);border-top-color:#fff;}"
    "@keyframes spin{to{transform:rotate(360deg)}}"
  );
}

static String pageShell(const String& title, const String& body) {
  String out;
  out.reserve(2600);
  out += F("<!doctype html><html><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>");
  out += F("<title>");
  out += htmlEscape(title);
  out += F("</title><style>");
  out += css();
  out += F("</style></head><body><div class='wrap'>");
  out += body;
  out += F("</div></body></html>");
  return out;
}

// ============================================================
// Internal: status formatting
// ============================================================

static String fmtUptime() {
  const uint32_t sec = millis() / 1000;
  const uint32_t h = sec / 3600;
  const uint32_t m = (sec % 3600) / 60;
  const uint32_t s = sec % 60;

  char buf[24];
  snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
           (unsigned long)h, (unsigned long)m, (unsigned long)s);
  return String(buf);
}

static const __FlashStringHelper* fwVersionText() {
  return F(__DATE__ " " __TIME__);
}

static String wifiStatusText() {
  // If no STA credentials are configured, do not show misleading "Connecting".
  if (!portalHasStaConfig()) return String(NOCTUA_I18N_WIFI_STATUS_NEED_CFG);
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) return String(NOCTUA_I18N_WIFI_STATUS_CONNECTED);
  if (st == WL_IDLE_STATUS) return String(NOCTUA_I18N_WIFI_STATUS_CONNECTING);
  if (st == WL_NO_SSID_AVAIL) return String(NOCTUA_I18N_WIFI_STATUS_NO_SSID);
  if (st == WL_CONNECT_FAILED) return String(NOCTUA_I18N_WIFI_STATUS_CONNECT_FAILED);
  if (st == WL_WRONG_PASSWORD) return String(NOCTUA_I18N_WIFI_STATUS_WRONG_PASSWORD);
  if (st == WL_DISCONNECTED) return String(NOCTUA_I18N_WIFI_STATUS_DISCONNECTED);
  return String(NOCTUA_I18N_WIFI_STATUS_UNKNOWN);
}

// Machine-friendly Wi-Fi status code for the UI logic.
static const char* wifiStatusCode() {
  if (!portalHasStaConfig()) return "need_cfg";

  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) return "connected";
  if (st == WL_IDLE_STATUS) return "connecting";
  if (st == WL_NO_SSID_AVAIL) return "no_ssid";
  if (st == WL_CONNECT_FAILED) return "connect_failed";
  if (st == WL_WRONG_PASSWORD) return "wrong_password";
  if (st == WL_DISCONNECTED) return "disconnected";
  return "unknown";
}

static String internetText() {
  if (WiFi.status() != WL_CONNECTED) return String(NOCTUA_I18N_DASH);
  if (!gInternetKnown) return String(NOCTUA_I18N_INTERNET_UNKNOWN);
  return gInternetOk ? String(NOCTUA_I18N_INTERNET_REACHABLE) : String(NOCTUA_I18N_INTERNET_NO_ROUTE);
}

static String statusLine() {
  String s;

  const bool hasStaCfg = portalHasStaConfig();

  // STA: show only when connected/connecting; otherwise hidden to avoid clutter.
  const wl_status_t st = WiFi.status();
  s += F("<p id='sta_pill' class='muted' style='");
  if (hasStaCfg && (st == WL_CONNECTED || st == WL_IDLE_STATUS)) {
    s += F("");
  } else {
    s += F("display:none;");
  }
  s += F("'>");
  s += F("<b>STA:</b> <code><span id='val_sta' class='");
  if (st == WL_CONNECTED) {
    s += F("stOk");
  } else {
    s += F("stWarn");
  }
  s += F("'>");
  if (st == WL_CONNECTED) {
    s += WiFi.localIP().toString();
  } else {
    s += NOCTUA_I18N_WIFI_STATUS_CONNECTING;
  }
  s += F("</span></code>");
  s += F("</p>");

  // When there is no STA configuration, show a clear hint instead of STA state.
  s += F("<p id='wifi_cfg_pill' class='muted' style='");
  s += hasStaCfg ? F("display:none;") : F("");
  s += F("'>");
  s += NOCTUA_I18N_STATUS_WIFI_CFG_PILL_HTML;
  s += F("</p>");

  s += F("<p id='ap_block' class='muted' style='");
  s += (gApRunning ? F("") : F("display:none;"));
  s += F("'>");
  const int apClients = WiFi.softAPgetStationNum();
  s += F("<b>AP:</b> <code><span id='val_ap' class='");
  s += (apClients > 0) ? F("stOk") : F("stWarn");
  s += F("'>192.168.4.1</span></code>");
  s += F("</p>");

  // Internet status on its own line under STA/AP
  s += F("<p class='muted'>");
  s += NOCTUA_I18N_STATUS_INTERNET_PREFIX_HTML;
  s += htmlEscape(internetText());
  s += F("</span></code>");
  s += F("</p>");

  return s;
}

// ============================================================
// Internal: misc helpers
// ============================================================

static void copyToBuf(char* dst, size_t dstSize, const String& src) {
  if (dstSize == 0) return;

  // Trim leading whitespace
  size_t start = 0;
  while (start < src.length()) {
    char c = src[start];
    if (c == '\r' || c == '\n') { start++; continue; }
    if (c > ' ') break;  // skip spaces/tabs/etc.
    start++;
  }

  // Copy until end/newline, then trim trailing whitespace
  size_t n = 0;
  for (size_t i = start; i < src.length() && (n + 1) < dstSize; i++) {
    char c = src[i];
    if (c == '\r' || c == '\n') break; // stop on line breaks
    dst[n++] = c;
  }

  // Trim trailing whitespace
  while (n > 0 && (unsigned char)dst[n - 1] <= (unsigned char)' ') {
    n--;
  }

  dst[n] = 0;
}

static void sendRedirect(const char* location) {
  gServer.sendHeader("Cache-Control", "no-store");
  gServer.sendHeader("Location", location);
  gServer.send(302, "text/plain", "Redirect");
}

// ============================================================
// HTTP handlers
// ============================================================

static void handleRoot() {
  gServer.sendHeader("Cache-Control", "no-store");

  String body;
  body.reserve(3600);

  body += F("<div class='card'>");
  body += F("<h1>Noctua</h1>");
  body += F("<p id='subtitle' class='muted'>");
  body += NOCTUA_I18N_HOME_SUBTITLE_PREFIX;
  body += F("<code><span id='val_uptime'>");
  body += htmlEscape(fmtUptime());
  body += F("</span></code></p>");

  // Connection (STA/AP) + optional API error details
  body += F("<div class='sep'></div>");
  // API status line
  body += F("<p class='muted'>");
  body += NOCTUA_I18N_HOME_STATUS_PREFIX_HTML;
  body += F("<code>");
  body += F("<span id='ping_eta_wrap' style='display:none'><span id='val_ping_eta'>—</span> · </span>");
  body += F("<span id='val_api'>");
  body += NOCTUA_I18N_API_WAITING;
  body += F("</span></code></p>");

  body += statusLine();

  body += F("<div class='sep'></div>");
  body += F("<a class='btn' href='/admin' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_CONFIGURE;
  body += F("</a>");

  // Live updates without full reload (no flicker).
  body += F("<script>");
  body += F("const I18N={");
  body += F("now:'");
  body += NOCTUA_I18N_JS_NOW;
  body += F("',sec:'");
  body += NOCTUA_I18N_JS_SEC;
  body += F("',");
  body += F("api_wait:'");
  body += NOCTUA_I18N_API_WAITING;
  body += F("',api_ok:'");
  body += NOCTUA_I18N_API_OK;
  body += F("',api_fail:'");
  body += NOCTUA_I18N_API_FAIL;
  body += F("',");
  body += F("wifi_connecting:'");
  body += NOCTUA_I18N_WIFI_STATUS_CONNECTING;
  body += F("',wifi_need_cfg:'");
  body += NOCTUA_I18N_WIFI_STATUS_NEED_CFG;
  body += F("',");
  body += F("internet_unknown:'");
  body += NOCTUA_I18N_INTERNET_UNKNOWN;
  body += F("',internet_reach:'");
  body += NOCTUA_I18N_INTERNET_REACHABLE;
  body += F("',internet_noroute:'");
  body += NOCTUA_I18N_INTERNET_NO_ROUTE;
  body += F("'};\n");

  body += F(
    "function setText(id, v){var el=document.getElementById(id);if(!el) return;if(el.textContent!==v) el.textContent=v;}"
    "function setDisplay(id, show){var el=document.getElementById(id);if(!el) return;var d=show ? '' : 'none';if(el.style.display!==d) el.style.display=d;}"
    "function clearUi(){"
      "setDisplay('sta_pill', false);"
      "setDisplay('wifi_cfg_pill', false);"
      "setText('val_sta','—');"
      "setClass('val_sta','');"
      "setDisplay('ap_block', false);"
      "setText('val_ap','192.168.4.1');"
      "setClass('val_ap','stWarn');"
      "setText('val_api','—');"
      "setClass('val_api','');"
      "setClass('subtitle','');"
      "setText('val_internet','—');"
      "setClass('val_internet','');"
      "setText('val_uptime','—');"
      "setDisplay('ping_eta_wrap', false);"
      "setText('val_ping_eta','—');"
    "}"
    "function fmtUptime(sec){sec=Math.max(0, sec|0);var h=(sec/3600)|0;var m=((sec%3600)/60)|0;var s=(sec%60)|0;return h+':' + (m<10?'0':'')+m + ':' + (s<10?'0':'')+s;}"
    "function fmtPingEta(sec){sec=(sec===undefined||sec===null)?-1:(sec|0);if(sec<0) return null;if(sec<=0) return I18N.now;return sec+I18N.sec;}"
    "function fmtApi(has, ok, err){if(!has) return {t:I18N.api_wait, c:'stWarn'};if(ok) return {t:I18N.api_ok, c:'stOk'};err=(err===undefined||err===null)?'':String(err);err=err.replace(/\\s+/g,' ').trim();if(err.length) return {t:err, c:'stBad'};return {t:I18N.api_fail, c:'stBad'};}"
    "function setClass(id, cls){var el=document.getElementById(id);if(!el) return;el.classList.remove('stOk','stBad','stWarn');if(cls) el.classList.add(cls);}"
    "function fmtInternet(wifi, known, ok){if(!wifi) return {t:'—', c:''};if(!known) return {t:I18N.internet_unknown, c:'stWarn'};return ok ? {t:I18N.internet_reach, c:'stOk'} : {t:I18N.internet_noroute, c:'stBad'};}"
    "async function poll(){"
      "try{"
        "const r=await fetch('/status.json', {cache:'no-store'});"
        "if(!r.ok){clearUi();return;}"
        "const j=await r.json();"
        "var wcode=(j.wifi_status_code||'');"
        "var needCfg=(wcode==='need_cfg');"
        "setDisplay('wifi_cfg_pill', needCfg);"
        "var staConnected=(wcode==='connected');"
        "var staConnecting=(wcode==='connecting');"
        "setDisplay('sta_pill', staConnected || staConnecting);"
        "if(staConnected){"
          "var ip=(j.local_ip===undefined||j.local_ip===null||j.local_ip==='') ? '—' : j.local_ip;"
          "setText('val_sta', ip);"
          "setClass('val_sta','stOk');"
        "}else if(staConnecting){"
          "setText('val_sta', I18N.wifi_connecting);"
          "setClass('val_sta','stWarn');"
        "}else if(needCfg){"
          "setText('val_sta', I18N.wifi_need_cfg);"
          "setClass('val_sta','stWarn');"
        "}else{"
          "setText('val_sta','—');"
          "setClass('val_sta','');"
        "}"
        "setDisplay('ap_block', !!j.ap_running);"
        "var apc=(j.ap_clients===undefined||j.ap_clients===null)?0:(j.ap_clients|0);"
        "setClass('val_ap', apc>0 ? 'stOk' : 'stWarn');"
        "setText('val_uptime', fmtUptime(j.uptime_s||0));"
        "var eta=fmtPingEta(j.next_ping_in_s);"
        "setDisplay('ping_eta_wrap', !!eta);"
        "setText('val_ping_eta', eta ? eta : '—');"
        "var a=fmtApi(!!j.has_ping, !!j.last_ping_ok, j.ping_error);"
        "setText('val_api', a.t);"
        "setClass('val_api', a.c);"
        "setClass('subtitle','');"
        "var s=fmtInternet(staConnected, !!j.internet_known, !!j.internet_ok);"
        "setText('val_internet', s.t);"
        "setClass('val_internet', s.c);"
      "}catch(e){clearUi();}"
    "}"
    "poll();"
    "setInterval(poll, 1200);"
  );
  body += F("</script>");

  body += F("</div>");

  body += F("<div class='muted corner'>");
  body += NOCTUA_I18N_FOOTER_FW_LABEL;
  body += F(" <code>");
  body += fwVersionText();
  body += F("</code></div>");

  gServer.send(200, "text/html; charset=utf-8", pageShell("Noctua", body));
}

static void handleStatusJson() {
  gServer.sendHeader("Cache-Control", "no-store");

  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  const int rssi = wifiConnected ? WiFi.RSSI() : 0;

  const bool hasPing = (gLastPingMs != 0);
  const int lastPingAgo = hasPing ? (int)((millis() - gLastPingMs) / 1000) : -1;

  const uint32_t uptimeSec = millis() / 1000;

  String json;
  json.reserve(320);

  json += '{';

  json += F("\"wifi_status_code\":\"");
  json += wifiStatusCode();
  json += F("\",");

  const String ws = wifiStatusText();

  json += F("\"wifi_status\":\"");
  json += jsonEscape(ws.c_str());
  json += F("\",");

  if (wifiConnected) {
    json += F("\"local_ip\":\"");
    json += WiFi.localIP().toString();
    json += F("\",");
  }

  json += F("\"uptime_s\":");
  json += String((unsigned long)uptimeSec);
  json += ',';

  json += F("\"next_ping_in_s\":");
  json += String(gNextPingInS);
  json += ',';

  json += F("\"has_ping\":");
  json += (hasPing ? F("true") : F("false"));
  json += ',';

  json += F("\"last_ping_ago_s\":");
  json += String(lastPingAgo);
  json += ',';

  json += F("\"last_ping_ok\":");
  json += (gLastPingOk ? F("true") : F("false"));
  json += ',';

  json += F("\"ping_error\":\"");
  json += jsonEscape(gLastPingError);
  json += F("\",");

  json += F("\"internet_ok\":");
  json += (gInternetOk ? F("true") : F("false"));
  json += ',';

  json += F("\"internet_known\":");
  json += (gInternetKnown ? F("true") : F("false"));
  json += ',';

  json += F("\"host_reachable\":");
  json += (gHostReachable ? F("true") : F("false"));

  json += F(",\"ap_running\":");
  json += (gApRunning ? F("true") : F("false"));

  json += F(",\"ap_clients\":");
  json += String((int)WiFi.softAPgetStationNum());

  if (wifiConnected) {
    json += F(",\"rssi_dbm\":");
    json += String(rssi);
  }

  json += '}';

  gServer.send(200, "application/json; charset=utf-8", json);
}

static void sendRebootingPage() {
  gServer.sendHeader("Connection", "close");

  String body;
  body.reserve(1700);
  body += F("<div class='card'>");
  body += F("<h1>");
  body += NOCTUA_I18N_H1_REBOOTING;
  body += F("</h1>");
  body += F("<p id='msg'>");
  body += NOCTUA_I18N_REBOOT_MSG;
  body += F("</p>");
  body += F("<p class='muted' id='detail'>");
  body += NOCTUA_I18N_REBOOT_DETAIL;
  body += F("</p>");
  body += F("<div class='sep'></div>");
  body += F("<a class='btn btn2' href='/' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_OPEN_HOME;
  body += F("</a>");
  body += F("</div>");
  body += F("<script>");
  body += F("(function(){");
  body += F("function sleep(ms){return new Promise(function(r){setTimeout(r,ms);});}");
  body += F("async function check(){");
  body += F("try{");
  body += F("var r=await fetch('/status.json',{cache:'no-store'});");
  body += F("if(!r.ok) return false;");
  body += F("var j=await r.json();");
  body += F("if(j && (j.wifi_status_code==='connected' || j.ap_running)) return true;");
  body += F("return false;");
  body += F("}catch(e){return false;}");
  body += F("}");
  body += F("async function loop(){");
  body += F("var t0=Date.now();");
  body += F("for(;;){");
  body += F("var ok=await check();");
  body += F("if(ok){window.location.replace('/');return;}");
  body += F("var dt=Date.now()-t0;");
  body += F("if(dt>60000){");
  body += F("var m=document.getElementById('msg'); if(m) m.textContent='");
  body += NOCTUA_I18N_REBOOT_TOO_LONG;
  body += F("';");
  body += F("var d=document.getElementById('detail'); if(d) d.textContent=\"");
  body += NOCTUA_I18N_REBOOT_TOO_LONG_DETAIL;
  body += F("\";");
  body += F("return;}");
  body += F("var wait=(dt<3000)?1000:2500;");
  body += F("await sleep(wait);");
  body += F("}");
  body += F("}");
  body += F("loop();");
  body += F("})();");
  body += F("</script>");

  gServer.send(200, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_REBOOTING), body));
}

static void handleLoginGet() {
  if (!authRequired()) {
    sendRedirect("/admin");
    return;
  }

  if (isLoggedIn()) {
    sendRedirect("/admin");
    return;
  }

  gServer.sendHeader("Cache-Control", "no-store");
  String body;
  body.reserve(1400);

  body += F("<div class='card'>");
  body += F("<h1>");
  body += NOCTUA_I18N_TITLE_LOGIN;
  body += F("</h1>");
  body += F("<p class='muted'>");
  body += NOCTUA_I18N_LOGIN_HINT;
  body += F("</p>");
  body += F("<form method='POST' action='/login'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_PASSWORD;
  body += F("</label>");
  body += F("<input type='password' name='pass' placeholder='");
  body += NOCTUA_I18N_PLACEHOLDER_ADMIN_PASSWORD;
  body += F("' autofocus>");
  body += F("<button class='btn' type='submit'>");
  body += NOCTUA_I18N_BTN_LOGIN_SUBMIT;
  body += F("</button> ");
  body += F("<a class='btn btn2' href='/' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_BACK;
  body += F("</a>");
  body += F("</form>");
  body += F("</div>");

  gServer.send(200, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_LOGIN), body));
}

static void handleLoginPost() {
  gServer.sendHeader("Cache-Control", "no-store");

  if (!authRequired()) {
    sendRedirect("/admin");
    return;
  }

  String passInput = gServer.arg("pass");
  passInput.trim();

  const char* storedPass = gCfg.adminPass;
  
  bool passwordMatch = false;
  if (strlen(storedPass) > 0 && passInput.length() == strlen(storedPass)) {
    passwordMatch = true;
    for (size_t i = 0; i < passInput.length(); i++) {
      if (passInput[i] != storedPass[i]) {
        passwordMatch = false;
        break;
      }
    }
  }

  if (passwordMatch) {
    gLoginExpireMs = millis() + LOGIN_TIMEOUT_MS;
    sendRedirect("/admin");
    return;
  }

  String body;
  body.reserve(1500);

  body += F("<div class='card'>");
  body += F("<h1>");
  body += NOCTUA_I18N_TITLE_LOGIN;
  body += F("</h1>");
  body += F("<p class='muted'>");
  body += NOCTUA_I18N_LOGIN_HINT;
  body += F("</p>");
  body += F("<p style='color:#b00020;margin:6px 0 8px;'><b>");
  body += NOCTUA_I18N_LOGIN_WRONG_PASSWORD;
  body += F("</b></p>");
  body += F("<form method='POST' action='/login'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_PASSWORD;
  body += F("</label>");
  body += F("<input type='password' name='pass' placeholder='");
  body += NOCTUA_I18N_PLACEHOLDER_ADMIN_PASSWORD;
  body += F("' autofocus>");
  body += F("<button class='btn' type='submit'>");
  body += NOCTUA_I18N_BTN_LOGIN_SUBMIT;
  body += F("</button> ");
  body += F("<a class='btn btn2' href='/' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_BACK;
  body += F("</a>");
  body += F("</form>");
  body += F("</div>");

  gServer.send(401, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_LOGIN), body));
}

 

static void handleAdmin() {
  gServer.sendHeader("Cache-Control", "no-store");

  if (!isAuthorized()) {
    sendRedirect("/login");
    return;
  }

  String body;
  body.reserve(3200);

  body += F("<div class='card'>");
  body += F("<h1>");
  body += NOCTUA_I18N_TITLE_CONFIGURE;
  body += F("</h1>");
  body += F("<p class='muted'>");
  body += NOCTUA_I18N_CONFIG_HINT;
  body += F("</p>");

  body += F("<form id='saveForm' method='POST' action='/save' onsubmit='return onSaveSubmit()'>");
  body += F("<div class='row'>");

  body += F("<div class='field'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_WIFI_SSID;
  body += F("</label>");
  body += F("<input name='ssid' placeholder='");
  body += NOCTUA_I18N_PLACEHOLDER_SSID;
  body += F("' value='");
  body += htmlEscape(String(gCfg.wifiSsid));
  body += F("'>");
  body += F("</div>");

  body += F("<div class='field'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_WIFI_PASSWORD;
  body += F("</label>");
  body += F("<input name='pass' type='password' placeholder='");
  body += NOCTUA_I18N_LABEL_PASSWORD;
  body += F("' value='");
  body += htmlEscape(String(gCfg.wifiPass));
  body += F("'>");
  body += F("</div>");

  body += F("<div class='field'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_ADMIN_PASSWORD;
  body += F("</label>");
  body += F("<input name='admin' type='password' placeholder='");
  body += NOCTUA_I18N_PLACEHOLDER_OPTIONAL;
  body += F("' value='");
  body += htmlEscape(String(gCfg.adminPass));
  body += F("'>");
  body += F("</div>");

  body += F("<div class='field'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_CONFIRM_ADMIN_PASSWORD;
  body += F("</label>");
  body += F("<input name='admin2' type='password' placeholder='");
  body += NOCTUA_I18N_PLACEHOLDER_REPEAT_PASSWORD;
  body += F("' value='");
  body += htmlEscape(String(gCfg.adminPass));
  body += F("'>");
  body += F("</div>");

  body += F("<div class='field'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_CHANNEL_KEY;
  body += F("</label>");
  body += F("<input name='channel' placeholder='");
  body += NOCTUA_I18N_LABEL_CHANNEL_KEY;
  body += F("' value='");
  body += htmlEscape(String(gCfg.channelKey));
  body += F("'>");
  body += F("</div>");

  body += F("<div class='field'>");
  body += F("<label>");
  body += NOCTUA_I18N_LABEL_LED;
  body += F("</label>");
  body += F("<label style='display:flex;align-items:flex-start;gap:10px;margin-top:6px;max-width:100%;'>");
  body += F("<input type='checkbox' name='led_on' style='margin-top:2px;flex:0 0 auto;'");
  if (!gCfg.ledDisabled) body += F(" checked");
  body += F("> ");
  body += F("<span style='flex:1 1 auto;min-width:0;word-break:break-word;overflow-wrap:anywhere;'>");
  body += NOCTUA_I18N_LED_ENABLED;
  body += F("</span>");
  body += F("</label>");
  body += F("</div>");

  body += F("</div>");
  body += F("</form>");

  // Action row: buttons live together, forms stay separate (no nesting)
  body += F("<div style='display:flex;justify-content:space-between;align-items:center;margin-top:12px;gap:8px;'>");
  body += F("<div>");
  body += F("<button class='btn' type='submit' form='saveForm'>");
  body += NOCTUA_I18N_BTN_SAVE;
  body += F("</button> ");
  body += F("<a class='btn btn2' href='/' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_CANCEL;
  body += F("</a>");
  body += F("</div>");

  body += F("<div style='display:flex;gap:8px;align-items:center;'>");
  body += F("<a class='btn btn2' href='/update' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_FIRMWARE_UPDATE;
  body += F("</a>");
  body += F("<form method='GET' action='/reset-config' style='display:inline;margin:0;'>");
  body += F("<button class='btn btnDanger' type='submit'>");
  body += NOCTUA_I18N_BTN_CLEAR_FLASH;
  body += F("</button>");
  body += F("</form>");
  body += F("</div>");
  body += F("</div>");

  // full-screen overlay shown while saving
  body += F("<div id='saveOverlay' class='overlay'><div class='spinner'></div></div>");
  body += F("<script>");
  body += F("function onSaveSubmit(){var a=document.querySelector('input[name=admin]'); var b=document.querySelector('input[name=admin2]'); if(a&&b&&a.value!==b.value){alert('");
  body += NOCTUA_I18N_ALERT_ADMIN_PASSWORDS_MISMATCH;
  body += F("'); return false;} var o=document.getElementById('saveOverlay'); if(o) o.style.display='flex'; return true;}");
  body += F("</script>");

  body += F("</div>");

  gServer.send(200, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_CONFIGURE), body));
}

static void handleUpdateGet() {
  gServer.sendHeader("Cache-Control", "no-store");

  if (!isAuthorized()) {
    sendRedirect("/login");
    return;
  }

  if (!isAdminPasswordSet()) {
    String body;
    body.reserve(900);
    body += F("<div class='card'>");
    body += F("<h1>");
    body += NOCTUA_I18N_TITLE_FIRMWARE_UPDATE;
    body += F("</h1>");
    body += F("<p class='stBad'>");
    body += NOCTUA_I18N_OTA_NEED_ADMIN_PASS_FIRST;
    body += F("</p>");
    body += F("<a class='btn btn2' href='/admin' style='text-decoration:none;'>");
    body += NOCTUA_I18N_BTN_BACK;
    body += F("</a>");
    body += F("</div>");
    gServer.send(403, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_FIRMWARE_UPDATE), body));
    return;
  }

  String body;
  body.reserve(1400);
  body += F("<div class='card'>");
  body += F("<h1>");
  body += NOCTUA_I18N_TITLE_FIRMWARE_UPDATE;
  body += F("</h1>");
  body += F("<p class='muted'>");
  body += NOCTUA_I18N_OTA_UPLOAD_HELP;
  body += F("</p>");
  body += F("<form method='POST' action='/update' enctype='multipart/form-data' style='margin-top:10px;'>");
  body += F("<input type='file' name='firmware' accept='.bin,application/octet-stream' required>");
  body += F("<div style='display:flex;gap:10px;flex-wrap:wrap;margin-top:10px;'>");
  body += F("<button class='btn' type='submit'>");
  body += NOCTUA_I18N_BTN_UPDATE;
  body += F("</button>");
  body += F("<a class='btn btn2' href='/admin' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_CANCEL;
  body += F("</a>");
  body += F("</div>");
  body += F("</form>");
  body += F("</div>");

  gServer.send(200, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_FIRMWARE_UPDATE), body));
}

static void handleUpdatePost() {
  gServer.sendHeader("Cache-Control", "no-store");

  if (!isAuthorized()) {
    sendRedirect("/login");
    return;
  }

  if (!isAdminPasswordSet()) {
    gServer.send(403, "text/plain; charset=utf-8", String(NOCTUA_I18N_OTA_ADMIN_PASSWORD_REQUIRED));
    return;
  }

  if (Update.hasError()) {
    Serial.printf("❌ [OTA] Update failed: err=%d bytes=%lu stage='%s'\n",
                  (int)Update.getError(),
                  (unsigned long)gOtaBytesReceived,
                  gOtaLastErrorMsg);
    Update.printError(Serial);

    String body;
    body.reserve(1500);
    body += F("<div class='card'>");
    body += F("<h1>");
    body += NOCTUA_I18N_TITLE_FIRMWARE_UPDATE;
    body += F("</h1>");
    body += F("<p class='stBad'>");
    body += NOCTUA_I18N_OTA_UPDATE_FAILED;
    body += F("</p>");
    body += F("<p class='muted'>");
    body += NOCTUA_I18N_OTA_ERROR_LABEL;
    body += F(" <code>");
    body += String((int)Update.getError());
    body += F("</code>");
    if (gOtaLastErrorMsg[0]) {
      body += F(" · ");
      body += htmlEscape(String(gOtaLastErrorMsg));
    }
    body += F("</p>");
    body += F("<p class='muted'>");
    body += NOCTUA_I18N_OTA_RECEIVED_LABEL;
    body += F(" <code>");
    body += String((unsigned long)gOtaBytesReceived);
    body += F("</code> ");
    body += NOCTUA_I18N_OTA_BYTES;
    body += F("</p>");
    body += F("<a class='btn btn2' href='/update' style='text-decoration:none;'>");
    body += NOCTUA_I18N_BTN_BACK;
    body += F("</a>");
    body += F("</div>");
    gServer.send(500, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_FIRMWARE_UPDATE), body));
    return;
  }

  sendRebootingPage();
  gOtaRebootPending = true;
  gOtaRebootDueMs = millis() + 800;
}

static void handleUpdateUpload() {
  if (!isAuthorized()) return;
  if (!isAdminPasswordSet()) return;

  HTTPUpload& upload = gServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaClearError();

    Update.runAsync(true);
    const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace, U_FLASH)) {
      otaSetError(F("begin failed"));
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning()) {
      const size_t written = Update.write(upload.buf, upload.currentSize);
      gOtaBytesReceived += (uint32_t)written;
      if (written != upload.currentSize) {
        otaSetError(F("write failed"));
      }
      yield();
    } else {
      // begin failed; count what we got for diagnostics
      gOtaBytesReceived += (uint32_t)upload.currentSize;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.isRunning()) {
      if (!Update.end(true)) {
        otaSetError(F("end failed"));
      }
      yield();
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) {
      (void)Update.end();
      yield();
    }
    otaSetError(F("upload aborted"));
  }
}

static void handleResetConfigGet() {
  gServer.sendHeader("Cache-Control", "no-store");

  if (!isAuthorized()) {
    sendRedirect("/login");
    return;
  }

  String body;
  body.reserve(1200);

  body += F("<div class='card'>");
  body += F("<h1>");
  body += NOCTUA_I18N_TITLE_CLEAR_FLASH;
  body += F("</h1>");
  body += F("<p>");
  body += NOCTUA_I18N_CLEAR_FLASH_CONFIRM;
  body += F("</p>");
  body += F("<div style='display:flex;gap:10px;flex-wrap:wrap;margin-top:10px;'>");
  body += F("<form method='POST' action='/reset-config' style='display:inline;margin:0;'>");
  body += F("<button class='btn btnDanger' type='submit'>");
  body += NOCTUA_I18N_BTN_YES_CLEAR;
  body += F("</button>");
  body += F("</form>");
  body += F("<a class='btn btn2' href='/admin' style='text-decoration:none;'>");
  body += NOCTUA_I18N_BTN_CANCEL;
  body += F("</a>");
  body += F("</div>");
  body += F("</div>");

  gServer.send(200, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_CLEAR_FLASH), body));
}

static void handleSave() {
  gServer.sendHeader("Cache-Control", "no-store");

  if (!isAuthorized()) {
    sendRedirect("/login");
    return;
  }

  const String ssid = gServer.arg("ssid");
  const String pass = gServer.arg("pass");
  const String channel = gServer.arg("channel");
  const String admin = gServer.arg("admin");
  const String admin2 = gServer.arg("admin2");
  const bool ledOn = gServer.hasArg("led_on");

  // Admin password must be entered twice to avoid accidental lockout.
  if (admin != admin2) {
    String body;
    body.reserve(900);
    body += F("<div class='card'>");
    body += F("<h1>");
    body += NOCTUA_I18N_TITLE_CONFIGURE;
    body += F("</h1>");
    body += F("<p class='stBad' style='margin:0 0 10px 0;'>");
    body += NOCTUA_I18N_ADMIN_CONFIRM_MISMATCH;
    body += F("</p>");
    body += F("<a class='btn btn2' href='javascript:history.back()' style='text-decoration:none;'>");
    body += NOCTUA_I18N_BTN_BACK;
    body += F("</a>");
    body += F("</div>");
    gServer.send(400, "text/html; charset=utf-8", pageShell(String(NOCTUA_I18N_TITLE_CONFIGURE), body));
    return;
  }

  copyToBuf(gCfg.wifiSsid, sizeof(gCfg.wifiSsid), ssid);
  copyToBuf(gCfg.wifiPass, sizeof(gCfg.wifiPass), pass);
  copyToBuf(gCfg.channelKey, sizeof(gCfg.channelKey), channel);
  copyToBuf(gCfg.adminPass, sizeof(gCfg.adminPass), admin);
  gCfg.ledDisabled = !ledOn;

  // Forget login immediately after saving
  gLoginExpireMs = 0;

  // Respond first, then write config + reboot from portalLoop().
  // (Avoid long flash/FS work inside an HTTP handler.)
  sendRebootingPage();

  gSaveAndRebootPending = true;
  gSaveAndRebootDueMs = millis() + 250;
}

static void handleResetConfig() {
  gServer.sendHeader("Cache-Control", "no-store");

  if (!isAuthorized()) {
    sendRedirect("/login");
    return;
  }

  sendRebootingPage();

  // Do not touch flash/FS here (can trigger WDT). Just mark and reboot.
  markResetConfigOnNextBoot();

  gResetConfigPending = true;
  // Give TCP some time to flush the response before we reboot.
  gResetConfigDueMs = millis() + 600;
}

static void handleNotFound() {
  // Captive portals often probe random URLs -> redirect all to root
  sendRedirect("/");
}

// ============================================================
// Config persistence
// ============================================================

bool portalLoadConfig(NoctuaConfig& cfg) {
  if (!fsBeginWithFormatFallback()) return false;
  if (!LittleFS.exists(CFG_PATH)) return false;

  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;

  bool applied = false;
  while (f.available()) {
    const String line = f.readStringUntil('\n');
    const int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String k = line.substring(0, eq);
    String v = line.substring(eq + 1);
    k.trim();
    v.trim();

    if (k == F("ssid")) {
      copyToBuf(cfg.wifiSsid, sizeof(cfg.wifiSsid), v);
      applied = true;
    } else if (k == F("pass")) {
      copyToBuf(cfg.wifiPass, sizeof(cfg.wifiPass), v);
      applied = true;
    } else if (k == F("channel")) {
      copyToBuf(cfg.channelKey, sizeof(cfg.channelKey), v);
      applied = true;
    } else if (k == F("admin")) {
      copyToBuf(cfg.adminPass, sizeof(cfg.adminPass), v);
      applied = true;
    } else if (k == F("led_off")) {
      cfg.ledDisabled = (v == F("1") || v == F("true") || v == F("on"));
      applied = true;
    }
  }

  f.close();
  return applied;
}

bool portalSaveConfig(const NoctuaConfig& cfg) {
  // Runtime save should never format flash. If mount fails, fail fast.
  if (!fsBeginNoFormat()) return false;

  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;

  f.print(F("ssid="));
  f.println(cfg.wifiSsid);
  f.print(F("pass="));
  f.println(cfg.wifiPass);
  f.print(F("channel="));
  f.println(cfg.channelKey);
  f.print(F("admin="));
  f.println(cfg.adminPass);

  f.print(F("led_off="));
  f.println(cfg.ledDisabled ? F("1") : F("0"));

  f.close();
  LittleFS.end();
  return true;
}

// ============================================================
// Portal lifecycle
// ============================================================

void portalSetup(const char* apName, const char* apPass) {
  if (apName && apName[0]) strlcpy(gApSsid, apName, sizeof(gApSsid));
  if (apPass && apPass[0]) strlcpy(gApPass, apPass, sizeof(gApPass));

  fsBeginWithFormatFallback();
  portalLoadConfig(gCfg);

  gServer.on("/", handleRoot);
  gServer.on("/status.json", handleStatusJson);

  gServer.on("/login", HTTP_GET, handleLoginGet);
  gServer.on("/login", HTTP_POST, handleLoginPost);
  gServer.on("/admin", handleAdmin);
  gServer.on("/save", HTTP_POST, handleSave);
  gServer.on("/reset-config", HTTP_GET, handleResetConfigGet);
  gServer.on("/reset-config", HTTP_POST, handleResetConfig);
  gServer.on("/update", HTTP_GET, handleUpdateGet);
  gServer.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);
  gServer.onNotFound(handleNotFound);

  gServer.begin();
}

void portalLoop() {
  if (gApRunning) {
    gDns.processNextRequest();
  }
  gServer.handleClient();

  if (gResetConfigPending && (int32_t)(millis() - gResetConfigDueMs) >= 0) {
    gResetConfigPending = false;
    safeRestart();
  }

  if (gSaveAndRebootPending && (int32_t)(millis() - gSaveAndRebootDueMs) >= 0) {
    gSaveAndRebootPending = false;

    // LittleFS operations can be slow; guard against WDT during this section.
    ESP.wdtDisable();
    (void)portalSaveConfig(gCfg);

    delay(50);
    yield();
    safeRestart();
  }

  if (gOtaRebootPending && (int32_t)(millis() - gOtaRebootDueMs) >= 0) {
    gOtaRebootPending = false;
    safeRestart();
  }
}

void portalStartAP() {
  WiFi.mode(WIFI_AP_STA);

  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(gApSsid, gApPass);

  gDns.start(DNS_PORT, "*", AP_IP);
  gApRunning = true;
}

void portalStopAP() {
  if (!gApRunning) return;
  gDns.stop();
  WiFi.softAPdisconnect(true);
  gApRunning = false;
}

bool portalIsAPRunning() { return gApRunning; }
