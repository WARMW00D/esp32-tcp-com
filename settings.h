// =========================================================================
// settings.h — глобальное состояние, настройки в NVS, проверки ввода
// =========================================================================
#pragma once

// ---------- Объекты и состояние ----------
WiFiServer  server(CONSOLE_TCP_PORT);
WiFiClient  client;
WebServer   webServer(WEB_PORT);
Preferences prefs;
static const char* NVS_NS = "consvr";

unsigned long bootMillis   = 0;
unsigned long rxBytesTotal = 0;   // TCP -> UART
unsigned long txBytesTotal = 0;   // UART -> TCP
bool apMode = false;              // true = работаем через собственную точку доступа
String g_apSsid;                  // имя резервной AP (префикс + хвост MAC)
String g_apPassword;              // пароль резервной AP (по умолчанию AP_DEFAULT_PASSWORD)
String g_csrfToken;               // случайный токен форм, новый при каждой загрузке

// ---------- Настройки, хранимые в NVS ----------
String g_ssid;
String g_staPassword;
bool   g_eapEnabled = false;
String g_eapIdentity;
String g_eapUsername;
String g_eapPassword;

String g_authPassword;                 // пароль консоли; пусто = без пароля
String g_portalPassword;               // отдельный пароль портала; пусто = без пароля
bool   g_portalUseSessionPass = false; // портал использует пароль консоли

String g_whiteListRaw[WHITELIST_SLOTS];
bool   g_staticIpEnabled = false;
String g_staticIpStr;
String g_staticMaskStr = "255.255.255.0";
String g_staticGwStr;

String g_hostname;                     // имя для mDNS / DHCP
long   g_uartBaud      = DEFAULT_UART_BAUD; // текущая скорость (может временно менять RFC 2217)
long   g_uartBaudSaved = DEFAULT_UART_BAUD; // сохранённая скорость
int    g_idleTimeoutMin = 0;           // 0 = без ограничения простоя сессии
bool   g_autoBaud = false;             // автоопределение скорости при подключении клиента
int    g_txPowerQ = DEFAULT_TX_POWER_Q;// мощность Wi-Fi, единицы 0.25 dBm (как wifi_power_t)
bool   g_langEn = false;

bool   g_vpnEnabled = false;
String g_vpnPrivateKey;
String g_vpnPeerPublicKey;
String g_vpnEndpoint;
int    g_vpnPort = 51820;
String g_vpnLocalIp;
int    g_vpnKeepalive = 25;
bool   g_vpnStarted = false;

struct WhiteEntry { IPAddress net; IPAddress mask; };
WhiteEntry g_whiteEntries[WHITELIST_SLOTS];
int g_whiteCount = 0;

// ---------- Допустимые значения ----------
const long BAUD_OPTIONS[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
const int  BAUD_OPTIONS_COUNT = sizeof(BAUD_OPTIONS) / sizeof(BAUD_OPTIONS[0]);

// Мощность передатчика: значения wifi_power_t (0.25 dBm) и подписи
const int  TXP_VALUES[] = {34, 44, 52, 60, 68, 78};
const char* const TXP_LABELS[] = {"8.5 dBm", "11 dBm", "13 dBm", "15 dBm", "17 dBm", "19.5 dBm"};
const int  TXP_COUNT = sizeof(TXP_VALUES) / sizeof(TXP_VALUES[0]);

bool isValidBaud(long b) {
  for (int i = 0; i < BAUD_OPTIONS_COUNT; i++) if (BAUD_OPTIONS[i] == b) return true;
  return false;
}

bool isValidTxPower(int q) {
  for (int i = 0; i < TXP_COUNT; i++) if (TXP_VALUES[i] == q) return true;
  return false;
}

// Имя хоста: латиница, цифры, дефис; 1..32 символа, без дефиса по краям
String sanitizeHostname(String in) {
  in.trim();
  in.toLowerCase();
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 32; i++) {
    char c = in[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') out += c;
  }
  while (out.startsWith("-")) out.remove(0, 1);
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}

// MAC читается напрямую из eFuse — работает и до инициализации Wi-Fi
String macSuffix(int bytes) {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  String s;
  char hx[3];
  for (int i = 6 - bytes; i < 6; i++) { snprintf(hx, sizeof(hx), "%02X", mac[i]); s += hx; }
  return s;
}

// ---------- White List ----------
bool parseCIDR(String entry, IPAddress &net, IPAddress &mask) {
  entry.trim();
  if (entry.length() == 0) return false;
  int slashIdx = entry.indexOf('/');
  String ipPart = (slashIdx == -1) ? entry : entry.substring(0, slashIdx);
  int prefixLen = 32;
  if (slashIdx != -1) {
    String p = entry.substring(slashIdx + 1);
    if (p.length() == 0 || p.length() > 2) return false;
    for (size_t i = 0; i < p.length(); i++) if (!isDigit(p[i])) return false;
    prefixLen = p.toInt();
  }
  if (prefixLen < 0 || prefixLen > 32) return false;
  IPAddress ip;
  if (!ip.fromString(ipPart)) return false;
  uint32_t maskBits = (prefixLen == 0) ? 0 : (0xFFFFFFFFu << (32 - prefixLen));
  mask = IPAddress((maskBits >> 24) & 0xFF, (maskBits >> 16) & 0xFF, (maskBits >> 8) & 0xFF, maskBits & 0xFF);
  net  = IPAddress(ip[0] & mask[0], ip[1] & mask[1], ip[2] & mask[2], ip[3] & mask[3]);
  return true;
}

void rebuildWhiteList() {
  g_whiteCount = 0;
  for (int i = 0; i < WHITELIST_SLOTS; i++) {
    IPAddress net, mask;
    if (parseCIDR(g_whiteListRaw[i], net, mask)) {
      g_whiteEntries[g_whiteCount].net = net;
      g_whiteEntries[g_whiteCount].mask = mask;
      g_whiteCount++;
    }
  }
}

bool isIpInAllowedNetworks(const IPAddress &ip) {
  for (int i = 0; i < g_whiteCount; i++) {
    const IPAddress &m = g_whiteEntries[i].mask;
    const IPAddress &n = g_whiteEntries[i].net;
    if ((ip[0] & m[0]) == n[0] && (ip[1] & m[1]) == n[1] && (ip[2] & m[2]) == n[2] && (ip[3] & m[3]) == n[3]) return true;
  }
  return false;
}

// ---------- NVS ----------
static String wlKey(int i) { return "wl" + String(i); }

void loadSettings() {
  // read-only begin() на пустом NVS вернёт false — значения по умолчанию всё равно подставятся
  prefs.begin(NVS_NS, true);
  g_ssid          = prefs.getString("ssid", DEFAULT_SSID);
  g_staPassword   = prefs.getString("stapass", DEFAULT_PASSWORD);
  g_eapEnabled    = prefs.getBool("eap_en", false);
  g_eapIdentity   = prefs.getString("eap_id", "");
  g_eapUsername   = prefs.getString("eap_user", "");
  g_eapPassword   = prefs.getString("eap_pass", "");
  g_uartBaudSaved = prefs.getLong("baud", DEFAULT_UART_BAUD);
  g_langEn        = prefs.getString("lang", "ru") == "en";
  g_authPassword  = prefs.getString("authpass", "");
  for (int i = 0; i < WHITELIST_SLOTS; i++) g_whiteListRaw[i] = prefs.getString(wlKey(i).c_str(), "");
  g_staticIpEnabled = prefs.getBool("static_en", false);
  g_staticIpStr   = prefs.getString("static_ip", "");
  g_staticMaskStr = prefs.getString("static_mask", "255.255.255.0");
  g_staticGwStr   = prefs.getString("static_gw", "");
  g_portalPassword       = prefs.getString("portal_pass", "");
  g_portalUseSessionPass = prefs.getBool("portal_use_sess", false);
  g_hostname       = prefs.getString("hostname", "");
  g_idleTimeoutMin = prefs.getInt("idle_min", 0);
  g_autoBaud       = prefs.getBool("autobaud", false);
  g_txPowerQ       = prefs.getInt("txpower", DEFAULT_TX_POWER_Q);
  g_vpnEnabled       = prefs.getBool("vpn_en", false);
  g_vpnPrivateKey    = prefs.getString("vpn_priv", "");
  g_vpnPeerPublicKey = prefs.getString("vpn_peer_pub", "");
  g_vpnEndpoint      = prefs.getString("vpn_endpoint", "");
  g_vpnPort          = prefs.getInt("vpn_port", 51820);
  g_vpnLocalIp       = prefs.getString("vpn_local_ip", "");
  g_vpnKeepalive     = prefs.getInt("vpn_keepalive", 25);
  g_apPassword       = prefs.getString("ap_pass", AP_DEFAULT_PASSWORD);
  prefs.end();

  // Пароль AP должен быть 8–63 символа, иначе softAP() не поднимется
  if (g_apPassword.length() < 8 || g_apPassword.length() > 63) g_apPassword = AP_DEFAULT_PASSWORD;

  // Защита от мусора в NVS
  if (!isValidBaud(g_uartBaudSaved)) g_uartBaudSaved = DEFAULT_UART_BAUD;
  g_uartBaud = g_uartBaudSaved;
  if (!isValidTxPower(g_txPowerQ)) g_txPowerQ = DEFAULT_TX_POWER_Q;
  if (g_idleTimeoutMin < 0 || g_idleTimeoutMin > 1440) g_idleTimeoutMin = 0;
  g_hostname = sanitizeHostname(g_hostname);
  if (g_hostname.length() == 0) g_hostname = String(DEFAULT_HOSTNAME_PREFIX) + "-" + macSuffix(2);
  g_hostname.toLowerCase();
  rebuildWhiteList();
}

void saveSecuritySettings() {
  prefs.begin(NVS_NS, false);
  prefs.putString("authpass", g_authPassword);
  for (int i = 0; i < WHITELIST_SLOTS; i++) prefs.putString(wlKey(i).c_str(), g_whiteListRaw[i]);
  prefs.putBool("static_en", g_staticIpEnabled);
  prefs.putString("static_ip", g_staticIpStr);
  prefs.putString("static_mask", g_staticMaskStr);
  prefs.putString("static_gw", g_staticGwStr);
  prefs.putLong("baud", g_uartBaudSaved);
  prefs.putString("portal_pass", g_portalPassword);
  prefs.putBool("portal_use_sess", g_portalUseSessionPass);
  prefs.putString("hostname", g_hostname);
  prefs.putInt("idle_min", g_idleTimeoutMin);
  prefs.putBool("autobaud", g_autoBaud);
  prefs.putString("ap_pass", g_apPassword);
  prefs.end();
}

void saveWifiSettings() {
  prefs.begin(NVS_NS, false);
  prefs.putString("ssid", g_ssid);
  prefs.putString("stapass", g_staPassword);
  prefs.putBool("eap_en", g_eapEnabled);
  prefs.putString("eap_id", g_eapIdentity);
  prefs.putString("eap_user", g_eapUsername);
  prefs.putString("eap_pass", g_eapPassword);
  prefs.putLong("baud", g_uartBaudSaved);
  prefs.putInt("txpower", g_txPowerQ);
  prefs.end();
}

void saveVpnSettings() {
  prefs.begin(NVS_NS, false);
  prefs.putBool("vpn_en", g_vpnEnabled);
  prefs.putString("vpn_priv", g_vpnPrivateKey);
  prefs.putString("vpn_peer_pub", g_vpnPeerPublicKey);
  prefs.putString("vpn_endpoint", g_vpnEndpoint);
  prefs.putInt("vpn_port", g_vpnPort);
  prefs.putString("vpn_local_ip", g_vpnLocalIp);
  prefs.putInt("vpn_keepalive", g_vpnKeepalive);
  prefs.end();
}

void saveLanguage() {
  prefs.begin(NVS_NS, false);
  prefs.putString("lang", g_langEn ? "en" : "ru");
  prefs.end();
}

// Сброс кнопкой BOOT / с портала: Wi-Fi (+ опционально пароль портала и пароль резервной AP).
// Пароль консоли, White List, Static IP и скорость UART не трогаются.
void clearWifiSettings(bool alsoPortalPassword) {
  g_ssid = ""; g_staPassword = "";
  g_eapEnabled = false; g_eapIdentity = ""; g_eapUsername = ""; g_eapPassword = "";
  prefs.begin(NVS_NS, false);
  prefs.putString("ssid", "");
  prefs.putString("stapass", "");
  prefs.putBool("eap_en", false);
  prefs.putString("eap_id", "");
  prefs.putString("eap_user", "");
  prefs.putString("eap_pass", "");
  if (alsoPortalPassword) {
    g_portalPassword = ""; g_portalUseSessionPass = false;
    prefs.putString("portal_pass", "");
    prefs.putBool("portal_use_sess", false);
    g_apPassword = AP_DEFAULT_PASSWORD;           // забытый пароль AP не должен закрывать доступ
    prefs.remove("ap_pass");
  }
  prefs.end();
}

// Применить скорость UART. persist=true — сохранить как основную (с портала),
// false — временно (запрос RFC 2217 от клиента, откатывается после сессии).
void applyUartBaud(long baud, bool persist) {
  if (baud <= 0) return;
  g_uartBaud = baud;
  Serial1.updateBaudRate(baud);
  if (persist) g_uartBaudSaved = baud;
}
