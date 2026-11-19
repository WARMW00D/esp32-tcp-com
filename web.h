// =========================================================================
// web.h — веб-портал: страницы, обработчики форм, обновление прошивки (OTA)
// =========================================================================
#pragma once

unsigned long g_restartAt = 0;    // отложенная перезагрузка — чтобы ответ успел уйти в браузер
bool   g_otaAllowed = false;
String g_otaError;

// Стили отдаются отдельным файлом с кэшированием, а не вклеиваются в каждую страницу
static const char PORTAL_CSS[] PROGMEM = R"CSS(
*{box-sizing:border-box}body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#0b0f19;color:#e6e9ef;max-width:900px;margin:0 auto;padding:20px 16px 40px}
a{color:#7aa2ff}.topbar{display:flex;justify-content:space-between;align-items:flex-start;flex-wrap:wrap;gap:10px}
.topbar h1{font-size:20px;margin:0}.subtitle{color:#7b8494;font-size:13px;margin-top:4px}
.lang-btn{background:#e5484d;color:#fff!important;padding:6px 14px;border-radius:20px;text-decoration:none;font-size:13px;font-weight:600;white-space:nowrap}
.lang-btn:hover{background:#c93d42}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:18px}
@media(max-width:560px){.grid{grid-template-columns:1fr}}
.card,.section{background:#141b2d;border:1px solid #232b3d;border-radius:12px;padding:16px 18px}.section{margin-top:14px}
.card h3,.section h3{margin:0 0 10px;font-size:12px;letter-spacing:1px;text-transform:uppercase;color:#7b8494}
.row{display:flex;justify-content:space-between;gap:12px;padding:7px 0;border-bottom:1px solid #1f2636;font-size:14px}
.row:last-child{border-bottom:none}.label{color:#8b93a7}.value{font-weight:600;text-align:right;word-break:break-all}
.badge{padding:3px 10px;border-radius:20px;font-size:12px;font-weight:600}.ok{background:#113b2c;color:#4ade80}.off{background:#242c3f;color:#9aa4b8}.warn{background:#3b2e11;color:#e0a33c}
.actions{margin-top:18px;display:flex;flex-wrap:wrap;gap:8px}form.inline{display:inline}
button,a.btn{background:#1f2937;color:#e6e9ef;border:1px solid #2d3748;padding:9px 16px;border-radius:8px;cursor:pointer;text-decoration:none;font-size:14px;display:inline-block}
button:hover,a.btn:hover{background:#26304a}button:disabled{opacity:.45;cursor:default}
.danger{background:#3a1620!important;border-color:#5c2430!important;color:#ff8080!important}
h2{font-size:16px;color:#c3c9d6;margin:22px 0 4px}label{color:#c3c9d6;display:block;margin-bottom:6px;font-size:14px}
label.chk{display:flex;align-items:center;gap:8px}label.chk input{width:auto;margin:0}
input[type=text],input[type=password],input[type=number],input[type=file],select{width:100%;padding:9px 10px;margin:0 0 12px;border:1px solid #2d3748;border-radius:6px;background:#0e131f;color:#e6e9ef;font-size:14px}
.hint{color:#7b8494;font-size:12.5px;margin:-8px 0 12px}.msg{color:#4ade80}.err{color:#ff8080}
.radiorow{display:flex;justify-content:space-between;align-items:center;padding:7px 4px;border-bottom:1px solid #1f2636;font-size:14px}
.radiorow label{display:flex;align-items:center;gap:8px;margin:0}.radiorow input{width:auto;margin:0}.dim{color:#7b8494}
)CSS";

// ---------- Вспомогательные функции HTML ----------
String htmlEscape(const String &in) {
  String s;
  s.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&':  s += F("&amp;");  break;
      case '<':  s += F("&lt;");   break;
      case '>':  s += F("&gt;");   break;
      case '"':  s += F("&quot;"); break;
      case '\'': s += F("&#39;");  break;   // атрибуты в одинарных кавычках — без этого XSS через имя Wi-Fi сети
      default:   s += c;
    }
  }
  return s;
}

static String badge(const String &text, const char* cls) {
  return "<span class='badge " + String(cls) + "'>" + text + "</span>";
}

static String row(const String &label, const String &value) {
  return "<div class='row'><span class='label'>" + label + "</span><span class='value'>" + value + "</span></div>";
}

static String csrfField() {
  return "<input type='hidden' name='csrf' value='" + g_csrfToken + "'>";
}

static String postButton(const char* action, const String &text, const char* cls, const String &confirmText, bool enabled) {
  String h = "<form class='inline' method='POST' action='" + String(action) + "'>" + csrfField() + "<button";
  if (cls && *cls) h += " class='" + String(cls) + "'";
  if (!enabled) h += " disabled";
  if (confirmText.length()) h += " onclick='return confirm(\"" + htmlEscape(confirmText) + "\")'";
  h += ">" + text + "</button></form>";
  return h;
}

String fmtDuration(unsigned long ms) {
  unsigned long s = ms / 1000, d = s / 86400; s %= 86400;
  unsigned long h = s / 3600; s %= 3600;
  unsigned long m = s / 60;   s %= 60;
  String out;
  if (d) out += String(d) + T(T_UNIT_D) + " ";
  out += String(h) + T(T_UNIT_H) + " " + String(m) + T(T_UNIT_M) + " " + String(s) + T(T_UNIT_S);
  return out;
}

static String currentIp() {
  return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

static String txPowerLabel(int q) {
  for (int i = 0; i < TXP_COUNT; i++) if (TXP_VALUES[i] == q) return TXP_LABELS[i];
  return String(q / 4.0f, 1) + " dBm";
}

String pageBegin(const String &title, const char* path) {
  String h;
  h.reserve(7000);   // меньше перераспределений и фрагментации кучи
  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<link rel='stylesheet' href='/style.css'><title>");
  h += title;
  h += F("</title></head><body><div class='topbar'><div><h1>&#x1F5A5;&#xFE0F; ");
  h += T(T_TITLE);
  h += "</h1><div class='subtitle'>" + T(T_SUBTITLE) + " &middot; " + currentIp() + " &middot; " + htmlEscape(g_hostname) + ".local</div></div>";
  h += "<a class='lang-btn' href='/lang?set=" + String(g_langEn ? "ru" : "en") + "&amp;from=" + String(path) + "'>" + T(T_LANG_SWITCH) + "</a></div>";
  return h;
}

static void sendPage(String &html) {
  html += F("</body></html>");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", html);
}

// Страница «перезагрузка» с автоматическим возвратом на главную
static void sendRebootPage(const String &msg) {
  String h = pageBegin(T(T_REBOOTING), "/");
  h.replace("<head>", "<head><meta http-equiv='refresh' content='12;url=/'>");
  h += "<div class='section'><p class='msg'>" + msg + "</p><p class='dim'>" + T(T_REBOOTING) + "</p></div>";
  sendPage(h);
  g_restartAt = millis() + 800;
}

// ---------- Доступ ----------
// На портале резервной AP пароль не спрашивается: к самой AP пускает только её пароль,
// а иначе можно навсегда закрыть себе доступ к настройке (страховка — кнопка BOOT).
static String portalPassword() {
  return g_portalUseSessionPass ? g_authPassword : g_portalPassword;
}

static bool portalAuthorized() {
  if (apMode) return true;
  String pass = portalPassword();
  if (pass.length() == 0) return true;
  return webServer.authenticate(PORTAL_USER, pass.c_str());
}

bool checkPortalAuth() {
  if (portalAuthorized()) return true;
  webServer.requestAuthentication(BASIC_AUTH, "ESP32 Console Server");
  return false;
}

// Защита от CSRF: каждая форма содержит случайный токен текущей загрузки
static bool checkCsrf() {
  if (webServer.arg("csrf") == g_csrfToken) return true;
  webServer.send(403, "text/plain; charset=utf-8", T(T_CSRF_ERROR));
  return false;
}

static String uartSpeedSelect() {
  String h = "<label>" + T(T_UART_SPEED_TITLE) + "</label><select name='baud'>";
  for (int i = 0; i < BAUD_OPTIONS_COUNT; i++) {
    h += "<option value='" + String(BAUD_OPTIONS[i]) + "'" + (BAUD_OPTIONS[i] == g_uartBaudSaved ? " selected" : "") + ">" + String(BAUD_OPTIONS[i]) + "</option>";
  }
  h += "</select><p class='hint'>" + T(T_UART_SPEED_HINT) + "</p>";
  return h;
}

static String passwordField(const char* name, bool isSet) {
  return "<input type='password' name='" + String(name) + "' autocomplete='new-password' placeholder='" +
         (isSet ? T(T_PASSWORD_SAVED_PH) : T(T_PASSWORD_NOT_SET_PH)) + "'>";
}

// ---------- Главная ----------
void handleRoot() {
  if (!checkPortalAuth()) return;
  bool busy = g_sessionActive && client.connected();
  String h = pageBegin(T(T_TITLE), "/");
  String bd = webServer.arg("baud");
  if (bd == "busy")          h += "<p class='err'>" + T(T_BAUD_BUSY) + "</p>";
  else if (bd == "fail")     h += "<p class='err'>" + T(T_BAUD_NOT_DETECTED) + "</p>";
  else if (bd.length())      h += "<p class='msg'>" + T(T_BAUD_DETECTED) + ": " + htmlEscape(bd) + " " + T(T_BAUD_UNIT) + "</p>";
  h += "<div class='grid'>";

  h += "<div class='card'><h3>" + T(T_CARD_CONSOLE) + "</h3>";
  if (!busy)                 h += row(T(T_STATE), badge(T(T_STATE_FREE), "ok"));
  else if (!isAuthenticated) h += row(T(T_STATE), badge(T(T_STATE_AUTH), "warn"));
  else                       h += row(T(T_STATE), badge(T(T_STATE_BUSY), "off"));
  h += row(T(T_CLIENT), busy ? client.remoteIP().toString() : "&mdash;");
  if (busy) h += row(T(T_SESSION_TIME), fmtDuration(millis() - g_sessionStart));
  h += row(T(T_TCP_PORT), String(CONSOLE_TCP_PORT));
  String baud = String(g_uartBaud) + " " + T(T_BAUD_UNIT);
  if (g_uartBaud != g_uartBaudSaved) baud += " " + badge(T(T_RFC2217_TEMP), "warn");
  h += row(T(T_UART_SPEED), baud);
  h += row(T(T_AUTOBAUD), g_autoBaud ? badge(T(T_ENABLED), "ok") : badge(T(T_DISABLED), "off"));
  h += "</div>";

  bool portalProtected = portalPassword().length() > 0;
  h += "<div class='card'><h3>" + T(T_CARD_SECURITY) + "</h3>";
  h += row(T(T_SESSION_PASSWORD), g_authPassword.length() ? badge(T(T_ENABLED), "ok") : badge(T(T_DISABLED), "off"));
  h += row(T(T_PORTAL_PASSWORD), portalProtected ? badge(T(T_ENABLED), "ok") : badge(T(T_DISABLED), "off"));
  h += row(T(T_WHITELIST), g_whiteCount ? badge(String(g_whiteCount) + T(T_NETWORKS_SUFFIX), "ok") : badge(T(T_DISABLED), "off"));
  h += row(T(T_STATICIP), g_staticIpEnabled ? htmlEscape(g_staticIpStr) : T(T_DHCP));
  h += row(T(T_IDLE_TIMEOUT), g_idleTimeoutMin ? String(g_idleTimeoutMin) + " " + T(T_MINUTES_SHORT) : T(T_OFF));
  h += "</div>";

  h += "<div class='card'><h3>" + T(T_CARD_WIFI) + "</h3>";
  h += row(T(T_MODE), apMode ? T(T_MODE_AP) : T(T_MODE_STA));
  h += row(T(T_NETWORK), htmlEscape(apMode ? g_apSsid : g_ssid));
  if (!apMode) h += row(T(T_NETWORK_TYPE), g_eapEnabled ? T(T_SECURITY_ENTERPRISE) : T(T_SECURITY_PSK));
  h += row(T(T_IP_ADDRESS), currentIp());
  h += row(T(T_HOSTNAME), htmlEscape(g_hostname) + ".local");
  h += row(T(T_MAC_ADDRESS), apMode ? WiFi.softAPmacAddress() : WiFi.macAddress());
  if (apMode) {
    h += row(T(T_AP_CLIENTS), String(WiFi.softAPgetStationNum()));
    if (g_ssid.length()) h += row(T(T_AP_RETRY), htmlEscape(g_ssid) + " &middot; " + String(AP_RETRY_INTERVAL_MS / 60000) + " " + T(T_MINUTES_SHORT));
  } else {
    h += row(T(T_SIGNAL), String(WiFi.RSSI()) + " dBm");
  }
  h += row(T(T_TX_POWER), txPowerLabel(g_txPowerQ));
  if (!apMode) h += row(T(T_FALLBACK_AP), htmlEscape(g_apSsid));
  h += row(T(T_AP_PASSWORD), htmlEscape(g_apPassword));
  h += "</div>";

  h += "<div class='card'><h3>" + T(T_CARD_SYSTEM) + "</h3>";
  h += row(T(T_BOARD), BOARD_NAME);
  h += row(T(T_FIRMWARE), FW_VERSION);
  h += row(T(T_UPTIME), fmtDuration(millis() - bootMillis));
  h += row(T(T_FREE_MEMORY), String(ESP.getFreeHeap() / 1024) + " " + T(T_KB));
  h += row(T(T_BYTES_RX), String(rxBytesTotal));
  h += row(T(T_BYTES_TX), String(txBytesTotal));
#ifdef USE_WIREGUARD
  h += row(T(T_VPN_STATUS), !g_vpnEnabled ? badge(T(T_DISABLED), "off") : (g_vpnStarted ? badge(T(T_VPN_UP), "ok") : badge(T(T_VPN_DOWN), "warn")));
#endif
  h += "</div></div>";

  h += "<div class='actions'>";
  h += postButton("/kick", T(T_BTN_KICK), "", "", busy);
  h += postButton("/baud/detect", T(T_BTN_DETECT_BAUD), "", T(T_CONFIRM_DETECT), !busy);
  h += postButton("/restart", T(T_BTN_RESTART), "", T(T_CONFIRM_RESTART), true);
  h += "<a class='btn' href='/settings'>" + T(T_BTN_SETTINGS) + "</a>";
  h += "<a class='btn' href='/wifi'>" + T(T_BTN_WIFI) + "</a>";
#ifdef USE_WIREGUARD
  h += "<a class='btn' href='/vpn'>" + T(T_BTN_VPN) + "</a>";
#endif
  h += "<a class='btn' href='/update'>" + T(T_BTN_UPDATE) + "</a>";
  h += postButton("/wifi/reset", T(T_BTN_RESET_WIFI), "danger", T(T_CONFIRM_RESET_WIFI), true);
  h += "</div>";
  sendPage(h);
}

// ---------- Настройки ----------
void handleSettingsGet() {
  if (!checkPortalAuth()) return;
  String h = pageBegin(T(T_SETTINGS_TITLE), "/settings");
  h += "<h2>" + T(T_SETTINGS_TITLE) + "</h2>";
  if (webServer.hasArg("saved")) h += "<p class='msg'>" + T(T_SAVED) + "</p>";
  if (webServer.arg("err") == "ap") h += "<p class='err'>" + T(T_AP_PASSWORD_SHORT) + "</p>";
  h += "<form method='POST' action='/settings'>" + csrfField();

  h += "<div class='section'><label>" + T(T_SESSION_PASSWORD) + "</label><p class='hint'>" + T(T_SESSION_PASSWORD_HINT) + "</p>";
  h += passwordField("authpass", g_authPassword.length() > 0);
  h += "<label class='chk'><input type='checkbox' name='authpass_clear'> " + T(T_DISABLE_PASSWORD) + "</label></div>";

  h += "<div class='section'><label>" + T(T_PORTAL_PASSWORD) + "</label><p class='hint'>" + T(T_PORTAL_PASSWORD_HINT) + "</p>";
  h += "<label class='chk'><input type='checkbox' name='portal_use_session'" + String(g_portalUseSessionPass ? " checked" : "") +
       " onclick=\"document.getElementById('pp').style.display=this.checked?'none':'block'\"> " + T(T_PORTAL_USE_SESSION) + "</label>";
  h += "<div id='pp' style='display:" + String(g_portalUseSessionPass ? "none" : "block") + ";margin-top:12px'>";
  h += "<label>" + T(T_PORTAL_SEPARATE) + "</label>" + passwordField("portal_pass", g_portalPassword.length() > 0);
  h += "<label class='chk'><input type='checkbox' name='portal_clear'> " + T(T_DISABLE_PASSWORD) + "</label></div></div>";

  h += "<div class='section'><label>" + T(T_IDLE_TIMEOUT) + "</label>";
  h += "<input type='number' name='idle_min' min='0' max='1440' value='" + String(g_idleTimeoutMin) + "'>";
  h += "<p class='hint'>" + T(T_IDLE_HINT) + "</p></div>";

  h += "<div class='section'><label>" + T(T_HOSTNAME) + "</label>";
  h += "<input type='text' name='hostname' maxlength='32' value='" + htmlEscape(g_hostname) + "'>";
  h += "<p class='hint'>" + T(T_HOSTNAME_HINT) + "</p>";
  h += "<label>" + T(T_AP_PASSWORD) + " (" + htmlEscape(g_apSsid) + ")</label>";
  h += "<input type='text' name='ap_pass' maxlength='63' value='" + htmlEscape(g_apPassword) + "'>";
  h += "<p class='hint'>" + T(T_AP_PASSWORD_HINT) + "</p>";
  h += "</div>";

  h += "<div class='section'><label>" + T(T_WHITELIST_TITLE) + "</label><p class='hint'>" + T(T_WHITELIST_HINT) + "</p>";
  for (int i = 0; i < WHITELIST_SLOTS; i++)
    h += "<input type='text' name='wl" + String(i) + "' value='" + htmlEscape(g_whiteListRaw[i]) + "' placeholder='10.0.0.0/16'>";
  h += "</div>";

  h += "<div class='section'><label class='chk'><input type='checkbox' name='static_en'" + String(g_staticIpEnabled ? " checked" : "") + "> " + T(T_STATICIP_ENABLE) + "</label><br>";
  h += "<label>" + T(T_IP_LABEL) + "</label><input type='text' name='static_ip' value='" + htmlEscape(g_staticIpStr) + "' placeholder='192.168.1.200'>";
  h += "<label>" + T(T_MASK_LABEL) + "</label><input type='text' name='static_mask' value='" + htmlEscape(g_staticMaskStr) + "' placeholder='255.255.255.0'>";
  h += "<label>" + T(T_GATEWAY_LABEL) + "</label><input type='text' name='static_gw' value='" + htmlEscape(g_staticGwStr) + "' placeholder='192.168.1.1'>";
  h += "<p class='hint' style='color:#e0a33c'>" + T(T_STATICIP_HINT) + "</p></div>";

  h += "<div class='section'>" + uartSpeedSelect();
  h += "<label class='chk'><input type='checkbox' name='autobaud'" + String(g_autoBaud ? " checked" : "") + "> " + T(T_AUTOBAUD_LABEL) + "</label>";
  h += "<p class='hint' style='margin-top:6px'>" + T(T_AUTOBAUD_HINT) + "</p></div>";
  h += "<br><button type='submit'>" + T(T_SAVE) + "</button></form>";
  h += "<p style='margin-top:14px'><a href='/'>" + T(T_BACK) + "</a></p>";
  sendPage(h);
}

void handleSettingsPost() {
  if (!checkPortalAuth() || !checkCsrf()) return;
  String err;

  // Пароли: пустое поле = оставить сохранённый, галочка = отключить
  if (webServer.hasArg("authpass_clear")) g_authPassword = "";
  else if (webServer.arg("authpass").length()) g_authPassword = webServer.arg("authpass");

  g_portalUseSessionPass = webServer.hasArg("portal_use_session");
  if (webServer.hasArg("portal_clear")) g_portalPassword = "";
  else if (webServer.arg("portal_pass").length()) g_portalPassword = webServer.arg("portal_pass");

  g_idleTimeoutMin = constrain((int)webServer.arg("idle_min").toInt(), 0, 1440);
  g_autoBaud = webServer.hasArg("autobaud");

  String host = sanitizeHostname(webServer.arg("hostname"));
  bool hostChanged = host.length() && host != g_hostname;
  if (host.length()) g_hostname = host;

  String ap = webServer.arg("ap_pass");
  if (ap.length()) {
    if (ap.length() >= 8 && ap.length() <= 63) g_apPassword = ap;
    else err = "ap";
  }

  for (int i = 0; i < WHITELIST_SLOTS; i++) g_whiteListRaw[i] = webServer.arg("wl" + String(i));
  rebuildWhiteList();
  g_staticIpEnabled = webServer.hasArg("static_en");
  g_staticIpStr   = webServer.arg("static_ip");
  g_staticMaskStr = webServer.arg("static_mask");
  g_staticGwStr   = webServer.arg("static_gw");

  long baud = webServer.arg("baud").toInt();
  if (isValidBaud(baud)) applyUartBaud(baud, true);

  saveSecuritySettings();
  if (hostChanged) { WiFi.setHostname(g_hostname.c_str()); startMdns(); }

  webServer.sendHeader("Location", err.length() ? "/settings?saved=1&err=" + err : String("/settings?saved=1"));
  webServer.send(303);
}

// ---------- Wi-Fi ----------
void handleWifiGet() {
  if (!checkPortalAuth()) return;
  if (webServer.hasArg("scan")) doWifiScan();

  String h = pageBegin(T(T_WIFI_TITLE), "/wifi");
  h += "<h2>" + T(T_WIFI_TITLE) + "</h2>";

  h += "<div class='section'><h3>" + T(T_CURRENT_NETWORK) + "</h3>";
  h += row(T(T_MODE), apMode ? T(T_MODE_AP) : T(T_MODE_STA));
  h += row(T(T_NETWORK), htmlEscape(apMode ? g_apSsid : g_ssid));
  if (!apMode) h += row(T(T_NETWORK_TYPE), g_eapEnabled ? T(T_SECURITY_ENTERPRISE) : T(T_SECURITY_PSK));
  h += row(T(T_IP_ADDRESS), currentIp());
  h += row(T(T_MAC_ADDRESS), apMode ? WiFi.softAPmacAddress() : WiFi.macAddress());
  h += "</div>";

  h += "<form method='GET' action='/wifi' style='margin-top:14px'><button type='submit' name='scan' value='1'>" + T(T_SCAN_BUTTON) + "</button></form>";

  h += "<form method='POST' action='/wifi'>" + csrfField();
  h += "<div class='section'><label>" + T(T_SELECT_NETWORK) + "</label>";
  if (g_scanCount == 0) {
    h += "<p class='hint'>" + T(T_SCAN_HINT) + "</p>";
  } else {
    for (int i = 0; i < g_scanCount; i++) {
      String s = htmlEscape(g_scanSsid[i]);
      h += "<div class='radiorow'><label><input type='radio' name='ssid' value='" + s + "'" + (g_scanSsid[i] == g_ssid ? " checked" : "") + "> " + s +
           (g_scanSecure[i] ? " &#x1F512;" : "") + "</label><span class='dim'>" + String(g_scanRssi[i]) + " dBm</span></div>";
    }
  }
  h += "<br><label>" + T(T_MANUAL_SSID) + "</label><input type='text' name='ssid_manual' maxlength='32' placeholder='SSID'>";
  h += "<p class='hint'>" + T(T_MANUAL_SSID_HINT) + "</p></div>";

  h += "<div class='section'><label>" + T(T_NETWORK_TYPE) + "</label>";
  h += "<label class='chk'><input type='radio' name='sec_mode' value='psk'" + String(!g_eapEnabled ? " checked" : "") +
       " onclick=\"document.getElementById('pskb').style.display='block';document.getElementById('eapb').style.display='none'\"> " + T(T_SECURITY_PSK) + "</label>";
  h += "<label class='chk' style='margin-bottom:12px'><input type='radio' name='sec_mode' value='enterprise'" + String(g_eapEnabled ? " checked" : "") +
       " onclick=\"document.getElementById('pskb').style.display='none';document.getElementById('eapb').style.display='block'\"> " + T(T_SECURITY_ENTERPRISE) + "</label>";
  h += "<div id='pskb' style='display:" + String(!g_eapEnabled ? "block" : "none") + "'>";
  h += "<label>" + T(T_WIFI_PASSWORD) + "</label>" + passwordField("wifi_pass", g_staPassword.length() > 0);
  h += "<p class='hint'>" + T(T_WIFI_PASSWORD_HINT) + "</p>";
  h += "<label class='chk'><input type='checkbox' name='wifi_open'> " + T(T_WIFI_OPEN) + "</label></div>";
  h += "<div id='eapb' style='display:" + String(g_eapEnabled ? "block" : "none") + "'>";
  h += "<label>" + T(T_EAP_IDENTITY) + "</label><input type='text' name='eap_identity' value='" + htmlEscape(g_eapIdentity) + "'>";
  h += "<p class='hint'>" + T(T_EAP_IDENTITY_HINT) + "</p>";
  h += "<label>" + T(T_EAP_USERNAME) + "</label><input type='text' name='eap_username' value='" + htmlEscape(g_eapUsername) + "'>";
  h += "<label>" + T(T_EAP_PASSWORD) + "</label>" + passwordField("eap_password", g_eapPassword.length() > 0);
  h += "<p class='hint'>" + T(T_EAP_PASSWORD_HINT) + "</p></div></div>";

  h += "<div class='section'><label>" + T(T_TX_POWER) + "</label><select name='txpower'>";
  for (int i = 0; i < TXP_COUNT; i++)
    h += "<option value='" + String(TXP_VALUES[i]) + "'" + (TXP_VALUES[i] == g_txPowerQ ? " selected" : "") + ">" + TXP_LABELS[i] + "</option>";
  h += "</select><p class='hint'>" + T(T_TX_POWER_HINT) + "</p>";
  h += uartSpeedSelect() + "</div>";

  h += "<br><button type='submit'>" + T(T_SAVE_REBOOT) + "</button></form>";

  h += "<div class='section'><p class='hint' style='margin:0 0 12px'>" + T(T_RESET_WIFI_HINT) + "</p>";
  h += postButton("/wifi/reset", T(T_BTN_RESET_WIFI), "danger", T(T_CONFIRM_RESET_WIFI), true) + "</div>";
  h += "<p style='margin-top:14px'><a href='/'>" + T(T_BACK) + "</a></p>";
  sendPage(h);
}

void handleWifiPost() {
  if (!checkPortalAuth() || !checkCsrf()) return;
  String chosen = webServer.arg("ssid_manual");
  chosen.trim();
  if (chosen.length() == 0) chosen = webServer.arg("ssid");
  if (chosen.length() > 0 && chosen.length() <= 32) g_ssid = chosen;

  if (webServer.hasArg("wifi_open")) g_staPassword = "";
  else if (webServer.arg("wifi_pass").length()) g_staPassword = webServer.arg("wifi_pass");

  g_eapEnabled = webServer.arg("sec_mode") == "enterprise";
  if (g_eapEnabled) {
    if (webServer.arg("eap_identity").length()) g_eapIdentity = webServer.arg("eap_identity");
    if (webServer.arg("eap_username").length()) g_eapUsername = webServer.arg("eap_username");
    if (webServer.arg("eap_password").length()) g_eapPassword = webServer.arg("eap_password");
  }

  int txp = webServer.arg("txpower").toInt();
  if (isValidTxPower(txp)) g_txPowerQ = txp;
  long baud = webServer.arg("baud").toInt();
  if (isValidBaud(baud)) g_uartBaudSaved = baud;

  saveWifiSettings();
  sendRebootPage(T(T_SAVED));
}

void handleWifiReset() {
  if (!checkPortalAuth() || !checkCsrf()) return;
  clearWifiSettings(false);
  sendRebootPage(T(T_RESET_DONE));
}

// ---------- WireGuard ----------
#ifdef USE_WIREGUARD
void handleVpnGet() {
  if (!checkPortalAuth()) return;
  String h = pageBegin(T(T_VPN_TITLE), "/vpn");
  h += "<h2>" + T(T_VPN_TITLE) + "</h2>";
  if (webServer.hasArg("saved")) h += "<p class='msg'>" + T(T_SAVED) + "</p>";
  h += "<div class='section'>" + row(T(T_VPN_STATUS), !g_vpnEnabled ? badge(T(T_DISABLED), "off") :
       (g_vpnStarted ? badge(T(T_VPN_UP), "ok") : badge(T(T_VPN_DOWN), "warn"))) + "</div>";

  h += "<form method='POST' action='/vpn'>" + csrfField();
  h += "<div class='section'><label class='chk'><input type='checkbox' name='vpn_en'" + String(g_vpnEnabled ? " checked" : "") + "> " + T(T_VPN_ENABLE) + "</label></div>";
  h += "<div class='section'>";
  h += "<label>" + T(T_VPN_PRIVATE_KEY) + "</label>" + passwordField("vpn_priv", g_vpnPrivateKey.length() > 0);
  h += "<p class='hint'>" + T(T_VPN_PRIVATE_KEY_HINT) + "</p>";
  h += "<label>" + T(T_VPN_PEER_KEY) + "</label><input type='text' name='vpn_peer_pub' value='" + htmlEscape(g_vpnPeerPublicKey) + "'>";
  h += "<label>" + T(T_VPN_ENDPOINT) + "</label><input type='text' name='vpn_endpoint' value='" + htmlEscape(g_vpnEndpoint) + "' placeholder='vpn.example.com'>";
  h += "<label>" + T(T_VPN_PORT) + "</label><input type='number' name='vpn_port' min='1' max='65535' value='" + String(g_vpnPort) + "'>";
  h += "<label>" + T(T_VPN_LOCAL_IP) + "</label><input type='text' name='vpn_local_ip' value='" + htmlEscape(g_vpnLocalIp) + "' placeholder='10.0.0.2'>";
  h += "<p class='hint'>" + T(T_VPN_LOCAL_IP_HINT) + "</p>";
  h += "<label>" + T(T_VPN_KEEPALIVE) + "</label><input type='number' name='vpn_keepalive' min='0' max='3600' value='" + String(g_vpnKeepalive) + "'>";
#ifdef WIREGUARD_HAS_KEEPALIVE
  h += "<p class='hint'>" + T(T_VPN_KEEPALIVE_HINT) + "</p>";
#else
  h += "<p class='hint'>" + T(T_VPN_KEEPALIVE_LIBHINT) + "</p>";
#endif
  h += "</div><br><button type='submit'>" + T(T_SAVE) + "</button></form>";
  h += "<p style='margin-top:14px'><a href='/'>" + T(T_BACK) + "</a></p>";
  sendPage(h);
}

void handleVpnPost() {
  if (!checkPortalAuth() || !checkCsrf()) return;
  g_vpnEnabled = webServer.hasArg("vpn_en");
  if (webServer.arg("vpn_priv").length()) g_vpnPrivateKey = webServer.arg("vpn_priv");
  g_vpnPeerPublicKey = webServer.arg("vpn_peer_pub");
  g_vpnEndpoint      = webServer.arg("vpn_endpoint");
  g_vpnLocalIp       = webServer.arg("vpn_local_ip");
  long port = webServer.arg("vpn_port").toInt();
  if (port >= 1 && port <= 65535) g_vpnPort = (int)port;
  g_vpnKeepalive = constrain((int)webServer.arg("vpn_keepalive").toInt(), 0, 3600);
  saveVpnSettings();
  // Ответ отправляем до запуска туннеля: запуск может занять до ~10 с (NTP, DNS)
  webServer.sendHeader("Location", "/vpn?saved=1");
  webServer.send(303);
  if (g_vpnEnabled) startVpn(); else stopVpn();
}
#endif

// ---------- Обновление прошивки ----------
void handleUpdateGet() {
  if (!checkPortalAuth()) return;
  String h = pageBegin(T(T_UPDATE_TITLE), "/update");
  h += "<h2>" + T(T_UPDATE_TITLE) + "</h2><div class='section'>";
  h += row(T(T_BOARD), BOARD_NAME) + row(T(T_FIRMWARE), FW_VERSION);
  h += "<br><form method='POST' action='/update?csrf=" + g_csrfToken + "' enctype='multipart/form-data' "
       "onsubmit=\"this.querySelector('button').disabled=true;this.querySelector('button').innerText='" + T(T_UPDATE_PROGRESS) + "'\">";
  h += "<input type='file' name='firmware' accept='.bin' required>";
  h += "<p class='hint'>" + T(T_UPDATE_HINT) + "</p>";
  h += "<button type='submit'>" + T(T_UPDATE_BUTTON) + "</button></form></div>";
  h += "<p style='margin-top:14px'><a href='/'>" + T(T_BACK) + "</a></p>";
  sendPage(h);
}

// Приём файла прошивки потоком (вызывается по частям)
void handleUpdateUpload() {
  HTTPUpload &up = webServer.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      g_otaError = "";
      g_otaAllowed = portalAuthorized() && webServer.arg("csrf") == g_csrfToken;
      if (!g_otaAllowed) return;
      Serial.println("[OTA] Receiving " + up.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) g_otaError = Update.errorString();
      break;
    case UPLOAD_FILE_WRITE:
      if (g_otaAllowed && g_otaError.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize)
        g_otaError = Update.errorString();
      break;
    case UPLOAD_FILE_END:
      if (g_otaAllowed && g_otaError.isEmpty()) {
        if (Update.end(true)) Serial.printf("[OTA] Done, %u bytes\n", (unsigned)up.totalSize);
        else g_otaError = Update.errorString();
      }
      break;
    case UPLOAD_FILE_ABORTED:
      if (g_otaAllowed) Update.abort();
      g_otaError = "aborted";
      break;
    default: break;
  }
}

void handleUpdateDone() {
  if (!g_otaAllowed) {
    if (!checkPortalAuth()) return;
    webServer.send(403, "text/plain; charset=utf-8", T(T_CSRF_ERROR));
    return;
  }
  if (g_otaError.length()) {
    Serial.println("[OTA] Failed: " + g_otaError);
    String h = pageBegin(T(T_UPDATE_TITLE), "/update");
    h += "<div class='section'><p class='err'>" + T(T_UPDATE_FAIL) + ": " + htmlEscape(g_otaError) + "</p></div>";
    h += "<p><a href='/update'>" + T(T_BACK) + "</a></p>";
    sendPage(h);
    return;
  }
  sendRebootPage(T(T_UPDATE_OK));
}

// ---------- Прочее ----------
void handleLang() {
  if (!checkPortalAuth()) return;
  String set = webServer.arg("set");
  if (set == "en" || set == "ru") { g_langEn = (set == "en"); saveLanguage(); }
  // Только локальный путь — защита от открытого редиректа на чужой сайт
  String from = webServer.arg("from");
  if (!from.startsWith("/") || from.startsWith("//") || from.indexOf('\\') >= 0 ||
      from.indexOf('\r') >= 0 || from.indexOf('\n') >= 0) from = "/";
  webServer.sendHeader("Location", from);
  webServer.send(303);
}

void handleKick() {
  if (!checkPortalAuth() || !checkCsrf()) return;
  kickRequested = true;
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

// Определение скорости по кнопке (только когда консоль свободна — иначе вмешаемся в чужую сессию)
void handleBaudDetect() {
  if (!checkPortalAuth() || !checkCsrf()) return;
  String res;
  if (g_sessionActive && client.connected()) res = "busy";
  else {
    long b = detectConsoleBaud(true);
    if (b) { saveSecuritySettings(); res = String(b); } else res = "fail";
  }
  webServer.sendHeader("Location", "/?baud=" + res);
  webServer.send(303);
}

void handleRestart() {
  if (!checkPortalAuth() || !checkCsrf()) return;
  sendRebootPage(T(T_RESTARTING));
}

void handleCss() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "text/css", PORTAL_CSS);
}

void handleNotFound() {
  webServer.sendHeader("Location", "/");
  webServer.send(302);
}

void setupWebPortal() {
  uint8_t rnd[12];
  esp_fill_random(rnd, sizeof(rnd));
  g_csrfToken = "";
  char hx[3];
  for (size_t i = 0; i < sizeof(rnd); i++) { snprintf(hx, sizeof(hx), "%02x", rnd[i]); g_csrfToken += hx; }

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/style.css", HTTP_GET, handleCss);
  webServer.on("/settings", HTTP_GET, handleSettingsGet);
  webServer.on("/settings", HTTP_POST, handleSettingsPost);
  webServer.on("/wifi", HTTP_GET, handleWifiGet);
  webServer.on("/wifi", HTTP_POST, handleWifiPost);
  webServer.on("/wifi/reset", HTTP_POST, handleWifiReset);
#ifdef USE_WIREGUARD
  webServer.on("/vpn", HTTP_GET, handleVpnGet);
  webServer.on("/vpn", HTTP_POST, handleVpnPost);
#endif
  webServer.on("/update", HTTP_GET, handleUpdateGet);
  webServer.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  webServer.on("/lang", HTTP_GET, handleLang);
  webServer.on("/kick", HTTP_POST, handleKick);
  webServer.on("/baud/detect", HTTP_POST, handleBaudDetect);
  webServer.on("/restart", HTTP_POST, handleRestart);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("[SYSTEM] Web portal started on port " + String(WEB_PORT));
}

// Отложенная перезагрузка после ответа браузеру
void webLoop() {
  webServer.handleClient();
  if (g_restartAt && (long)(millis() - g_restartAt) >= 0) {
    Serial.println("[SYSTEM] Restarting...");
    delay(100);
    ESP.restart();
  }
}
