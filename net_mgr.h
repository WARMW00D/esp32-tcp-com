// =========================================================================
// net_mgr.h — Wi-Fi (STA / резервная AP), повторные попытки, mDNS, NTP, WireGuard
// =========================================================================
#pragma once

#ifdef USE_WIREGUARD
static WireGuard g_wg;
#endif

bool          g_staRetryActive = false;  // в режиме AP идёт фоновая попытка подключиться к сети
unsigned long g_staRetryStart  = 0;
unsigned long g_lastStaRetry   = 0;
unsigned long g_lastStaOk      = 0;      // последний момент, когда STA была подключена
bool          g_mdnsStarted    = false;

// ---------- Кэш сканирования ----------
String g_scanSsid[MAX_SCAN_RESULTS];
int    g_scanRssi[MAX_SCAN_RESULTS];
bool   g_scanSecure[MAX_SCAN_RESULTS];
int    g_scanCount = 0;

void applyTxPower() {
  WiFi.setTxPower((wifi_power_t)g_txPowerQ);
}

// Статический IP применяется после WiFi.mode(), иначе на ядре 3.x настройка теряется
void applyStaticIp() {
  if (!g_staticIpEnabled) return;
  IPAddress ip, gw, mask;
  if (ip.fromString(g_staticIpStr) && gw.fromString(g_staticGwStr) && mask.fromString(g_staticMaskStr)) {
    WiFi.config(ip, gw, mask, gw);   // DNS = шлюз
  } else {
    Serial.println("[ERROR] Static IP settings invalid, using DHCP.");
  }
}

// Подключение к сохранённой сети: обычной (PSK/открытой) или WPA2-Enterprise (PEAP/MSCHAPv2)
void startStaConnect() {
  applyStaticIp();
  if (g_eapEnabled) {
    WiFi.begin(g_ssid.c_str(), WPA2_AUTH_PEAP, g_eapIdentity.c_str(), g_eapUsername.c_str(), g_eapPassword.c_str());
  } else {
    WiFi.begin(g_ssid.c_str(), g_staPassword.c_str());
  }
}

static bool waitForSta(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void startMdns() {
  if (g_mdnsStarted) MDNS.end();
  g_mdnsStarted = MDNS.begin(g_hostname.c_str());
  if (g_mdnsStarted) {
    MDNS.addService("http", "tcp", WEB_PORT);
    MDNS.addService("telnet", "tcp", CONSOLE_TCP_PORT);
    Serial.println("[SYSTEM] mDNS: http://" + g_hostname + ".local");
  }
}

// ---------- Время (нужно WireGuard: метка времени в рукопожатии должна расти) ----------
bool syncTime(unsigned long timeoutMs) {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  unsigned long t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < timeoutMs) delay(100);
  bool ok = time(nullptr) >= 1700000000;
  Serial.println(ok ? "[SYSTEM] Time synchronized (NTP)." : "[WARNING] NTP time sync failed.");
  return ok;
}

// ---------- WireGuard ----------
#ifdef USE_WIREGUARD
void stopVpn() {
  if (g_vpnStarted) {
    g_wg.end();
    g_vpnStarted = false;
    Serial.println("[SYSTEM] WireGuard tunnel stopped.");
  }
}

// Можно вызывать повторно: старый туннель сначала закрывается (повторный begin() без end() роняет lwIP)
void startVpn() {
  stopVpn();
  if (!g_vpnEnabled || apMode) return;
  if (g_vpnPrivateKey.length() == 0 || g_vpnPeerPublicKey.length() == 0 ||
      g_vpnEndpoint.length() == 0 || g_vpnLocalIp.length() == 0) {
    Serial.println("[WARNING] WireGuard enabled, but configuration is incomplete.");
    return;
  }
  IPAddress localIp;
  if (!localIp.fromString(g_vpnLocalIp)) {
    Serial.println("[ERROR] WireGuard tunnel IP is invalid: " + g_vpnLocalIp);
    return;
  }
  if (time(nullptr) < 1700000000 && !syncTime(10000)) {
    Serial.println("[WARNING] Starting WireGuard without valid time — the server may reject the handshake.");
  }
#ifdef WIREGUARD_HAS_KEEPALIVE
  g_wg.setPersistentKeepalive((uint16_t)g_vpnKeepalive);
#endif
  g_vpnStarted = g_wg.begin(localIp, g_vpnPrivateKey.c_str(), g_vpnEndpoint.c_str(),
                            g_vpnPeerPublicKey.c_str(), (uint16_t)g_vpnPort);
  Serial.println(g_vpnStarted ? "[SYSTEM] WireGuard tunnel initialized." : "[ERROR] WireGuard tunnel failed to initialize.");
}
#endif

// Сервисы, которые поднимаются после успешного подключения к сети
static void onStaConnected() {
  g_lastStaOk = millis();
  Serial.println("[SYSTEM] Connected. IP: " + WiFi.localIP().toString() + "  RSSI: " + String(WiFi.RSSI()) + " dBm");
  startMdns();
#ifdef USE_WIREGUARD
  startVpn();
#endif
}

static void startFallbackAp(const char* why) {
  apMode = true;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  applyTxPower();
  bool ok = WiFi.softAP(g_apSsid.c_str(), g_apPassword.c_str());
  Serial.printf("[SYSTEM] %s Fallback AP %s: SSID \"%s\", password \"%s\", IP %s\n",
                why, ok ? "started" : "FAILED", g_apSsid.c_str(), g_apPassword.c_str(),
                WiFi.softAPIP().toString().c_str());
  startMdns();
  g_lastStaRetry = millis();
}

void networkBegin() {
  g_apSsid = String(AP_SSID_PREFIX) + "-" + macSuffix(2);
  WiFi.persistent(false);              // не дублировать настройки во флеш драйвера Wi-Fi — у нас свои в NVS
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(g_hostname.c_str());

  if (g_ssid.length() == 0) {
    startFallbackAp("No Wi-Fi network configured.");
    return;
  }

  WiFi.mode(WIFI_STA);
  applyTxPower();
  Serial.print("[SYSTEM] Connecting to \"" + g_ssid + "\"" + (g_eapEnabled ? " (WPA2-Enterprise)" : ""));
  startStaConnect();
  if (!waitForSta(WIFI_CONNECT_TIMEOUT_MS)) {
    // Радио после программного рестарта иногда не переинициализируется — полностью выключаем и пробуем ещё раз
    Serial.print("[WARNING] First attempt failed, power-cycling the radio and retrying");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(300);
    WiFi.mode(WIFI_STA);
    applyTxPower();
    delay(100);
    startStaConnect();
    waitForSta(WIFI_CONNECT_TIMEOUT_MS);
  }

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    onStaConnected();
  } else {
    startFallbackAp("Configured Wi-Fi unreachable.");
  }
}

// Переход из резервной AP в обычный режим без перезагрузки
static void switchApToSta() {
  Serial.println("[SYSTEM] Configured network is back — leaving fallback AP.");
  apMode = false;
  g_staRetryActive = false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  onStaConnected();
}

// Вызывается из loop()
void networkLoop() {
  if (apMode) {
    if (g_ssid.length() == 0) return;     // сеть не настроена — ждём настройки через портал

    if (!g_staRetryActive) {
      // Периодически пробуем вернуться в сеть (типично: после отключения света
      // роутер загружается дольше ESP32). Не мешаем, если к AP кто-то подключён.
      if (millis() - g_lastStaRetry > AP_RETRY_INTERVAL_MS && WiFi.softAPgetStationNum() == 0) {
        Serial.println("[SYSTEM] Fallback AP: retrying \"" + g_ssid + "\" in background...");
        g_staRetryActive = true;
        g_staRetryStart = millis();
        WiFi.mode(WIFI_AP_STA);
        startStaConnect();
      }
    } else if (WiFi.status() == WL_CONNECTED) {
      switchApToSta();
    } else if (millis() - g_staRetryStart > AP_RETRY_WINDOW_MS) {
      WiFi.disconnect(false);
      WiFi.mode(WIFI_AP);
      g_staRetryActive = false;
      g_lastStaRetry = millis();
      Serial.println("[SYSTEM] Background retry failed, staying in fallback AP.");
    }
    return;
  }

  // Режим STA: драйвер переподключается сам; если связи нет слишком долго —
  // перезагружаемся (помогает при «зависшем» радио на проблемных платах).
  if (WiFi.status() == WL_CONNECTED) {
    g_lastStaOk = millis();
  } else if (millis() - g_lastStaOk > STA_LOST_REBOOT_MS) {
    Serial.println("[WARNING] Wi-Fi lost for too long — restarting.");
    delay(100);
    ESP.restart();
  }
}

// Сканирование (блокирует на 2–4 с). Из режима AP временно включается AP+STA.
void doWifiScan() {
  bool wasApOnly = (WiFi.getMode() == WIFI_AP);
  if (wasApOnly) WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  g_scanCount = 0;
  for (int i = 0; i < n && g_scanCount < MAX_SCAN_RESULTS; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < g_scanCount; j++) if (g_scanSsid[j] == s) { dup = true; break; }
    if (dup) continue;
    g_scanSsid[g_scanCount]   = s;
    g_scanRssi[g_scanCount]   = WiFi.RSSI(i);
    g_scanSecure[g_scanCount] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    g_scanCount++;
  }
  WiFi.scanDelete();
  if (wasApOnly && !g_staRetryActive) WiFi.mode(WIFI_AP);
}
