# Noctua

Прошивка для плат ESP8266 з вбудованим веб‑порталом.

- Мови: англійська / українська (вибір під час компіляції)
- Плати: ESP-01 (1MB) та Wemos D1 mini
- Система збірки: PlatformIO

## Веб‑інсталятор

Простий WebSerial інсталятор (GitHub Pages):

- https://opylypenko.github.io/Noctua/

Працює в Chrome/Edge через HTTPS.

## Збірка

```bash
pio run -e noctua -e noctua_ua -e d1_mini -e d1_mini_ua
```

## Прошивка через USB

Приклад (заміни порт під себе):

```bash
pio run -t upload -e d1_mini --upload-port /dev/cu.usbserial-110
```

## English version

See [README.md](README.md).
