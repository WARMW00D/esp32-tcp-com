// =========================================================================
// console.h — TCP-сессия консоли: приём подключений, авторизация, мост TCP<->UART
// =========================================================================
#pragma once

bool          g_sessionActive = false; // есть открытая сессия (в т.ч. в стадии ввода пароля)
bool          isAuthenticated = false;
unsigned long g_sessionStart  = 0;
unsigned long g_lastActivity  = 0;   // последний ввод от клиента (для таймаута простоя)
unsigned long g_authStart     = 0;   // начало ожидания пароля
String        g_authInput;           // вводимый пароль
bool          g_authSkipLF    = false; // после CR пропустить следующий LF/NUL
bool          kickRequested   = false;
uint8_t       bridgeBuf[BRIDGE_BUF_SIZE];

// ---------- Блокировка по IP после неверных паролей ----------
#ifdef USE_BAN_LIST
struct IPBanRecord {
  IPAddress ip;
  uint8_t wrongAttempts = 0;
  unsigned long lockoutStart = 0;
  bool isLocked = false;
};
IPBanRecord banList[BAN_MAX_TRACKED_IPS];

// Ищет запись IP; если нет — занимает свободный слот, а при переполнении
// вытесняет незаблокированную запись с наименьшим числом ошибок.
int banRecordIndex(const IPAddress &ip) {
  int freeIdx = -1, evictIdx = -1;
  for (int i = 0; i < BAN_MAX_TRACKED_IPS; i++) {
    if (banList[i].ip == ip && (banList[i].wrongAttempts || banList[i].isLocked)) return i;
    if (!banList[i].isLocked && banList[i].wrongAttempts == 0) { if (freeIdx < 0) freeIdx = i; }
    else if (!banList[i].isLocked && (evictIdx < 0 || banList[i].wrongAttempts < banList[evictIdx].wrongAttempts)) evictIdx = i;
  }
  int idx = freeIdx >= 0 ? freeIdx : evictIdx;
  if (idx < 0) return -1;  // все слоты заблокированы — новые IP не отслеживаются
  banList[idx].ip = ip;
  banList[idx].wrongAttempts = 0;
  banList[idx].isLocked = false;
  return idx;
}

bool isIpBanned(const IPAddress &ip) {
  for (int i = 0; i < BAN_MAX_TRACKED_IPS; i++)
    if (banList[i].isLocked && banList[i].ip == ip) return true;
  return false;
}

void banListExpire() {
  for (int i = 0; i < BAN_MAX_TRACKED_IPS; i++) {
    if (banList[i].isLocked && millis() - banList[i].lockoutStart >= BAN_LOCKOUT_MS) {
      banList[i].isLocked = false;
      banList[i].wrongAttempts = 0;
      Serial.println("[SECURITY] Lockout expired for IP: " + banList[i].ip.toString());
    }
  }
}
#endif

// ---------- Автоопределение скорости консоли ----------
// На каждой скорости шлём Ctrl+U (очистить строку ввода) и Enter и оцениваем ответ:
// на правильной скорости устройство вернёт читаемое приглашение ("Router>", "Switch#",
// "Password:"), на неправильной — мусор. Ctrl+U убирает «мусорные» символы, которые
// устройство успело принять на чужих скоростях, чтобы Enter не выполнил их как команду.
static int scoreBaudResponse(const uint8_t* buf, int n) {
  if (n < 2) return 0;
  int printable = 0, prompt = 0;
  for (int i = 0; i < n; i++) {
    uint8_t c = buf[i];
    if ((c >= 0x20 && c <= 0x7E) || c == '\r' || c == '\n' || c == '\t') printable++;
    if (c == '>' || c == '#' || c == ':' || c == ']') prompt = 1;
  }
  int ratio = printable * 100 / n;
  if (ratio < 90) return 0;
  return ratio + prompt * 100 + (n > 4 ? 10 : 0);
}

static int probeBaud(long baud) {
  Serial1.updateBaudRate(baud);
  delay(30);
  while (Serial1.available()) Serial1.read();
  const uint8_t probe[] = {0x15, '\r'};
  Serial1.write(probe, sizeof(probe));
  Serial1.flush();
  uint8_t buf[96];
  int n = 0;
  unsigned long t0 = millis(), lastByte = 0;
  while (millis() - t0 < 400 && n < (int)sizeof(buf)) {
    if (Serial1.available()) { buf[n++] = Serial1.read(); lastByte = millis(); }
    else if (n > 0 && millis() - lastByte > 60) break;   // ответ закончился
    else delay(2);
  }
  return scoreBaudResponse(buf, n);
}

// Возвращает найденную скорость (и применяет её) или 0, если ни одна не подошла.
long detectConsoleBaud(bool persist) {
  Serial.println("[AUTOBAUD] Detecting console speed...");
  long best = 0;
  int bestScore = 0;
  // Быстрый путь: текущая скорость уже правильная
  int s = probeBaud(g_uartBaudSaved);
  if (s >= 190) { best = g_uartBaudSaved; bestScore = s; }
  else {
    const long order[] = {9600, 115200, 57600, 38400, 19200, 4800, 2400, 1200};
    for (long b : order) {
      if (b == g_uartBaudSaved) continue;
      s = probeBaud(b);
      if (s > bestScore) { bestScore = s; best = b; }
      if (s >= 190) break;      // уверенный результат: читаемый текст с приглашением
    }
    if (bestScore < 100) best = 0;   // ничего похожего на текст — оставляем сохранённую скорость
  }
  if (best) {
    applyUartBaud(best, persist);
    Serial.printf("[AUTOBAUD] Detected %ld baud (score %d)\n", best, bestScore);
  } else {
    applyUartBaud(g_uartBaudSaved, false);
    Serial.println("[AUTOBAUD] No readable response at any speed.");
  }
  while (Serial1.available()) Serial1.read();
  return best;
}

// ---------- Управление сессией ----------
void endSession(const char* reason) {
  if (client.connected() && reason) client.println(reason);
  client.stop();
  g_sessionActive = false;
  isAuthenticated = false;
  g_authInput = "";
  if (g_uartBaud != g_uartBaudSaved) {      // клиент менял скорость по RFC 2217 — вернуть сохранённую
    applyUartBaud(g_uartBaudSaved, false);
    Serial.printf("[SYSTEM] UART baud restored to %ld\n", g_uartBaudSaved);
  }
}

// TCP keepalive: «мёртвый» клиент (ноутбук уснул, пропал из сети) отваливается примерно за минуту
static void enableTcpKeepalive(WiFiClient &c) {
  int fd = c.fd();
  if (fd < 0) return;
  int on = 1, idle = TCP_KEEPALIVE_IDLE_S, intvl = TCP_KEEPALIVE_INTVL_S, cnt = TCP_KEEPALIVE_COUNT;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
}

// Автоопределение при подключении клиента (если включено в настройках)
static void runSessionAutobaud() {
  client.print("AUTOBAUD: ");
  long b = detectConsoleBaud(true);
  if (b) { saveSecuritySettings(); client.println(String(b) + " baud"); }
  else client.println("FAILED, using " + String(g_uartBaud) + " baud");
}

static void startSession(WiFiClient &nc) {
  client = nc;
  g_sessionActive = true;
  client.setNoDelay(true);
  enableTcpKeepalive(client);
  telnetReset();
  g_authInput = "";
  g_authSkipLF = false;
  g_sessionStart = g_lastActivity = g_authStart = millis();
  client.write(TELNET_GREETING, sizeof(TELNET_GREETING));
  String ip = client.remoteIP().toString();
  if (g_authPassword.length() > 0) {
    isAuthenticated = false;
    client.println("AUTH_REQUIRED");
    client.print("Password: ");
    Serial.println("[SYSTEM] Client connected, awaiting password: " + ip);
  } else {
    isAuthenticated = true;
    client.println("CONNECTED_NO_AUTH");
    Serial.println("[SYSTEM] Client connected (auth disabled): " + ip);
    if (g_autoBaud) runSessionAutobaud();
  }
}

void acceptClients() {
  if (!server.hasClient()) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  WiFiClient nc = server.accept();
#else
  WiFiClient nc = server.available();
#endif
  if (!nc) return;
  IPAddress ip = nc.remoteIP();

  if (g_whiteCount > 0 && !isIpInAllowedNetworks(ip)) {
    Serial.println("[WARNING] Connection REJECTED (not in White List): " + ip.toString());
    nc.stop();
    return;
  }
#ifdef USE_BAN_LIST
  if (isIpBanned(ip)) {
    Serial.println("[SECURITY] Connection REJECTED (banned IP): " + ip.toString());
    nc.println("YOUR_IP_IS_LOCKED_OUT");
    nc.stop();
    return;
  }
#endif
  if (g_sessionActive && client.connected()) {
    // Повторное подключение с того же IP вытесняет старую сессию, если та «зависла»
    // (ещё не авторизована или без ввода дольше TAKEOVER_IDLE_MS). Живую сессию не трогаем:
    // за NAT (например, WireGuard с маскарадингом) у всех клиентов может быть один IP.
    bool stale = !isAuthenticated || millis() - g_lastActivity > TAKEOVER_IDLE_MS;
    if (client.remoteIP() == ip && stale) {
      Serial.println("[SYSTEM] Same IP reconnected, replacing old session: " + ip.toString());
      endSession("SESSION_TAKEN_OVER");
    } else {
      nc.println("BUSY");
      nc.stop();
      return;
    }
  }
  startSession(nc);
}

// Ввод пароля консоли (посимвольно, со звёздочками)
static void handleAuthInput() {
  if (millis() - g_authStart > AUTH_TIMEOUT_MS) {
    Serial.println("[SECURITY] Password timeout: " + client.remoteIP().toString());
    client.print("\r\n");
    endSession("AUTH_TIMEOUT");
    return;
  }
  while (client.available() > 0) {
    uint8_t b = (uint8_t)client.read();
    if (telnetFilter(&b, 1, false) == 0) continue;     // служебный байт Telnet

    if (g_authSkipLF) { g_authSkipLF = false; if (b == '\n' || b == 0) continue; }
    if (b == 0x08 || b == 0x7F) {                      // Backspace / Delete
      if (g_authInput.length() > 0) { g_authInput.remove(g_authInput.length() - 1); client.print("\b \b"); }
      continue;
    }
    if (b != '\r' && b != '\n') {
      if (b >= 0x20 && g_authInput.length() < 128) { g_authInput += (char)b; client.print('*'); }
      continue;
    }
    if (b == '\r') g_authSkipLF = true;                // Enter = CR, CR LF, CR NUL или LF

    client.print("\r\n");
    String input = g_authInput;
    g_authInput = "";
    IPAddress ip = client.remoteIP();
    if (input == g_authPassword) {
      isAuthenticated = true;
      g_lastActivity = millis();
#ifdef USE_BAN_LIST
      int idx = banRecordIndex(ip);
      if (idx >= 0) banList[idx].wrongAttempts = 0;
#endif
      client.println("AUTH_OK");
      Serial.println("[SYSTEM] Access GRANTED for IP: " + ip.toString());
      if (g_autoBaud) runSessionAutobaud();
    } else {
      Serial.println("[SECURITY] Wrong password from " + ip.toString());
#ifdef USE_BAN_LIST
      int idx = banRecordIndex(ip);
      if (idx >= 0 && ++banList[idx].wrongAttempts >= BAN_MAX_ATTEMPTS) {
        banList[idx].isLocked = true;
        banList[idx].lockoutStart = millis();
        Serial.println("[SECURITY] IP " + ip.toString() + " BANNED");
        endSession("TOO_MANY_ATTEMPTS_IP_LOCKED");
        return;
      }
#endif
      endSession("AUTH_FAILED");
    }
    return;
  }
}

// Прозрачный мост. Количество порций за один проход ограничено,
// чтобы непрерывный поток не «вешал» веб-портал.
static void bridgeData() {
  for (int n = 0; n < 4 && client.available() > 0; n++) {
    int len = client.read(bridgeBuf, BRIDGE_BUF_SIZE);
    if (len <= 0) break;
    g_lastActivity = millis();
    len = telnetFilter(bridgeBuf, len, true);
    if (len > 0) { Serial1.write(bridgeBuf, len); rxBytesTotal += len; }
  }
  for (int n = 0; n < 4 && Serial1.available() > 0; n++) {
    int avail = Serial1.available();
    int len = Serial1.read(bridgeBuf, avail > BRIDGE_BUF_SIZE ? BRIDGE_BUF_SIZE : avail);
    if (len <= 0) break;
    clientWriteEscaped(bridgeBuf, len);
    txBytesTotal += len;
  }
}

void consoleLoop() {
#ifdef USE_BAN_LIST
  banListExpire();
#endif
  acceptClients();

  if (kickRequested) {
    kickRequested = false;
    if (g_sessionActive) {
      Serial.println("[SYSTEM] Client kicked via web portal.");
      endSession("KICKED_BY_ADMIN");
    }
  }

  if (!g_sessionActive) return;
  if (!client.connected()) {           // клиент закрыл соединение сам
    Serial.println("[SYSTEM] Client disconnected.");
    endSession(nullptr);
    return;
  }

  if (!isAuthenticated) { handleAuthInput(); return; }

  if (g_idleTimeoutMin > 0 && millis() - g_lastActivity > (unsigned long)g_idleTimeoutMin * 60000UL) {
    Serial.println("[SYSTEM] Session idle timeout.");
    client.print("\r\n");
    endSession("IDLE_TIMEOUT");
    return;
  }
  bridgeData();
}
