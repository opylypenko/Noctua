#pragma once

// Compile-time UI language selection.
// Define NOCTUA_LANG_UA to build Ukrainian UI.
// Default (no define) builds English UI.
// Only the selected language strings are compiled into the firmware.

#if defined(NOCTUA_LANG_UA)
  #include "noctua_i18n_ua.h"
#else
  #include "noctua_i18n_en.h"
#endif
