# Noctua

Firmware for ESP8266 boards with a built-in web portal.

- Language: English / Ukrainian (compile-time selection)
- Boards: ESP-01 (1MB) and Wemos D1 mini
- Build system: PlatformIO

## Web installer

A simple WebSerial installer is available via GitHub Pages:

- https://opylypenko.github.io/Noctua/

It works in Chrome/Edge over HTTPS.

## Build

```bash
pio run -e noctua -e noctua_ua -e d1_mini -e d1_mini_ua
```

## Upload

Example (adjust the port):

```bash
pio run -t upload -e d1_mini --upload-port /dev/cu.usbserial-110
```

## Українська версія

See [README.uk.md](README.uk.md).
