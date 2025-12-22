#pragma once

#include <WString.h>

// English UI strings (selected at compile time via include/noctua_i18n.h)

// Status / common
#define NOCTUA_I18N_DASH F("—")

#define NOCTUA_I18N_WIFI_STATUS_NEED_CFG F("Need configuration")
#define NOCTUA_I18N_WIFI_STATUS_CONNECTED F("Connected")
#define NOCTUA_I18N_WIFI_STATUS_CONNECTING F("Connecting")
#define NOCTUA_I18N_WIFI_STATUS_NO_SSID F("No SSID")
#define NOCTUA_I18N_WIFI_STATUS_CONNECT_FAILED F("Connect failed")
#define NOCTUA_I18N_WIFI_STATUS_WRONG_PASSWORD F("Wrong password")
#define NOCTUA_I18N_WIFI_STATUS_DISCONNECTED F("Disconnected")
#define NOCTUA_I18N_WIFI_STATUS_UNKNOWN F("Unknown")

#define NOCTUA_I18N_INTERNET_UNKNOWN F("Unknown")
#define NOCTUA_I18N_INTERNET_REACHABLE F("Reachable")
#define NOCTUA_I18N_INTERNET_NO_ROUTE F("No route")

#define NOCTUA_I18N_STATUS_WIFI_CFG_PILL_HTML \
  F("<b>WiFi:</b> <code><span class='stWarn'>Need configuration</span></code>")

#define NOCTUA_I18N_STATUS_INTERNET_PREFIX_HTML \
  F("<b>Internet:</b> <code><span id='val_internet'>")

// Home page
#define NOCTUA_I18N_HOME_SUBTITLE_PREFIX F("Svitlobot Service Monitor · Uptime: ")
#define NOCTUA_I18N_HOME_STATUS_PREFIX_HTML F("<b>Status:</b> ")

#define NOCTUA_I18N_API_WAITING F("Waiting")
#define NOCTUA_I18N_API_OK F("Ok")
#define NOCTUA_I18N_API_FAIL F("Fail")

#define NOCTUA_I18N_BTN_CONFIGURE F("Configure")

// JS i18n tokens
#define NOCTUA_I18N_JS_NOW F("now")
#define NOCTUA_I18N_JS_SEC F("s")

// Footer
#define NOCTUA_I18N_FOOTER_FW_LABEL F("FW:")

// Rebooting page
#define NOCTUA_I18N_TITLE_REBOOTING F("Rebooting")
#define NOCTUA_I18N_H1_REBOOTING F("Rebooting...")
#define NOCTUA_I18N_REBOOT_MSG F("Applying changes and restarting.")
#define NOCTUA_I18N_REBOOT_DETAIL F("Waiting for device to come back online.")
#define NOCTUA_I18N_BTN_OPEN_HOME F("Open home")
#define NOCTUA_I18N_REBOOT_TOO_LONG F("Device is taking too long to come back.")
#define NOCTUA_I18N_REBOOT_TOO_LONG_DETAIL \
  F("If Wi-Fi is not available, the device may come up as AP 'Noctua' at http://192.168.4.1/")

// Login
#define NOCTUA_I18N_TITLE_LOGIN F("Login")
#define NOCTUA_I18N_LOGIN_HINT F("Enter admin password")
#define NOCTUA_I18N_LABEL_PASSWORD F("Password")
#define NOCTUA_I18N_PLACEHOLDER_ADMIN_PASSWORD F("Admin password")
#define NOCTUA_I18N_BTN_LOGIN_SUBMIT F("Login")
#define NOCTUA_I18N_BTN_BACK F("Back")
#define NOCTUA_I18N_LOGIN_WRONG_PASSWORD F("Wrong password")

// Admin / Configure
#define NOCTUA_I18N_TITLE_CONFIGURE F("Configure")
#define NOCTUA_I18N_CONFIG_HINT F("Wi-Fi and app settings")

#define NOCTUA_I18N_LABEL_WIFI_SSID F("Wi-Fi SSID")
#define NOCTUA_I18N_PLACEHOLDER_SSID F("SSID")

#define NOCTUA_I18N_LABEL_WIFI_PASSWORD F("Wi-Fi Password")

#define NOCTUA_I18N_LABEL_ADMIN_PASSWORD F("Admin password")
#define NOCTUA_I18N_PLACEHOLDER_OPTIONAL F("(optional)")

#define NOCTUA_I18N_LABEL_CONFIRM_ADMIN_PASSWORD F("Confirm admin password")
#define NOCTUA_I18N_PLACEHOLDER_REPEAT_PASSWORD F("(repeat password)")

#define NOCTUA_I18N_LABEL_CHANNEL_KEY F("Channel key")
#define NOCTUA_I18N_LABEL_LED F("LED")
#define NOCTUA_I18N_LED_ENABLED F("LED enabled")

#define NOCTUA_I18N_BTN_SAVE F("Save")
#define NOCTUA_I18N_BTN_CANCEL F("Cancel")
#define NOCTUA_I18N_BTN_FIRMWARE_UPDATE F("Firmware update")
#define NOCTUA_I18N_BTN_CLEAR_FLASH F("Clear Flash")

#define NOCTUA_I18N_ALERT_ADMIN_PASSWORDS_MISMATCH F("Admin passwords do not match")

// OTA Update
#define NOCTUA_I18N_TITLE_FIRMWARE_UPDATE F("Firmware update")
#define NOCTUA_I18N_OTA_NEED_ADMIN_PASS_FIRST \
  F("Set an admin password first to enable OTA updates.")
#define NOCTUA_I18N_OTA_UPLOAD_HELP \
  F("Upload firmware binary (.bin). Use the file built by PlatformIO: <code>.pio/build/esp01_1m/firmware.bin</code>. The device will reboot after update.")
#define NOCTUA_I18N_BTN_UPDATE F("Update")
#define NOCTUA_I18N_OTA_ADMIN_PASSWORD_REQUIRED \
  F("Admin password is required for OTA updates")
#define NOCTUA_I18N_OTA_UPDATE_FAILED F("Update failed.")
#define NOCTUA_I18N_OTA_ERROR_LABEL F("Error:")
#define NOCTUA_I18N_OTA_RECEIVED_LABEL F("Received:")
#define NOCTUA_I18N_OTA_BYTES F("bytes")

// Clear flash
#define NOCTUA_I18N_TITLE_CLEAR_FLASH F("Clear Flash")
#define NOCTUA_I18N_CLEAR_FLASH_CONFIRM \
  F("This will erase all saved settings (Wi-Fi, channel key, admin password) and reboot the device.")
#define NOCTUA_I18N_BTN_YES_CLEAR F("Yes, clear")

// Save errors
#define NOCTUA_I18N_ADMIN_CONFIRM_MISMATCH \
  F("Admin password confirmation does not match.")
