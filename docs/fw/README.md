# Firmware files

This folder is served by GitHub Pages and used by the web installer at `docs/index.html`.

- Files are named with the current git commit id, e.g. `*_47d86bb.bin`.
- When you publish a new build, rebuild with PlatformIO and replace these files + update `BUILD_ID` in `docs/index.html`.

Build commands:
- `pio run -e noctua -e noctua_ua -e d1_mini -e d1_mini_ua`
