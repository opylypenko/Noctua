//api_client.cpp

#include "api_client.h"

#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include "noctua_portal.h"

// ============================================================
// Config
// ============================================================

static const char* gHost = "api.svitlobot.in.ua";
static const uint16_t gPort = 80;
static const char* gPingEndpoint = "/channelPing";

static ApiError gLastErr = ApiError::None;

static IPAddress gHostIp;
static uint32_t gHostIpResolvedMs = 0;
static const uint32_t HOST_IP_TTL_MS = 10UL * 60UL * 1000UL;  // 10 minutes

// ============================================================
// Internal helpers
// ============================================================

static void setErr(ApiError e) { gLastErr = e; }

static bool ipIsNonZero(const IPAddress& ip) {
  return ((uint8_t)ip[0] | (uint8_t)ip[1] | (uint8_t)ip[2] | (uint8_t)ip[3]) != 0;
}

static bool resolveHostIp(IPAddress& out) {
  if (!gHost || gHost[0] == 0) return false;

  const uint32_t now = millis();
  if (gHostIpResolvedMs != 0 && (now - gHostIpResolvedMs) < HOST_IP_TTL_MS && ipIsNonZero(gHostIp)) {
    out = gHostIp;
    return true;
  }

  IPAddress ip;
  yield();
  const bool ok = WiFi.hostByName(gHost, ip);
  yield();

  if (ok && ipIsNonZero(ip)) {
    gHostIp = ip;
    gHostIpResolvedMs = now;
    out = ip;
    return true;
  }

  return false;
}

// URL-encodes a string (percent-encoding for query parameters).
static String urlEncode(const char* str) {
  String encoded;
  if (!str) return encoded;
  
  for (size_t i = 0; str[i] != '\0'; i++) {
    char c = str[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// Reads a single CRLF/LF-terminated line with a hard deadline.
// - Trims '\r'
// - Stops at '\n' or when buffer is full (keeps it NUL-terminated)
// Returns true if any chars were read before deadline.
static bool readLineWithDeadline(WiFiClient& client, char* buf, size_t bufSize, uint32_t deadlineMs) {
  if (bufSize == 0) return false;
  buf[0] = 0;

  size_t n = 0;

  while (millis() < deadlineMs) {
    if (!client.available()) {
      if (!client.connected()) break;
      delay(2);
      yield();
      continue;
    }

    const int c = client.read();
    if (c < 0) continue;

    if (c == '\r') continue;
    if (c == '\n') break;

    if (n + 1 < bufSize) {
      buf[n++] = (char)c;
      buf[n] = 0;
    } else {
      // Buffer full: keep consuming until end of line.
    }
  }

  return (n > 0);
}

// Parses HTTP status code from "HTTP/1.1 200 OK".
// Returns true on success and writes code.
static bool parseHttpStatusCode(const char* statusLine, int& codeOut) {
  codeOut = 0;
  if (!statusLine || statusLine[0] == 0) return false;

  const char* sp = strchr(statusLine, ' ');
  if (!sp) return false;

  while (*sp == ' ') sp++;

  if (!(sp[0] >= '0' && sp[0] <= '9')) return false;
  if (!(sp[1] >= '0' && sp[1] <= '9')) return false;
  if (!(sp[2] >= '0' && sp[2] <= '9')) return false;

  codeOut = (sp[0] - '0') * 100 + (sp[1] - '0') * 10 + (sp[2] - '0');
  return true;
}

// Reads and discards HTTP headers until the empty line.
static bool discardHttpHeaders(WiFiClient& client, uint32_t deadlineMs) {
  char line[128];

  while (millis() < deadlineMs) {
    if (client.available()) {
      const int p = client.peek();
      if (p == '\r' || p == '\n') {
        while (client.available()) {
          const int c = client.read();
          if (c == '\n') break;
        }
        return true;
      }
    }

    const bool got = readLineWithDeadline(client, line, sizeof(line), deadlineMs);
    if (!got) {
      if (!client.connected() && !client.available()) return false;
      continue;
    }

    if (line[0] == 0) return true;
  }

  return false;
}

// Performs minimal HTTP GET and returns parsed status code.
// Returns true if a status code was obtained (even if non-200).
static bool httpGetStatus(const char* host, uint16_t port, const char* pathAndQuery, int& statusCodeOut, char* bodyBufOut = nullptr, size_t bodyBufSize = 0) {
  statusCodeOut = 0;
  if (bodyBufOut && bodyBufSize > 0) bodyBufOut[0] = 0;

  IPAddress ip;
  if (!resolveHostIp(ip)) {
    setErr(ApiError::DnsFailed);
    return false;
  }

  WiFiClient client;
  client.setTimeout(1500);

  // Connect using pre-resolved IP to avoid potentially long blocking DNS inside connect(hostname).
  if (!client.connect(ip, port)) {
    setErr(ApiError::ConnectFailed);
    return false;
  }

  size_t w = 0;
  w += client.print(F("GET "));
  w += client.print(pathAndQuery);
  w += client.print(F(" HTTP/1.1\r\nHost: "));
  w += client.print(host);
  w += client.print(F("\r\nConnection: close\r\n\r\n"));

  if (w == 0) {
    client.stop();
    setErr(ApiError::WriteFailed);
    return false;
  }

  const uint32_t deadline = millis() + 5000;

  char statusLine[96];
  const bool gotLine = readLineWithDeadline(client, statusLine, sizeof(statusLine), deadline);
  if (!gotLine) {
    client.stop();
    setErr(ApiError::ReadTimeout);
    return false;
  }

  int code = 0;
  if (!parseHttpStatusCode(statusLine, code)) {
    client.stop();
    setErr(ApiError::BadStatusLine);
    return false;
  }

  (void)discardHttpHeaders(client, deadline);

  // Capture response body (up to bodyBufSize bytes) for error logging.
  if (bodyBufOut && bodyBufSize > 0) {
    size_t nRead = 0;
    while (nRead < bodyBufSize - 1 && millis() < deadline) {
      if (client.available()) {
        const int c = client.read();
        if (c < 0) break;
        bodyBufOut[nRead++] = (char)c;
      } else {
        if (!client.connected()) break;
        delay(1);
        yield();
      }
    }
    bodyBufOut[nRead] = 0;
  }

  client.stop();

  statusCodeOut = code;
  return true;
}

// ============================================================
// Public API
// ============================================================

ApiError apiLastError() { return gLastErr; }

const char* apiLastErrorText() {
  switch (gLastErr) {
    case ApiError::None: return "none";
    case ApiError::NoHost: return "no host";
    case ApiError::DnsFailed: return "dns failed";
    case ApiError::NoWiFi: return "wifi not connected";
    case ApiError::NoAppConfig: return "no channel key";
    case ApiError::ConnectFailed: return "connect failed";
    case ApiError::WriteFailed: return "write failed";
    case ApiError::ReadTimeout: return "read timeout";
    case ApiError::BadStatusLine: return "bad status line";
    case ApiError::Non200: return "non-200";
    default: return "unknown";
  }
}

bool apiPing() {
  setErr(ApiError::None);

  if (!gHost || gHost[0] == 0) {
    setErr(ApiError::NoHost);
    portalSetHostReachable(false);
    portalSetPingError(apiLastErrorText());
    portalSetPingStatus(false);
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    setErr(ApiError::NoWiFi);
    portalSetHostReachable(false);
    portalSetPingError(apiLastErrorText());
    portalSetPingStatus(false);
    return false;
  }

  if (!portalHasAppConfig()) {
    setErr(ApiError::NoAppConfig);
    portalSetHostReachable(false);
    portalSetPingError(apiLastErrorText());
    portalSetPingStatus(false);
    return false;
  }

  const char* key = portalConfig().channelKey;
  String encodedKey = urlEncode(key ? key : "");
  
  char path[200];
  snprintf(path, sizeof(path), "%s?channel_key=%s", gPingEndpoint, encodedKey.c_str());

  int code = 0;
  char body[192] = {0};
  const bool gotCode = httpGetStatus(gHost, gPort, path, code, body, sizeof(body));

  // Host reachable == we got a valid HTTP status code.
  portalSetHostReachable(gotCode);

  const bool ok = gotCode && (code >= 200) && (code < 300);
  if (!ok && gotCode) {
    setErr(ApiError::Non200);
    if (body[0]) {
      // Report server response body to portal UI for debugging
      portalSetPingError(body);
      Serial.printf("apiPing: HTTP %d body: %s\n", code, body);
    } else {
      char err[32];
      snprintf(err, sizeof(err), "HTTP %d", code);
      portalSetPingError(err);
      Serial.printf("apiPing: HTTP %d (no body)\n", code);
    }
  } else {
    if (!gotCode) {
      portalSetPingError(apiLastErrorText());
    } else {
      portalSetPingError("");
    }
  }

  portalSetPingStatus(ok);

  return ok;
}
