# Firmware files

This folder is served by GitHub Pages and used by the web installer at `docs/index.html`.

- Files are named with a release id, e.g. `*_v1.0.0.bin`.
- When you publish a new build, rebuild with PlatformIO and replace these files + update `BUILD_ID` in `docs/index.html`.

Build commands:
- `pio run -e noctua -e noctua_ua -e d1_mini -e d1_mini_ua`
