#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>

// =========================================================================
// 1. ГЛОБАЛЬНЫЕ НАСТРОЙКИ РЕЖИМОВ (компилируемые)
// =========================================================================
// Пароль сессии, White List, Static IP, Wi-Fi сеть, скорость UART и язык
// интерфейса настраиваются в рантайме через веб-портал и хранятся в NVS.

// #define USE_BAN_LIST         // Раскомментируйте для часовой блокировки IP после 3 ошибок пароля.

// =========================================================================
// 2. ЗАВОДСКИЕ ЗНАЧЕНИЯ ПО УМОЛЧАНИЮ (используются один раз, пока NVS пуст)
// =========================================================================
const char* DEFAULT_SSID     = "e1";
const char* DEFAULT_PASSWORD = "ji3Xephe";
const int port    = 8888; // TCP порт для виртуального COM-порта
const int webPort = 80;   // Порт веб-портала

// --- РЕЗЕРВНАЯ ТОЧКА ДОСТУПА (если целевая сеть недоступна/не настроена) ---
const char* apSsid     = "ESP32-Console";
const char* apPassword = "console1234"; // Мин. 8 символов для WPA2, либо "" для открытой сети
const unsigned long wifiConnectTimeoutMs = 15000;
// В режиме AP устройство всегда доступно по адресу 192.168.4.1

#define DEFAULT_UART_BAUD 9600
const long baudOptions[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
const int baudOptionsCount = sizeof(baudOptions) / sizeof(baudOptions[0]);

#ifdef USE_BAN_LIST
  struct IPBanRecord {
    IPAddress ip;
    int wrongAttempts = 0;
    unsigned long lockoutStartTime = 0;
    bool isLocked = false;
  };
  const int MAX_TRACKED_IPS = 10;
  IPBanRecord banList[MAX_TRACKED_IPS];
  const int maxAttempts = 3;
  const unsigned long lockoutDuration = 3600000; // 1 час в мс
#endif

// =========================================================================
// 3. ЖЕЛЕЗО: ESP32-C3 SUPER MINI
// =========================================================================
#define RX1_PIN 20 // GPIO20 -> TXD платы MAX3232
#define TX1_PIN 21 // GPIO21 -> RXD платы MAX3232

WiFiServer server(port);
WiFiClient client;
WebServer webServer(webPort);
Preferences prefs;
const char* NVS_NS = "consvr";

// --- Статистика ---
unsigned long bootMillis = 0;
unsigned long rxBytesTotal = 0;
unsigned long txBytesTotal = 0;
bool kickRequested = false;
bool apMode = false;

// --- Буферизованный мост TCP<->UART ---
#define BRIDGE_BUF_SIZE 512
uint8_t bridgeBuf[BRIDGE_BUF_SIZE];

// --- Минимальный фильтр Telnet IAC (чтобы согласование эха не улетало в UART) ---
enum TelnetState { TN_DATA, TN_IAC, TN_CMD, TN_SB };
TelnetState telnetState = TN_DATA;
const uint8_t TELNET_NEGOTIATION[] = { 0xFF, 0xFB, 0x01,  // IAC WILL ECHO — просим клиент отключить локальное эхо
                                        0xFF, 0xFB, 0x03 }; // IAC WILL SUPPRESS_GO_AHEAD

// Схлопывает telnet-перевод строки (CR LF или CR NUL) в одиночный CR перед отправкой в UART —
// иначе Cisco консоль воспринимает второй байт как ещё один Enter (двойной вывод приглашения)
bool pendingCRCheck = false;
int normalizeLineEndings(uint8_t *buf, int len) {
  int outLen = 0;
  for (int i = 0; i < len; i++) {
    uint8_t b = buf[i];
    if (pendingCRCheck) {
      pendingCRCheck = false;
      if (b == 0x0A || b == 0x00) continue; // "довесок" после CR — выбрасываем, это не отдельный Enter
    }
    buf[outLen++] = b;
    if (b == 0x0D) pendingCRCheck = true;
  }
  return outLen;
}
int filterTelnetIAC(uint8_t *buf, int len) {
  int outLen = 0;
  for (int i = 0; i < len; i++) {
    uint8_t b = buf[i];
    switch (telnetState) {
      case TN_DATA:
        if (b == 0xFF) telnetState = TN_IAC;
        else buf[outLen++] = b;
        break;
      case TN_IAC:
        if (b == 0xFF) { buf[outLen++] = 0xFF; telnetState = TN_DATA; } // экранированный 0xFF — это данные
        else if (b == 0xFA) telnetState = TN_SB;
        else if (b == 0xFB || b == 0xFC || b == 0xFD || b == 0xFE) telnetState = TN_CMD; // WILL/WONT/DO/DONT
        else telnetState = TN_DATA; // однобайтовые команды (NOP, GA и т.п.)
        break;
      case TN_CMD:
        telnetState = TN_DATA; // это был байт опции для WILL/WONT/DO/DONT
        break;
      case TN_SB:
        if (b == 0xF0) telnetState = TN_DATA; // SE — конец субпереговоров (упрощённо)
        break;
    }
  }
  return outLen;
}

// =========================================================================
// 4. НАСТРОЙКИ, ХРАНИМЫЕ В NVS
// =========================================================================
String g_ssid = "";
String g_staPassword = "";
long   g_uartBaud = DEFAULT_UART_BAUD;
String g_language = "ru"; // "ru" | "en"

String g_authPassword = "";
String g_whiteListRaw[5];
bool   g_staticIpEnabled = false;
String g_staticIpStr   = "";
String g_staticMaskStr = "255.255.255.0";
String g_staticGwStr   = "";

struct WhiteEntry { IPAddress net; IPAddress mask; };
WhiteEntry g_whiteEntries[5];
int g_whiteCount = 0;

bool isAuthenticated = true;

// --- Кэш последнего сканирования Wi-Fi сетей ---
#define MAX_SCAN_RESULTS 20
String g_scanSsid[MAX_SCAN_RESULTS];
int    g_scanRssi[MAX_SCAN_RESULTS];
bool   g_scanSecure[MAX_SCAN_RESULTS];
int    g_scanCount = 0;

// =========================================================================
// 5. ПЕРЕВОДЫ (RU / EN)
// =========================================================================
enum TKey {
  T_TITLE, T_SUBTITLE_PREFIX, T_CARD_CONSOLE, T_STATE, T_STATE_FREE, T_STATE_BUSY, T_CLIENT, T_NONE_VAL,
  T_TCP_PORT, T_UART_SPEED, T_CARD_SESSION, T_SESSION_PASSWORD, T_ENABLED, T_DISABLED, T_WHITELIST,
  T_NETWORKS_SUFFIX, T_STATICIP, T_DHCP, T_CARD_WIFI, T_MODE, T_MODE_AP, T_MODE_STA, T_NETWORK,
  T_IP_ADDRESS, T_SIGNAL, T_AP_CLIENTS, T_CARD_SYSTEM, T_UPTIME, T_FREE_MEMORY, T_BYTES_RX, T_BYTES_TX,
  T_BTN_KICK, T_BTN_RESTART, T_BTN_SETTINGS, T_BTN_WIFI, T_BTN_RESET_WIFI, T_CONFIRM_RESTART,
  T_CONFIRM_RESET_WIFI, T_SAVED,
  T_SETTINGS_TITLE, T_SESSION_PASSWORD_HINT, T_WHITELIST_TITLE, T_WHITELIST_HINT, T_STATICIP_TITLE,
  T_STATICIP_ENABLE, T_IP_LABEL, T_MASK_LABEL, T_GATEWAY_LABEL, T_STATICIP_HINT, T_UART_SPEED_TITLE,
  T_UART_SPEED_HINT, T_SAVE_BUTTON, T_BACK_TO_STATUS,
  T_WIFI_TITLE, T_CURRENT_NETWORK, T_SCAN_BUTTON, T_SCANNING_HINT, T_SELECT_NETWORK, T_MANUAL_SSID,
  T_MANUAL_SSID_HINT, T_WIFI_PASSWORD, T_WIFI_PASSWORD_HINT, T_SAVE_REBOOT_BUTTON, T_RESET_WIFI_HINT,
  T_LANG_SWITCH, T_WIFI_OPEN_LABEL, T_KEY_COUNT
};
const char* T_RU[T_KEY_COUNT] = {
  /*TITLE*/ "ESP32 Console Server",
  /*SUBTITLE_PREFIX*/ "WiFi \u2192 COM \u043c\u043e\u0441\u0442",
  /*CARD_CONSOLE*/ "\u041a\u041e\u041d\u0421\u041e\u041b\u042c",
  /*STATE*/ "\u0421\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u0435",
  /*STATE_FREE*/ "\u0421\u0432\u043e\u0431\u043e\u0434\u043d\u0430",
  /*STATE_BUSY*/ "\u0417\u0430\u043d\u044f\u0442\u0430",
  /*CLIENT*/ "\u041a\u043b\u0438\u0435\u043d\u0442",
  /*NONE_VAL*/ "\u2014",
  /*TCP_PORT*/ "TCP-\u043f\u043e\u0440\u0442",
  /*UART_SPEED*/ "\u0421\u043a\u043e\u0440\u043e\u0441\u0442\u044c UART",
  /*CARD_SESSION*/ "\u0411\u0415\u0417\u041e\u041f\u0410\u0421\u041d\u041e\u0421\u0422\u042c",
  /*SESSION_PASSWORD*/ "\u041f\u0430\u0440\u043e\u043b\u044c \u043d\u0430 \u0441\u0435\u0441\u0441\u0438\u044e",
  /*ENABLED*/ "\u0432\u043a\u043b\u044e\u0447\u0435\u043d",
  /*DISABLED*/ "\u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d",
  /*WHITELIST*/ "White List",
  /*NETWORKS_SUFFIX*/ " \u0441\u0435\u0442\u044c(\u0435\u0439)",
  /*STATICIP*/ "Static IP",
  /*DHCP*/ "\u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d (DHCP)",
  /*CARD_WIFI*/ "WI-FI",
  /*MODE*/ "\u0420\u0435\u0436\u0438\u043c",
  /*MODE_AP*/ "\u0441\u0432\u043e\u044f \u0442\u043e\u0447\u043a\u0430 \u0434\u043e\u0441\u0442\u0443\u043f\u0430 (AP)",
  /*MODE_STA*/ "\u043a\u043b\u0438\u0435\u043d\u0442 Wi-Fi (STA)",
  /*NETWORK*/ "\u0421\u0435\u0442\u044c",
  /*IP_ADDRESS*/ "IP-\u0430\u0434\u0440\u0435\u0441",
  /*SIGNAL*/ "\u0421\u0438\u0433\u043d\u0430\u043b",
  /*AP_CLIENTS*/ "\u041a\u043b\u0438\u0435\u043d\u0442\u043e\u0432 \u043d\u0430 AP",
  /*CARD_SYSTEM*/ "\u0421\u0418\u0421\u0422\u0415\u041c\u0410",
  /*UPTIME*/ "\u0412\u0440\u0435\u043c\u044f \u0440\u0430\u0431\u043e\u0442\u044b",
  /*FREE_MEMORY*/ "\u0421\u0432\u043e\u0431\u043e\u0434\u043d\u0430\u044f \u043f\u0430\u043c\u044f\u0442\u044c",
  /*BYTES_RX*/ "\u0411\u0430\u0439\u0442 TCP\u2192UART",
  /*BYTES_TX*/ "\u0411\u0430\u0439\u0442 UART\u2192TCP",
  /*BTN_KICK*/ "\u041e\u0442\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u043a\u043b\u0438\u0435\u043d\u0442\u0430",
  /*BTN_RESTART*/ "\u041f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c",
  /*BTN_SETTINGS*/ "\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438",
  /*BTN_WIFI*/ "Wi-Fi",
  /*BTN_RESET_WIFI*/ "\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c Wi-Fi \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438",
  /*CONFIRM_RESTART*/ "\u041f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c \u0443\u0441\u0442\u0440\u043e\u0439\u0441\u0442\u0432\u043e?",
  /*CONFIRM_RESET_WIFI*/ "\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c Wi-Fi \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c\u0441\u044f?",
  /*SAVED*/ "\u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u043e.",
  /*SETTINGS_TITLE*/ "\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438",
  /*SESSION_PASSWORD_HINT*/ "\u0417\u0430\u043f\u0440\u0430\u0448\u0438\u0432\u0430\u0435\u0442\u0441\u044f \u0443 \u043a\u043b\u0438\u0435\u043d\u0442\u0430 \u043f\u0440\u0438 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0438 \u043a TCP-\u043a\u043e\u043d\u0441\u043e\u043b\u0438. \u041f\u0443\u0441\u0442\u043e\u0435 \u043f\u043e\u043b\u0435 = \u0431\u0435\u0437 \u043f\u0430\u0440\u043e\u043b\u044f.",
  /*WHITELIST_TITLE*/ "White List (\u0440\u0430\u0437\u0440\u0435\u0448\u0451\u043d\u043d\u044b\u0435 \u0441\u0435\u0442\u0438)",
  /*WHITELIST_HINT*/ "\u0424\u043e\u0440\u043c\u0430\u0442: IP/\u043c\u0430\u0441\u043a\u0430-CIDR, \u043d\u0430\u043f\u0440\u0438\u043c\u0435\u0440 192.168.1.0/24. \u041f\u0443\u0441\u0442\u0430\u044f \u0441\u0442\u0440\u043e\u043a\u0430 = \u0441\u043b\u043e\u0442 \u043d\u0435 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u0435\u0442\u0441\u044f. \u0412\u0441\u0435 \u043f\u0443\u0441\u0442\u044b\u0435 = White List \u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d.",
  /*STATICIP_TITLE*/ "Static IP",
  /*STATICIP_ENABLE*/ "\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0441\u0442\u0430\u0442\u0438\u0447\u0435\u0441\u043a\u0438\u0439 IP",
  /*IP_LABEL*/ "IP-\u0430\u0434\u0440\u0435\u0441",
  /*MASK_LABEL*/ "\u041c\u0430\u0441\u043a\u0430 \u043f\u043e\u0434\u0441\u0435\u0442\u0438",
  /*GATEWAY_LABEL*/ "\u0428\u043b\u044e\u0437",
  /*STATICIP_HINT*/ "\u041f\u0440\u0438\u043c\u0435\u043d\u044f\u0435\u0442\u0441\u044f \u0442\u043e\u043b\u044c\u043a\u043e \u043a \u0446\u0435\u043b\u0435\u0432\u043e\u0439 Wi-Fi \u0441\u0435\u0442\u0438 \u0438 \u0432\u0441\u0442\u0443\u043f\u0430\u0435\u0442 \u0432 \u0441\u0438\u043b\u0443 \u043f\u043e\u0441\u043b\u0435 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0438.",
  /*UART_SPEED_TITLE*/ "\u0421\u043a\u043e\u0440\u043e\u0441\u0442\u044c UART (\u043a\u043e\u043d\u0441\u043e\u043b\u044c)",
  /*UART_SPEED_HINT*/ "\u041f\u0440\u0438\u043c\u0435\u043d\u044f\u0435\u0442\u0441\u044f \u0441\u0440\u0430\u0437\u0443 \u043f\u043e\u0441\u043b\u0435 \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u044f, \u0431\u0435\u0437 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0438.",
  /*SAVE_BUTTON*/ "\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c",
  /*BACK_TO_STATUS*/ "\u2190 \u043d\u0430\u0437\u0430\u0434 \u043a \u0441\u0442\u0430\u0442\u0443\u0441\u0443",
  /*WIFI_TITLE*/ "Wi-Fi \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438",
  /*CURRENT_NETWORK*/ "\u0422\u0435\u043a\u0443\u0449\u0435\u0435 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435",
  /*SCAN_BUTTON*/ "\u0421\u043a\u0430\u043d\u0438\u0440\u043e\u0432\u0430\u0442\u044c \u0441\u0435\u0442\u0438",
  /*SCANNING_HINT*/ "\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u00ab\u0421\u043a\u0430\u043d\u0438\u0440\u043e\u0432\u0430\u0442\u044c \u0441\u0435\u0442\u0438\u00bb, \u0447\u0442\u043e\u0431\u044b \u0443\u0432\u0438\u0434\u0435\u0442\u044c \u0434\u043e\u0441\u0442\u0443\u043f\u043d\u044b\u0435 \u0441\u0435\u0442\u0438.",
  /*SELECT_NETWORK*/ "\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0441\u0435\u0442\u044c",
  /*MANUAL_SSID*/ "\u0418\u043b\u0438 \u0432\u0432\u0435\u0434\u0438\u0442\u0435 \u0432\u0440\u0443\u0447\u043d\u0443\u044e (SSID)",
  /*MANUAL_SSID_HINT*/ "\u0417\u0430\u043f\u043e\u043b\u043d\u0438\u0442\u0435, \u0435\u0441\u043b\u0438 \u0441\u0435\u0442\u044c \u0441\u043a\u0440\u044b\u0442\u0430 \u0438\u043b\u0438 \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d\u0430 \u043f\u0440\u0438 \u0441\u043a\u0430\u043d\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0438.",
  /*WIFI_PASSWORD*/ "\u041f\u0430\u0440\u043e\u043b\u044c \u0441\u0435\u0442\u0438",
  /*WIFI_PASSWORD_HINT*/ "\u041e\u0441\u0442\u0430\u0432\u044c\u0442\u0435 \u043f\u0443\u0441\u0442\u044b\u043c, \u0447\u0442\u043e\u0431\u044b \u0441\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0442\u0435\u043a\u0443\u0449\u0438\u0439 \u0441\u043e\u0445\u0440\u0430\u043d\u0451\u043d\u043d\u044b\u0439 \u043f\u0430\u0440\u043e\u043b\u044c. \u0414\u043b\u044f \u043e\u0442\u043a\u0440\u044b\u0442\u043e\u0439 \u0441\u0435\u0442\u0438 \u0431\u0435\u0437 \u043f\u0430\u0440\u043e\u043b\u044f \u043e\u0442\u043c\u0435\u0442\u044c\u0442\u0435 \u0433\u0430\u043b\u043e\u0447\u043a\u0443 \u043d\u0438\u0436\u0435.",
  /*SAVE_REBOOT_BUTTON*/ "\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c",
  /*RESET_WIFI_HINT*/ "\u0423\u0434\u0430\u043b\u0438\u0442 \u0441\u043e\u0445\u0440\u0430\u043d\u0451\u043d\u043d\u0443\u044e \u0441\u0435\u0442\u044c \u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442 \u0443\u0441\u0442\u0440\u043e\u0439\u0441\u0442\u0432\u043e \u0432 \u0440\u0435\u0436\u0438\u043c \u0441\u043e\u0431\u0441\u0442\u0432\u0435\u043d\u043d\u043e\u0439 \u0442\u043e\u0447\u043a\u0438 \u0434\u043e\u0441\u0442\u0443\u043f\u0430 192.168.4.1.",
  /*LANG_SWITCH*/ "English",
  /*WIFI_OPEN_LABEL*/ "\u042d\u0442\u043e \u043e\u0442\u043a\u0440\u044b\u0442\u0430\u044f \u0441\u0435\u0442\u044c (\u0431\u0435\u0437 \u043f\u0430\u0440\u043e\u043b\u044f)"
};
const char* T_EN[T_KEY_COUNT] = {
  "ESP32 Console Server", "WiFi \u2192 COM bridge", "CONSOLE", "State", "Free", "Busy", "Client", "\u2014",
  "TCP port", "UART speed", "SECURITY", "Session password", "enabled", "disabled", "White List",
  " network(s)", "Static IP", "disabled (DHCP)", "WI-FI", "Mode", "own access point (AP)",
  "Wi-Fi client (STA)", "Network", "IP address", "Signal", "AP clients", "SYSTEM", "Uptime",
  "Free memory", "Bytes TCP\u2192UART", "Bytes UART\u2192TCP", "Disconnect client", "Restart", "Settings",
  "Wi-Fi", "Reset Wi-Fi settings", "Restart the device?", "Reset Wi-Fi settings and restart?", "Saved.",
  "Settings", "Requested from the client when connecting to the TCP console. Empty = no password.",
  "White List (allowed networks)",
  "Format: IP/CIDR mask, e.g. 192.168.1.0/24. Empty string = slot unused. All empty = White List disabled.",
  "Static IP", "Enable static IP", "IP address", "Subnet mask", "Gateway",
  "Applies only to the target Wi-Fi network, takes effect after reboot.",
  "UART speed (console)", "Applied immediately after saving, no reboot needed.", "Save",
  "\u2190 back to status",
  "Wi-Fi settings", "Current connection", "Scan networks", "Press \u00abScan networks\u00bb to see available networks.",
  "Select a network", "Or enter manually (SSID)", "Fill in if the network is hidden or wasn't found by the scan.",
  "Network password", "Leave blank to keep the currently saved password. For an open network with no password, check the box below.", "Save and restart",
  "Deletes the saved network and restarts the device into its own access point at 192.168.4.1.",
  "\u0420\u0443\u0441\u0441\u043a\u0438\u0439", "This is an open network (no password)"
};
String T(int k) { return String(g_language == "en" ? T_EN[k] : T_RU[k]); }

#ifdef USE_BAN_LIST
int getIPRecordIndex(IPAddress ip) {
  int firstEmpty = -1;
  for (int i = 0; i < MAX_TRACKED_IPS; i++) {
    if (banList[i].ip == ip) return i;
    if (banList[i].wrongAttempts == 0 && !banList[i].isLocked && firstEmpty == -1) firstEmpty = i;
  }
  if (firstEmpty != -1) {
    banList[firstEmpty].ip = ip;
    banList[firstEmpty].wrongAttempts = 0;
    banList[firstEmpty].isLocked = false;
    return firstEmpty;
  }
  return -1;
}
#endif

bool parseCIDR(String entry, IPAddress &net, IPAddress &mask) {
  entry.trim();
  if (entry.length() == 0) return false;
  int slashIdx = entry.indexOf('/');
  String ipPart = (slashIdx == -1) ? entry : entry.substring(0, slashIdx);
  int prefixLen = (slashIdx == -1) ? 32 : entry.substring(slashIdx + 1).toInt();
  if (prefixLen < 0 || prefixLen > 32) return false;
  IPAddress ip;
  if (!ip.fromString(ipPart)) return false;
  uint32_t maskBits = (prefixLen == 0) ? 0 : (0xFFFFFFFFu << (32 - prefixLen));
  mask = IPAddress((maskBits >> 24) & 0xFF, (maskBits >> 16) & 0xFF, (maskBits >> 8) & 0xFF, maskBits & 0xFF);
  net = IPAddress(ip[0] & mask[0], ip[1] & mask[1], ip[2] & mask[2], ip[3] & mask[3]);
  return true;
}

void rebuildWhiteList() {
  g_whiteCount = 0;
  for (int i = 0; i < 5; i++) {
    IPAddress net, mask;
    if (parseCIDR(g_whiteListRaw[i], net, mask)) {
      g_whiteEntries[g_whiteCount].net = net;
      g_whiteEntries[g_whiteCount].mask = mask;
      g_whiteCount++;
    }
  }
}

bool isIpInAllowedNetworks(IPAddress clientIp) {
  for (int i = 0; i < g_whiteCount; i++) {
    if ((clientIp & g_whiteEntries[i].mask) == g_whiteEntries[i].net) return true;
  }
  return false;
}

void loadSettings() {
  prefs.begin(NVS_NS, true);
  g_ssid        = prefs.getString("ssid", DEFAULT_SSID);
  g_staPassword = prefs.getString("stapass", DEFAULT_PASSWORD);
  g_uartBaud    = prefs.getLong("baud", DEFAULT_UART_BAUD);
  g_language    = prefs.getString("lang", "ru");
  g_authPassword = prefs.getString("authpass", "");
  for (int i = 0; i < 5; i++) g_whiteListRaw[i] = prefs.getString(("wl" + String(i)).c_str(), "");
  g_staticIpEnabled = prefs.getBool("static_en", false);
  g_staticIpStr   = prefs.getString("static_ip", "");
  g_staticMaskStr = prefs.getString("static_mask", "255.255.255.0");
  g_staticGwStr   = prefs.getString("static_gw", "");
  prefs.end();
  rebuildWhiteList();
}

void saveSecuritySettings() {
  prefs.begin(NVS_NS, false);
  prefs.putString("authpass", g_authPassword);
  for (int i = 0; i < 5; i++) prefs.putString(("wl" + String(i)).c_str(), g_whiteListRaw[i]);
  prefs.putBool("static_en", g_staticIpEnabled);
  prefs.putString("static_ip", g_staticIpStr);
  prefs.putString("static_mask", g_staticMaskStr);
  prefs.putString("static_gw", g_staticGwStr);
  prefs.putLong("baud", g_uartBaud);
  prefs.end();
}

void saveWifiSettings() {
  prefs.begin(NVS_NS, false);
  prefs.putString("ssid", g_ssid);
  prefs.putString("stapass", g_staPassword);
  prefs.putLong("baud", g_uartBaud);
  prefs.end();
}

void applyUartBaud(long baud) {
  g_uartBaud = baud;
  Serial1.updateBaudRate(g_uartBaud); // применяется мгновенно, без перезапуска UART
}

// =========================================================================
// 6. ВЕБ-ПОРТАЛ: тёмная тема, статус, настройки, Wi-Fi
// =========================================================================
String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("\"", "&quot;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  return s;
}

String pageHeader(String title) {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>" + title + "</title><style>";
  html += "*{box-sizing:border-box;} body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#0b0f19;color:#e6e9ef;max-width:900px;margin:0 auto;padding:20px 16px 40px;}";
  html += "a{color:#7aa2ff;} .topbar{display:flex;justify-content:space-between;align-items:flex-start;flex-wrap:wrap;gap:10px;}";
  html += ".topbar h1{font-size:20px;margin:0;display:flex;align-items:center;gap:8px;} .subtitle{color:#7b8494;font-size:13px;margin-top:4px;}";
  html += ".lang-btn{background:#e5484d;color:#fff !important;padding:6px 14px;border-radius:20px;text-decoration:none;font-size:13px;font-weight:600;white-space:nowrap;}";
  html += ".lang-btn:hover{background:#c93d42;}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:18px;}";
  html += "@media(max-width:560px){.grid{grid-template-columns:1fr;}}";
  html += ".card{background:#141b2d;border:1px solid #232b3d;border-radius:12px;padding:16px 18px;}";
  html += ".card h3{margin:0 0 10px 0;font-size:12px;letter-spacing:1px;text-transform:uppercase;color:#7b8494;}";
  html += ".row{display:flex;justify-content:space-between;gap:12px;padding:7px 0;border-bottom:1px solid #1f2636;font-size:14px;}";
  html += ".row:last-child{border-bottom:none;} .row .label{color:#8b93a7;} .row .value{font-weight:600;color:#e6e9ef;text-align:right;}";
  html += ".badge{padding:3px 10px;border-radius:20px;font-size:12px;font-weight:600;}";
  html += ".badge-green{background:#113b2c;color:#4ade80;} .badge-gray{background:#242c3f;color:#9aa4b8;}";
  html += ".actions{margin-top:18px;display:flex;flex-wrap:wrap;gap:8px;}";
  html += "button,a.btn{background:#1f2937;color:#e6e9ef;border:1px solid #2d3748;padding:9px 16px;border-radius:8px;cursor:pointer;text-decoration:none;font-size:14px;display:inline-block;}";
  html += "button:hover,a.btn:hover{background:#26304a;} .btn-danger{background:#3a1620;border-color:#5c2430;color:#ff8080 !important;}";
  html += ".btn-danger:hover{background:#4a1c28;}";
  html += "h2{font-size:16px;color:#c3c9d6;margin:22px 0 4px;} .section{background:#141b2d;border:1px solid #232b3d;border-radius:12px;padding:18px;margin-top:14px;}";
  html += "label{color:#c3c9d6;display:block;margin-bottom:6px;font-size:14px;}";
  html += "input[type=text],input[type=password],select{width:100%;padding:9px 10px;margin:0 0 12px 0;border:1px solid #2d3748;border-radius:6px;background:#0e131f;color:#e6e9ef;font-size:14px;}";
  html += ".hint{color:#7b8494;font-size:12.5px;margin-top:-8px;margin-bottom:12px;}";
  html += ".radiorow{display:flex;justify-content:space-between;align-items:center;padding:7px 4px;border-bottom:1px solid #1f2636;font-size:14px;}";
  html += ".radiorow input{margin-right:8px;}";
  html += "</style></head><body>";
  return html;
}

String badge(String text, bool ok) {
  return "<span class='badge " + String(ok ? "badge-green" : "badge-gray") + "'>" + text + "</span>";
}

String row(String label, String value) {
  return "<div class='row'><span class='label'>" + label + "</span><span class='value'>" + value + "</span></div>";
}

String topBar(String currentPath) {
  String otherLang = (g_language == "en") ? "ru" : "en";
  String html = "<div class='topbar'><div><h1>\U0001F5A5\uFE0F " + T(T_TITLE) + "</h1>";
  html += "<div class='subtitle'>" + T(T_SUBTITLE_PREFIX) + " \u00b7 " + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "</div></div>";
  html += "<a class='lang-btn' href='/lang?set=" + otherLang + "&from=" + currentPath + "'>" + T(T_LANG_SWITCH) + "</a></div>";
  return html;
}

void handleRoot() {
  bool connected = client && client.connected();
  String html = pageHeader(T(T_TITLE));
  html += topBar("/");

  html += "<div class='grid'>";

  html += "<div class='card'><h3>" + T(T_CARD_CONSOLE) + "</h3>";
  html += row(T(T_STATE), badge(connected ? T(T_STATE_BUSY) : T(T_STATE_FREE), !connected));
  html += row(T(T_CLIENT), connected ? client.remoteIP().toString() : T(T_NONE_VAL));
  html += row(T(T_TCP_PORT), String(port));
  html += row(T(T_UART_SPEED), String(g_uartBaud) + " \u0431\u043e\u0434");
  html += "</div>";

  html += "<div class='card'><h3>" + T(T_CARD_SESSION) + "</h3>";
  html += row(T(T_SESSION_PASSWORD), badge(g_authPassword.length() > 0 ? T(T_ENABLED) : T(T_DISABLED), g_authPassword.length() > 0));
  html += row(T(T_WHITELIST), g_whiteCount > 0 ? badge(String(g_whiteCount) + T(T_NETWORKS_SUFFIX), true) : badge(T(T_DISABLED), false));
  html += row(T(T_STATICIP), g_staticIpEnabled ? g_staticIpStr : T(T_DHCP));
  html += "</div>";

  html += "<div class='card'><h3>" + T(T_CARD_WIFI) + "</h3>";
  html += row(T(T_MODE), apMode ? T(T_MODE_AP) : T(T_MODE_STA));
  html += row(T(T_NETWORK), apMode ? String(apSsid) : g_ssid);
  html += row(T(T_IP_ADDRESS), apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  if (apMode) html += row(T(T_AP_CLIENTS), String(WiFi.softAPgetStationNum()));
  else html += row(T(T_SIGNAL), String(WiFi.RSSI()) + " dBm");
  html += "</div>";

  html += "<div class='card'><h3>" + T(T_CARD_SYSTEM) + "</h3>";
  {
    unsigned long ms = millis() - bootMillis;
    unsigned long s = ms / 1000; unsigned long d = s / 86400; s %= 86400;
    unsigned long h = s / 3600; s %= 3600; unsigned long m = s / 60; s %= 60;
    String up = (d ? String(d) + "\u0434 " : "") + String(h) + "\u0447 " + String(m) + "\u043c " + String(s) + "\u0441";
    html += row(T(T_UPTIME), up);
  }
  html += row(T(T_FREE_MEMORY), String(ESP.getFreeHeap() / 1024) + " \u041a\u0411");
  html += row(T(T_BYTES_RX), String(rxBytesTotal));
  html += row(T(T_BYTES_TX), String(txBytesTotal));
  html += "</div>";

  html += "</div>"; // grid

  html += "<div class='actions'>";
  html += "<form action='/kick' method='POST' style='display:inline'><button" + String(connected ? "" : " disabled") + ">" + T(T_BTN_KICK) + "</button></form>";
  html += "<form action='/restart' method='POST' style='display:inline'><button onclick='return confirm(\"" + T(T_CONFIRM_RESTART) + "\")'>" + T(T_BTN_RESTART) + "</button></form>";
  html += "<a class='btn' href='/settings'>" + T(T_BTN_SETTINGS) + "</a>";
  html += "<a class='btn' href='/wifi'>" + T(T_BTN_WIFI) + "</a>";
  html += "<form action='/wifi/reset' method='POST' style='display:inline'><button class='btn-danger' onclick='return confirm(\"" + T(T_CONFIRM_RESET_WIFI) + "\")'>" + T(T_BTN_RESET_WIFI) + "</button></form>";
  html += "</div></body></html>";

  webServer.send(200, "text/html", html);
}

String uartSpeedSelect() {
  String html = "<label>" + T(T_UART_SPEED_TITLE) + "</label><select name='baud'>";
  for (int i = 0; i < baudOptionsCount; i++) {
    html += "<option value='" + String(baudOptions[i]) + "'" + (baudOptions[i] == g_uartBaud ? " selected" : "") + ">" + String(baudOptions[i]) + "</option>";
  }
  html += "</select><p class='hint'>" + T(T_UART_SPEED_HINT) + "</p>";
  return html;
}

void handleSettingsGet() {
  String html = pageHeader(T(T_SETTINGS_TITLE));
  html += topBar("/settings");
  html += "<h2>" + T(T_SETTINGS_TITLE) + "</h2>";
  if (webServer.hasArg("saved")) html += "<p style='color:#4ade80'>" + T(T_SAVED) + "</p>";

  html += "<form method='POST' action='/settings'>";

  html += "<div class='section'><label>" + T(T_SESSION_PASSWORD) + "</label>";
  html += "<p class='hint'>" + T(T_SESSION_PASSWORD_HINT) + "</p>";
  html += "<input type='text' name='authpass' value='" + htmlEscape(g_authPassword) + "'>";
  html += "</div>";

  html += "<div class='section'><label>" + T(T_WHITELIST_TITLE) + "</label>";
  html += "<p class='hint'>" + T(T_WHITELIST_HINT) + "</p>";
  for (int i = 0; i < 5; i++) {
    html += "<input type='text' name='wl" + String(i) + "' value='" + htmlEscape(g_whiteListRaw[i]) + "' placeholder='10.0.0.0/16'>";
  }
  html += "</div>";

  html += "<div class='section'><label><input type='checkbox' name='static_en' style='width:auto;display:inline;margin-right:8px' " + String(g_staticIpEnabled ? "checked" : "") + "> " + T(T_STATICIP_ENABLE) + "</label>";
  html += "<label>" + T(T_IP_LABEL) + "</label><input type='text' name='static_ip' value='" + htmlEscape(g_staticIpStr) + "' placeholder='192.168.1.200'>";
  html += "<label>" + T(T_MASK_LABEL) + "</label><input type='text' name='static_mask' value='" + htmlEscape(g_staticMaskStr) + "' placeholder='255.255.255.0'>";
  html += "<label>" + T(T_GATEWAY_LABEL) + "</label><input type='text' name='static_gw' value='" + htmlEscape(g_staticGwStr) + "' placeholder='192.168.1.1'>";
  html += "<p class='hint' style='color:#e0a33c'>" + T(T_STATICIP_HINT) + "</p></div>";

  html += "<div class='section'>" + uartSpeedSelect() + "</div>";

  html += "<button type='submit'>" + T(T_SAVE_BUTTON) + "</button></form>";
  html += "<p style='margin-top:14px'><a href='/'>" + T(T_BACK_TO_STATUS) + "</a></p></body></html>";

  webServer.send(200, "text/html", html);
}

void handleSettingsPost() {
  g_authPassword = webServer.arg("authpass");
  for (int i = 0; i < 5; i++) g_whiteListRaw[i] = webServer.arg("wl" + String(i));
  g_staticIpEnabled = webServer.hasArg("static_en");
  g_staticIpStr   = webServer.arg("static_ip");
  g_staticMaskStr = webServer.arg("static_mask");
  g_staticGwStr   = webServer.arg("static_gw");
  rebuildWhiteList();

  long newBaud = webServer.arg("baud").toInt();
  if (newBaud > 0) applyUartBaud(newBaud);

  saveSecuritySettings();
  webServer.sendHeader("Location", "/settings?saved=1");
  webServer.send(303);
}

void doWifiScan() {
  wifi_mode_t prevMode = WiFi.getMode();
  if (prevMode == WIFI_AP) WiFi.mode(WIFI_AP_STA); // временно включаем STA для сканирования поверх AP
  int n = WiFi.scanNetworks();
  g_scanCount = 0;
  for (int i = 0; i < n && g_scanCount < MAX_SCAN_RESULTS; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < g_scanCount; j++) if (g_scanSsid[j] == s) { dup = true; break; }
    if (dup) continue;
    g_scanSsid[g_scanCount] = s;
    g_scanRssi[g_scanCount] = WiFi.RSSI(i);
    g_scanSecure[g_scanCount] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    g_scanCount++;
  }
  WiFi.scanDelete();
}

void handleWifiGet() {
  if (webServer.hasArg("scan")) doWifiScan();

  String html = pageHeader(T(T_WIFI_TITLE));
  html += topBar("/wifi");
  html += "<h2>" + T(T_WIFI_TITLE) + "</h2>";
  if (webServer.hasArg("saved")) html += "<p style='color:#4ade80'>" + T(T_SAVED) + "</p>";

  html += "<div class='section'><h3 style='text-transform:none;font-size:13px;color:#7b8494'>" + T(T_CURRENT_NETWORK) + "</h3>";
  html += row(T(T_MODE), apMode ? T(T_MODE_AP) : T(T_MODE_STA));
  html += row(T(T_NETWORK), apMode ? String(apSsid) : g_ssid);
  html += row(T(T_IP_ADDRESS), apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  html += "</div>";

  html += "<form method='GET' action='/wifi'><button type='submit' name='scan' value='1'>" + T(T_SCAN_BUTTON) + "</button></form>";

  html += "<form method='POST' action='/wifi'>";
  html += "<div class='section'><label>" + T(T_SELECT_NETWORK) + "</label>";
  if (g_scanCount == 0) {
    html += "<p class='hint'>" + T(T_SCANNING_HINT) + "</p>";
  } else {
    for (int i = 0; i < g_scanCount; i++) {
      html += "<div class='radiorow'><label style='display:flex;align-items:center;margin:0'><input type='radio' name='ssid' value='" + htmlEscape(g_scanSsid[i]) + "'" + (g_scanSsid[i] == g_ssid ? " checked" : "") + "> " + htmlEscape(g_scanSsid[i]) + (g_scanSecure[i] ? " \U0001F512" : "") + "</label><span style='color:#7b8494'>" + String(g_scanRssi[i]) + " dBm</span></div>";
    }
  }
  html += "<label style='margin-top:12px'>" + T(T_MANUAL_SSID) + "</label><input type='text' name='ssid_manual' placeholder='SSID'>";
  html += "<p class='hint'>" + T(T_MANUAL_SSID_HINT) + "</p>";
  html += "<label>" + T(T_WIFI_PASSWORD) + "</label><input type='text' name='wifi_pass' value=''>";
  html += "<p class='hint'>" + T(T_WIFI_PASSWORD_HINT) + "</p>";
  html += "<label style='display:flex;align-items:center;gap:8px;margin-bottom:12px'><input type='checkbox' name='wifi_open' style='width:auto;margin:0'> " + T(T_WIFI_OPEN_LABEL) + "</label>";
  html += "</div>";

  html += "<div class='section'>" + uartSpeedSelect() + "</div>";

  html += "<button type='submit'>" + T(T_SAVE_REBOOT_BUTTON) + "</button></form>";

  html += "<div class='section'><p class='hint'>" + T(T_RESET_WIFI_HINT) + "</p>";
  html += "<form action='/wifi/reset' method='POST'><button class='btn-danger' onclick='return confirm(\"" + T(T_CONFIRM_RESET_WIFI) + "\")'>" + T(T_BTN_RESET_WIFI) + "</button></form></div>";

  html += "<p style='margin-top:14px'><a href='/'>" + T(T_BACK_TO_STATUS) + "</a></p></body></html>";

  webServer.send(200, "text/html", html);
}

void handleWifiPost() {
  String manual = webServer.arg("ssid_manual");
  String picked = webServer.arg("ssid");
  String chosen = manual.length() > 0 ? manual : picked;
  if (chosen.length() > 0) g_ssid = chosen;

  bool openNetwork = webServer.hasArg("wifi_open");
  String pass = webServer.arg("wifi_pass");
  if (openNetwork) {
    g_staPassword = ""; // явно отмечено как открытая сеть
  } else if (pass.length() > 0) {
    g_staPassword = pass; // введён новый пароль
  }
  // иначе поле пустое и галочка не стоит — оставляем ранее сохранённый пароль как есть

  long newBaud = webServer.arg("baud").toInt();
  if (newBaud > 0) g_uartBaud = newBaud; // применится при перезапуске вместе с новой сетью

  saveWifiSettings();
  webServer.send(200, "text/plain", "Saved. Rebooting...");
  delay(300);
  ESP.restart();
}

void handleWifiReset() {
  g_ssid = "";
  g_staPassword = "";
  prefs.begin(NVS_NS, false);
  prefs.putString("ssid", "");
  prefs.putString("stapass", "");
  prefs.end();
  webServer.send(200, "text/plain", "Reset. Rebooting...");
  delay(300);
  ESP.restart();
}

void handleLang() {
  String set = webServer.arg("set");
  if (set == "en" || set == "ru") {
    g_language = set;
    prefs.begin(NVS_NS, false);
    prefs.putString("lang", g_language);
    prefs.end();
  }
  String from = webServer.arg("from");
  if (from.length() == 0) from = "/";
  webServer.sendHeader("Location", from);
  webServer.send(303);
}

void handleKick() {
  if (client && client.connected()) kickRequested = true;
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

void handleRestart() {
  webServer.send(200, "text/plain", "Restarting...");
  delay(300);
  ESP.restart();
}

void setupWebPortal() {
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/settings", HTTP_GET, handleSettingsGet);
  webServer.on("/settings", HTTP_POST, handleSettingsPost);
  webServer.on("/wifi", HTTP_GET, handleWifiGet);
  webServer.on("/wifi", HTTP_POST, handleWifiPost);
  webServer.on("/wifi/reset", HTTP_POST, handleWifiReset);
  webServer.on("/lang", HTTP_GET, handleLang);
  webServer.on("/kick", HTTP_POST, handleKick);
  webServer.on("/restart", HTTP_POST, handleRestart);
  webServer.begin();
  Serial.println("[SYSTEM] Web-portal started on port " + String(webPort));
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[SYSTEM] Reset reason: " + String(esp_reset_reason()));
  loadSettings();
  Serial1.setRxBufferSize(4096); // заводской буфер 256 байт слишком мал для быстрого "show run" и т.п.
  Serial1.setTxBufferSize(1024);
  Serial1.begin(g_uartBaud, SERIAL_8N1, RX1_PIN, TX1_PIN);

  WiFi.setSleep(false);

  if (g_staticIpEnabled) {
    IPAddress ip, gw, mask;
    bool ok = ip.fromString(g_staticIpStr) && gw.fromString(g_staticGwStr) && mask.fromString(g_staticMaskStr);
    if (ok) {
      WiFi.config(ip, gw, mask, gw);
      Serial.println("[SYSTEM] Static IP configured: " + g_staticIpStr);
    } else {
      Serial.println("[ERROR] Static IP settings invalid, falling back to DHCP.");
    }
  }

  if (g_ssid.length() == 0) {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    bool apOk = WiFi.softAP(apSsid, apPassword);
    Serial.println("[SYSTEM] No Wi-Fi network configured. Fallback AP started.");
    Serial.println("[SYSTEM] softAP() returned: " + String(apOk ? "OK" : "FAILED"));
    Serial.println("[SYSTEM] AP SSID: " + String(apSsid) + "   AP IP: " + WiFi.softAPIP().toString());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(g_ssid.c_str(), g_staPassword.c_str());
    Serial.print("[SYSTEM] Connecting to \"" + g_ssid + "\"");
    unsigned long connectStarted = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - connectStarted) < wifiConnectTimeoutMs) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED) {
      // Радио иногда не полностью переинициализируется после программного ESP.restart() —
      // пробуем полностью выключить и включить Wi-Fi заново перед тем, как сдаться и уйти в AP.
      Serial.println("\n[WARNING] First attempt failed, power-cycling the radio and retrying...");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(300);
      WiFi.mode(WIFI_STA);
      delay(100);
      WiFi.begin(g_ssid.c_str(), g_staPassword.c_str());
      Serial.print("[SYSTEM] Retrying connection to \"" + g_ssid + "\"");
      connectStarted = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - connectStarted) < wifiConnectTimeoutMs) {
        delay(500);
        Serial.print(".");
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      apMode = false;
      Serial.println("\n[SYSTEM] Connected. Current ESP32-C3 IP: " + WiFi.localIP().toString());
    } else {
      apMode = true;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_AP);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
      bool apOk = WiFi.softAP(apSsid, apPassword);
      Serial.println("\n[WARNING] Configured WiFi unreachable. Fallback AP started.");
      Serial.println("[SYSTEM] softAP() returned: " + String(apOk ? "OK" : "FAILED"));
      Serial.println("[SYSTEM] AP SSID: " + String(apSsid) + "   AP IP: " + WiFi.softAPIP().toString());
    }
  }

  server.begin();
  bootMillis = millis();
  Serial.println("[SYSTEM] TCP Server started.");

  const char* headerKeys[] = {"Referer"};
  webServer.collectHeaders(headerKeys, 1);
  setupWebPortal();
}

void loop() {
  unsigned long currentMillis = millis();

  webServer.handleClient();

  if (kickRequested) {
    kickRequested = false;
    if (client && client.connected()) {
      client.println("KICKED_BY_ADMIN");
      delay(50);
      client.stop();
      Serial.println("[SYSTEM] Client kicked via web-portal.");
    }
  }

  #ifdef USE_BAN_LIST
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
      if (banList[i].isLocked && (currentMillis - banList[i].lockoutStartTime >= lockoutDuration)) {
        banList[i].isLocked = false;
        banList[i].wrongAttempts = 0;
        Serial.print("[SECURITY] Lockout expired for IP: "); Serial.println(banList[i].ip.toString());
      }
    }
  #endif

  if (server.hasClient()) {
    WiFiClient newClient = server.available();
    IPAddress remoteIP = newClient.remoteIP();

    if (g_whiteCount > 0 && !isIpInAllowedNetworks(remoteIP)) {
      Serial.print("[WARNING] Connection REJECTED (Network not in White List): "); Serial.println(remoteIP.toString());
      newClient.stop();
      return;
    }

    #ifdef USE_BAN_LIST
      int ipIdx = getIPRecordIndex(remoteIP);
      if (ipIdx != -1 && banList[ipIdx].isLocked) {
        Serial.print("[SECURITY] Connection REJECTED (Banned IP): "); Serial.println(remoteIP.toString());
        newClient.println("YOUR_IP_IS_LOCKED_OUT");
        delay(100);
        newClient.stop();
        return;
      }
    #endif

    if (client && client.connected()) {
      newClient.println("BUSY");
      delay(100);
      newClient.stop();
    } else {
      client = newClient;
      client.setNoDelay(true);
      telnetState = TN_DATA; // сброс фильтра IAC для новой сессии
      client.write(TELNET_NEGOTIATION, sizeof(TELNET_NEGOTIATION)); // просим клиент отключить локальное эхо
      bool authRequired = (g_authPassword.length() > 0);
      if (authRequired) {
        isAuthenticated = false;
        client.println("AUTH_REQUIRED");
        Serial.print("[SYSTEM] Client connected, awaiting password: "); Serial.println(remoteIP.toString());
      } else {
        isAuthenticated = true;
        client.println("CONNECTED_NO_AUTH");
        Serial.print("[SYSTEM] Client connected (Auth disabled): "); Serial.println(remoteIP.toString());
      }
    }
  }

  if (g_authPassword.length() > 0 && client && client.connected() && !isAuthenticated) {
    IPAddress currentIP = client.remoteIP();
    #ifdef USE_BAN_LIST
      int ipIdx = getIPRecordIndex(currentIP);
    #endif
    if (client.available() > 0) {
      String input = client.readStringUntil('\n');
      input.trim();
      if (input == g_authPassword) {
        isAuthenticated = true;
        #ifdef USE_BAN_LIST
          if (ipIdx != -1) banList[ipIdx].wrongAttempts = 0;
        #endif
        client.println("AUTH_OK");
        Serial.print("[SYSTEM] Access GRANTED for IP: "); Serial.println(currentIP.toString());
      } else {
        #ifdef USE_BAN_LIST
          if (ipIdx != -1) {
            banList[ipIdx].wrongAttempts++;
            Serial.print("[SECURITY] Wrong password from "); Serial.print(currentIP.toString());
            Serial.print(" (Attempt "); Serial.print(banList[ipIdx].wrongAttempts); Serial.println("/3)");
            if (banList[ipIdx].wrongAttempts >= maxAttempts) {
              banList[ipIdx].isLocked = true;
              banList[ipIdx].lockoutStartTime = currentMillis;
              client.println("TOO_MANY_ATTEMPTS_IP_LOCKED");
              Serial.print("[SECURITY] IP "); Serial.print(currentIP.toString()); Serial.println(" BANNED for 1 hour.");
            } else {
              client.println("AUTH_FAILED");
            }
          }
        #else
          client.println("AUTH_FAILED");
          Serial.print("[SECURITY] Wrong password from "); Serial.println(currentIP.toString());
        #endif
        delay(100);
        client.stop();
      }
    }
  }

  if (client && client.connected() && isAuthenticated) {
    int avail;
    while ((avail = client.available()) > 0) {
      int toRead = avail > BRIDGE_BUF_SIZE ? BRIDGE_BUF_SIZE : avail;
      int len = client.read(bridgeBuf, toRead);
      if (len > 0) {
        int filtered = filterTelnetIAC(bridgeBuf, len); // вырезаем telnet-согласование клиента, не пускаем в UART
        filtered = normalizeLineEndings(bridgeBuf, filtered); // схлопываем CR LF / CR NUL в один CR
        if (filtered > 0) {
          Serial1.write(bridgeBuf, filtered);
          rxBytesTotal += filtered;
        }
      }
    }
    while ((avail = Serial1.available()) > 0) {
      int toRead = avail > BRIDGE_BUF_SIZE ? BRIDGE_BUF_SIZE : avail;
      int len = Serial1.read(bridgeBuf, toRead);
      if (len > 0) {
        client.write(bridgeBuf, len);
        txBytesTotal += len;
      }
    }
  }
}
