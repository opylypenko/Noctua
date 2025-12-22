#pragma once

#include <WString.h>

// Ukrainian UI strings (selected at compile time via include/noctua_i18n.h)

// Status / common
#define NOCTUA_I18N_DASH F("—")

#define NOCTUA_I18N_WIFI_STATUS_NEED_CFG F("Потрібне налаштування")
#define NOCTUA_I18N_WIFI_STATUS_CONNECTED F("Підключено")
#define NOCTUA_I18N_WIFI_STATUS_CONNECTING F("Підключення")
#define NOCTUA_I18N_WIFI_STATUS_NO_SSID F("SSID не знайдено")
#define NOCTUA_I18N_WIFI_STATUS_CONNECT_FAILED F("Не вдалося підключитись")
#define NOCTUA_I18N_WIFI_STATUS_WRONG_PASSWORD F("Невірний пароль")
#define NOCTUA_I18N_WIFI_STATUS_DISCONNECTED F("Відключено")
#define NOCTUA_I18N_WIFI_STATUS_UNKNOWN F("Невідомо")

#define NOCTUA_I18N_INTERNET_UNKNOWN F("Невідомо")
#define NOCTUA_I18N_INTERNET_REACHABLE F("Доступний")
#define NOCTUA_I18N_INTERNET_NO_ROUTE F("Немає доступу")

#define NOCTUA_I18N_STATUS_WIFI_CFG_PILL_HTML \
  F("<b>WiFi:</b> <code><span class='stWarn'>Потрібне налаштування</span></code>")

#define NOCTUA_I18N_STATUS_INTERNET_PREFIX_HTML \
  F("<b>Інтернет:</b> <code><span id='val_internet'>")

// Home page
#define NOCTUA_I18N_HOME_SUBTITLE_PREFIX F("Монітор сервісу Svitlobot · Час роботи: ")
#define NOCTUA_I18N_HOME_STATUS_PREFIX_HTML F("<b>Статус:</b> ")

#define NOCTUA_I18N_API_WAITING F("Очікування")
#define NOCTUA_I18N_API_OK F("Ок")
#define NOCTUA_I18N_API_FAIL F("Помилка")

#define NOCTUA_I18N_BTN_CONFIGURE F("Налаштування")

// JS i18n tokens
#define NOCTUA_I18N_JS_NOW F("зараз")
#define NOCTUA_I18N_JS_SEC F("с")

// Footer
#define NOCTUA_I18N_FOOTER_FW_LABEL F("ПЗ:")

// Rebooting page
#define NOCTUA_I18N_TITLE_REBOOTING F("Перезавантаження")
#define NOCTUA_I18N_H1_REBOOTING F("Перезавантаження...")
#define NOCTUA_I18N_REBOOT_MSG F("Застосування змін та перезавантаження.")
#define NOCTUA_I18N_REBOOT_DETAIL F("Очікування, поки пристрій знову буде онлайн.")
#define NOCTUA_I18N_BTN_OPEN_HOME F("Відкрити головну")
#define NOCTUA_I18N_REBOOT_TOO_LONG F("Пристрій занадто довго перезавантажується.")
#define NOCTUA_I18N_REBOOT_TOO_LONG_DETAIL \
  F("Якщо Wi-Fi недоступний, пристрій може підняти AP 'Noctua' за адресою http://192.168.4.1/")

// Login
#define NOCTUA_I18N_TITLE_LOGIN F("Вхід")
#define NOCTUA_I18N_LOGIN_HINT F("Введіть пароль адміністратора")
#define NOCTUA_I18N_LABEL_PASSWORD F("Пароль")
#define NOCTUA_I18N_PLACEHOLDER_ADMIN_PASSWORD F("Пароль адміністратора")
#define NOCTUA_I18N_BTN_LOGIN_SUBMIT F("Увійти")
#define NOCTUA_I18N_BTN_BACK F("Назад")
#define NOCTUA_I18N_LOGIN_WRONG_PASSWORD F("Невірний пароль")

// Admin / Configure
#define NOCTUA_I18N_TITLE_CONFIGURE F("Налаштування")
#define NOCTUA_I18N_CONFIG_HINT F("Налаштування Wi-Fi та застосунку")

#define NOCTUA_I18N_LABEL_WIFI_SSID F("Wi-Fi SSID")
#define NOCTUA_I18N_PLACEHOLDER_SSID F("SSID")

#define NOCTUA_I18N_LABEL_WIFI_PASSWORD F("Пароль Wi-Fi")

#define NOCTUA_I18N_LABEL_ADMIN_PASSWORD F("Пароль адміністратора")
#define NOCTUA_I18N_PLACEHOLDER_OPTIONAL F("(опційно)")

#define NOCTUA_I18N_LABEL_CONFIRM_ADMIN_PASSWORD F("Підтвердіть пароль адміністратора")
#define NOCTUA_I18N_PLACEHOLDER_REPEAT_PASSWORD F("(повторіть пароль)")

#define NOCTUA_I18N_LABEL_CHANNEL_KEY F("Ключ каналу")
#define NOCTUA_I18N_LABEL_LED F("Світлодіод")
#define NOCTUA_I18N_LED_ENABLED F("Світлодіод увімкнено")

#define NOCTUA_I18N_BTN_SAVE F("Зберегти")
#define NOCTUA_I18N_BTN_CANCEL F("Скасувати")
#define NOCTUA_I18N_BTN_FIRMWARE_UPDATE F("Оновлення прошивки")
#define NOCTUA_I18N_BTN_CLEAR_FLASH F("Очистити флеш")

#define NOCTUA_I18N_ALERT_ADMIN_PASSWORDS_MISMATCH F("Паролі адміністратора не співпадають")

// OTA Update
#define NOCTUA_I18N_TITLE_FIRMWARE_UPDATE F("Оновлення прошивки")
#define NOCTUA_I18N_OTA_NEED_ADMIN_PASS_FIRST \
  F("Спочатку задайте пароль адміністратора, щоб увімкнути OTA-оновлення.")
#define NOCTUA_I18N_OTA_UPLOAD_HELP \
  F("Завантажте файл прошивки (.bin). Використайте файл, зібраний PlatformIO: <code>.pio/build/esp01_1m/firmware.bin</code>. Після оновлення пристрій перезавантажиться.")
#define NOCTUA_I18N_BTN_UPDATE F("Оновити")
#define NOCTUA_I18N_OTA_ADMIN_PASSWORD_REQUIRED \
  F("Для OTA-оновлень потрібен пароль адміністратора")
#define NOCTUA_I18N_OTA_UPDATE_FAILED F("Оновлення не вдалося.")
#define NOCTUA_I18N_OTA_ERROR_LABEL F("Помилка:")
#define NOCTUA_I18N_OTA_RECEIVED_LABEL F("Отримано:")
#define NOCTUA_I18N_OTA_BYTES F("байт")

// Clear flash
#define NOCTUA_I18N_TITLE_CLEAR_FLASH F("Очистити флеш")
#define NOCTUA_I18N_CLEAR_FLASH_CONFIRM \
  F("Це видалить усі збережені налаштування (Wi-Fi, ключ каналу, пароль адміністратора) та перезавантажить пристрій.")
#define NOCTUA_I18N_BTN_YES_CLEAR F("Так, очистити")

// Save errors
#define NOCTUA_I18N_ADMIN_CONFIRM_MISMATCH \
  F("Підтвердження пароля адміністратора не співпадає.")
