// =========================================================================
// lang.h — строки интерфейса (RU / EN)
// =========================================================================
// Одна таблица X-макросов: ключ и оба перевода стоят в одной строке,
// поэтому перечисление и массив строк не могут разойтись.
#pragma once

#define LANG_TABLE(X) \
  X(T_TITLE,               "ESP32 Console Server", "ESP32 Console Server") \
  X(T_SUBTITLE,            "WiFi → COM мост", "WiFi → COM bridge") \
  X(T_LANG_SWITCH,         "English", "Русский") \
  X(T_CARD_CONSOLE,        "Консоль", "Console") \
  X(T_STATE,               "Состояние", "State") \
  X(T_STATE_FREE,          "Свободна", "Free") \
  X(T_STATE_BUSY,          "Занята", "Busy") \
  X(T_STATE_AUTH,          "Ввод пароля", "Entering password") \
  X(T_CLIENT,              "Клиент", "Client") \
  X(T_SESSION_TIME,        "Длительность сессии", "Session time") \
  X(T_TCP_PORT,            "TCP-порт", "TCP port") \
  X(T_UART_SPEED,          "Скорость UART", "UART speed") \
  X(T_BAUD_UNIT,           "бод", "baud") \
  X(T_RFC2217_TEMP,        "задана клиентом", "set by client") \
  X(T_AUTOBAUD,            "Автоопределение скорости", "Speed auto-detect") \
  X(T_CARD_SECURITY,       "Безопасность", "Security") \
  X(T_SESSION_PASSWORD,    "Пароль на сессию", "Session password") \
  X(T_PORTAL_PASSWORD,     "Пароль на веб-портал", "Web portal password") \
  X(T_ENABLED,             "включён", "enabled") \
  X(T_DISABLED,            "выключен", "disabled") \
  X(T_WHITELIST,           "White List", "White List") \
  X(T_NETWORKS_SUFFIX,     " сеть(ей)", " network(s)") \
  X(T_STATICIP,            "Static IP", "Static IP") \
  X(T_DHCP,                "выключен (DHCP)", "disabled (DHCP)") \
  X(T_IDLE_TIMEOUT,        "Отключение при простое", "Idle timeout") \
  X(T_MINUTES_SHORT,       "мин", "min") \
  X(T_OFF,                 "выключено", "off") \
  X(T_CARD_WIFI,           "Wi-Fi", "Wi-Fi") \
  X(T_MODE,                "Режим", "Mode") \
  X(T_MODE_AP,             "своя точка доступа (AP)", "own access point (AP)") \
  X(T_MODE_STA,            "клиент Wi-Fi (STA)", "Wi-Fi client (STA)") \
  X(T_NETWORK,             "Сеть", "Network") \
  X(T_NETWORK_TYPE,        "Тип сети", "Network type") \
  X(T_SECURITY_PSK,        "Обычная (пароль или открытая)", "Regular (password or open)") \
  X(T_SECURITY_ENTERPRISE, "WPA2-Enterprise (802.1X)", "WPA2-Enterprise (802.1X)") \
  X(T_IP_ADDRESS,          "IP-адрес", "IP address") \
  X(T_MAC_ADDRESS,         "MAC-адрес", "MAC address") \
  X(T_SIGNAL,              "Сигнал", "Signal") \
  X(T_AP_CLIENTS,          "Клиентов на AP", "AP clients") \
  X(T_AP_RETRY,            "Повтор подключения к", "Retrying network") \
  X(T_HOSTNAME,            "Имя хоста (mDNS)", "Hostname (mDNS)") \
  X(T_TX_POWER,            "Мощность передатчика", "Transmit power") \
  X(T_FALLBACK_AP,         "Резервная точка доступа", "Fallback access point") \
  X(T_AP_PASSWORD,         "Пароль резервной AP", "Fallback AP password") \
  X(T_CARD_SYSTEM,         "Система", "System") \
  X(T_BOARD,               "Плата", "Board") \
  X(T_FIRMWARE,            "Версия прошивки", "Firmware version") \
  X(T_UPTIME,              "Время работы", "Uptime") \
  X(T_FREE_MEMORY,         "Свободная память", "Free memory") \
  X(T_KB,                  "КБ", "KB") \
  X(T_BYTES_RX,            "Байт TCP→UART", "Bytes TCP→UART") \
  X(T_BYTES_TX,            "Байт UART→TCP", "Bytes UART→TCP") \
  X(T_VPN_STATUS,          "WireGuard", "WireGuard") \
  X(T_VPN_UP,              "туннель запущен", "tunnel up") \
  X(T_VPN_DOWN,            "не запущен", "not started") \
  X(T_UNIT_D,              "д", "d") \
  X(T_UNIT_H,              "ч", "h") \
  X(T_UNIT_M,              "м", "m") \
  X(T_UNIT_S,              "с", "s") \
  X(T_BTN_KICK,            "Отключить клиента", "Disconnect client") \
  X(T_BTN_DETECT_BAUD,     "Определить скорость", "Detect speed") \
  X(T_BTN_RESTART,         "Перезагрузить", "Restart") \
  X(T_BTN_SETTINGS,        "Настройки", "Settings") \
  X(T_BTN_WIFI,            "Wi-Fi", "Wi-Fi") \
  X(T_BTN_VPN,             "WireGuard VPN", "WireGuard VPN") \
  X(T_BTN_UPDATE,          "Обновление прошивки", "Firmware update") \
  X(T_BTN_RESET_WIFI,      "Сбросить Wi-Fi настройки", "Reset Wi-Fi settings") \
  X(T_CONFIRM_RESTART,     "Перезагрузить устройство?", "Restart the device?") \
  X(T_CONFIRM_RESET_WIFI,  "Сбросить Wi-Fi настройки и перезагрузиться?", "Reset Wi-Fi settings and restart?") \
  X(T_CONFIRM_DETECT,      "В консоль будут отправлены Ctrl+U и Enter на разных скоростях. Продолжить?", "Ctrl+U and Enter will be sent to the console at several speeds. Continue?") \
  X(T_BAUD_DETECTED,       "Скорость определена и сохранена", "Speed detected and saved") \
  X(T_BAUD_NOT_DETECTED,   "Не удалось определить скорость: устройство не ответило читаемым текстом ни на одной скорости", "Could not detect the speed: no readable response at any speed") \
  X(T_BAUD_BUSY,           "Консоль занята — сначала отключите клиента", "Console is busy — disconnect the client first") \
  X(T_SAVED,               "Сохранено.", "Saved.") \
  X(T_BACK,                "← назад к статусу", "← back to status") \
  X(T_SAVE,                "Сохранить", "Save") \
  X(T_SAVE_REBOOT,         "Сохранить и перезагрузить", "Save and restart") \
  X(T_REBOOTING,           "Перезагрузка… страница обновится сама через несколько секунд.", "Restarting… the page will reload in a few seconds.") \
  X(T_RESTARTING,          "Устройство перезагружается.", "The device is restarting.") \
  X(T_RESET_DONE,          "Wi-Fi настройки сброшены. Устройство поднимет свою точку доступа.", "Wi-Fi settings cleared. The device will start its own access point.") \
  X(T_CSRF_ERROR,          "Форма устарела (устройство перезагружалось). Обновите страницу и повторите.", "The form has expired (the device restarted). Reload the page and try again.") \
  X(T_SETTINGS_TITLE,      "Настройки", "Settings") \
  X(T_SESSION_PASSWORD_HINT, "Запрашивается у клиента при подключении к TCP-консоли.", "Requested from the client when connecting to the TCP console.") \
  X(T_PASSWORD_SAVED_PH,   "•••••• сохранён — оставьте пустым, чтобы не менять", "•••••• saved — leave blank to keep") \
  X(T_PASSWORD_NOT_SET_PH, "не задан", "not set") \
  X(T_DISABLE_PASSWORD,    "Без пароля", "No password") \
  X(T_PORTAL_PASSWORD_HINT, "Защищает этот веб-интерфейс, когда устройство в рабочей сети (на портале резервной AP не действует). Логин — admin. Удержание кнопки BOOT 5 секунд сбрасывает его вместе с Wi-Fi настройками и паролем резервной AP.", "Protects this web interface while the device is on a working network (not applied on the fallback AP portal). Login is admin. Holding BOOT for 5 seconds resets it together with the Wi-Fi settings and the fallback AP password.") \
  X(T_PORTAL_USE_SESSION,  "Использовать пароль сессии консоли", "Use the console session password") \
  X(T_PORTAL_SEPARATE,     "Отдельный пароль портала", "Separate portal password") \
  X(T_IDLE_HINT,           "Отключать клиента, если он ничего не вводит указанное число минут. 0 — не отключать.", "Disconnect the client after this many minutes without input. 0 = never.") \
  X(T_HOSTNAME_HINT,       "Латиница, цифры и дефис. Портал будет доступен по адресу имя.local.", "Letters, digits and hyphen. The portal will be reachable at name.local.") \
  X(T_AP_PASSWORD_HINT,    "От 8 до 63 символов. Нужен для входа в резервную точку доступа, если рабочая сеть недоступна. Кнопка BOOT (5 с) возвращает заводской пароль из прошивки.", "8 to 63 characters. Used to join the fallback access point when the working network is unavailable. The BOOT button (5 s) restores the factory password from the firmware.") \
  X(T_AP_PASSWORD_SHORT,   "Пароль резервной AP не изменён: нужно от 8 до 63 символов.", "Fallback AP password not changed: it must be 8 to 63 characters.") \
  X(T_WHITELIST_TITLE,     "White List (разрешённые сети)", "White List (allowed networks)") \
  X(T_WHITELIST_HINT,      "Формат IP/маска-CIDR, например 192.168.1.0/24. Пустая строка — слот не используется. Все пустые — White List выключен.", "Format IP/CIDR, e.g. 192.168.1.0/24. Empty = slot unused. All empty = White List disabled.") \
  X(T_STATICIP_ENABLE,     "Включить статический IP", "Enable static IP") \
  X(T_IP_LABEL,            "IP-адрес", "IP address") \
  X(T_MASK_LABEL,          "Маска подсети", "Subnet mask") \
  X(T_GATEWAY_LABEL,       "Шлюз", "Gateway") \
  X(T_STATICIP_HINT,       "Применяется только к рабочей Wi-Fi сети и вступает в силу после перезагрузки.", "Applies only to the working Wi-Fi network, takes effect after a restart.") \
  X(T_UART_SPEED_TITLE,    "Скорость UART (консоль)", "UART speed (console)") \
  X(T_UART_SPEED_HINT,     "Применяется сразу, без перезагрузки.", "Applied immediately, no restart needed.") \
  X(T_AUTOBAUD_LABEL,      "Определять скорость автоматически при подключении клиента", "Detect the speed automatically when a client connects") \
  X(T_AUTOBAUD_HINT,       "Перед началом сессии в консоль отправляются Ctrl+U и Enter на разных скоростях (~0.5–4 с). Найденная скорость сохраняется.", "Before the session starts, Ctrl+U and Enter are sent at several speeds (~0.5–4 s). The detected speed is saved.") \
  X(T_WIFI_TITLE,          "Wi-Fi настройки", "Wi-Fi settings") \
  X(T_CURRENT_NETWORK,     "Текущее подключение", "Current connection") \
  X(T_SCAN_BUTTON,         "Сканировать сети", "Scan networks") \
  X(T_SCAN_HINT,           "Нажмите «Сканировать сети», чтобы увидеть доступные сети.", "Press Scan networks to see available networks.") \
  X(T_SELECT_NETWORK,      "Выберите сеть", "Select a network") \
  X(T_MANUAL_SSID,         "Или введите вручную (SSID)", "Or enter manually (SSID)") \
  X(T_MANUAL_SSID_HINT,    "Для скрытой сети или сети, не найденной при сканировании.", "For a hidden network or one the scan did not find.") \
  X(T_WIFI_PASSWORD,       "Пароль сети", "Network password") \
  X(T_WIFI_PASSWORD_HINT,  "Пустое поле — оставить сохранённый пароль. Для открытой сети отметьте галочку ниже.", "Leave blank to keep the saved password. For an open network tick the box below.") \
  X(T_WIFI_OPEN,           "Открытая сеть (без пароля)", "Open network (no password)") \
  X(T_EAP_IDENTITY,        "Identity", "Identity") \
  X(T_EAP_IDENTITY_HINT,   "Обычно совпадает с именем пользователя (user@domain или DOMAIN\\user). Пустое поле — не менять.", "Usually the same as the username (user@domain or DOMAIN\\user). Leave blank to keep.") \
  X(T_EAP_USERNAME,        "Имя пользователя", "Username") \
  X(T_EAP_PASSWORD,        "Пароль", "Password") \
  X(T_EAP_PASSWORD_HINT,   "Поддерживается PEAP/MSCHAPv2 без проверки CA-сертификата.", "PEAP/MSCHAPv2 without CA certificate validation is supported.") \
  X(T_TX_POWER_HINT,       "Чем ниже мощность, тем меньше пиковый ток. На платах со слабым питанием (C3 Super Mini) больше 8.5 dBm может мешать работе Wi-Fi.", "Lower power means lower peak current. On boards with weak power (C3 Super Mini) more than 8.5 dBm may break Wi-Fi.") \
  X(T_RESET_WIFI_HINT,     "Удалит сохранённую сеть и перезагрузит устройство в режим собственной точки доступа.", "Deletes the saved network and restarts the device into its own access point.") \
  X(T_VPN_TITLE,           "WireGuard VPN", "WireGuard VPN") \
  X(T_VPN_ENABLE,          "Включить WireGuard-туннель", "Enable WireGuard tunnel") \
  X(T_VPN_PRIVATE_KEY,     "Приватный ключ устройства", "Device private key") \
  X(T_VPN_PRIVATE_KEY_HINT, "Base64, команда wg genkey. Пустое поле — оставить сохранённый.", "Base64, from wg genkey. Leave blank to keep the saved one.") \
  X(T_VPN_PEER_KEY,        "Публичный ключ сервера", "Server public key") \
  X(T_VPN_ENDPOINT,        "Адрес сервера (endpoint)", "Server address (endpoint)") \
  X(T_VPN_PORT,            "Порт сервера", "Server port") \
  X(T_VPN_LOCAL_IP,        "IP устройства в туннеле", "Device tunnel IP") \
  X(T_VPN_LOCAL_IP_HINT,   "Адрес из подсети WireGuard, выданный этому пиру на сервере, например 10.0.0.2.", "Address from the WireGuard subnet assigned to this peer on the server, e.g. 10.0.0.2.") \
  X(T_VPN_KEEPALIVE,       "Persistent keepalive (сек)", "Persistent keepalive (sec)") \
  X(T_VPN_KEEPALIVE_HINT,  "Держит NAT открытым, чтобы устройство было доступно из туннеля. 0 — выключено, обычно 25.", "Keeps NAT open so the device stays reachable through the tunnel. 0 = off, 25 is typical.") \
  X(T_VPN_KEEPALIVE_LIBHINT, "Установлена библиотека без настройки keepalive — используется её значение по умолчанию (15 с). Пропатченная версия описана в README.", "The installed library cannot set keepalive — its default (15 s) is used. See README for the patched version.") \
  X(T_UPDATE_TITLE,        "Обновление прошивки", "Firmware update") \
  X(T_UPDATE_HINT,         "Файл .bin из Arduino IDE: Скетч → Экспорт бинарного файла. Прошивка должна быть собрана для этой же платы.", "A .bin file from Arduino IDE: Sketch → Export Compiled Binary. It must be built for this same board.") \
  X(T_UPDATE_BUTTON,       "Загрузить и прошить", "Upload and flash") \
  X(T_UPDATE_PROGRESS,     "Загрузка, не закрывайте страницу…", "Uploading, keep this page open…") \
  X(T_UPDATE_OK,           "Прошивка обновлена.", "Firmware updated.") \
  X(T_UPDATE_FAIL,         "Ошибка обновления", "Update failed")

enum TKey {
#define LANG_ENUM(k, ru, en) k,
  LANG_TABLE(LANG_ENUM)
#undef LANG_ENUM
  T_KEY_COUNT
};

static const char* const LANG_STR[T_KEY_COUNT][2] = {
#define LANG_ROW(k, ru, en) {ru, en},
  LANG_TABLE(LANG_ROW)
#undef LANG_ROW
};

extern bool g_langEn;
// Параметр int, а не TKey: автогенерация прототипов в Arduino IDE ставит их до объявления enum
inline String T(int k) { return String(LANG_STR[k][g_langEn ? 1 : 0]); }
