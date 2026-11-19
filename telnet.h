// =========================================================================
// telnet.h — разбор протокола Telnet и управление COM-портом по RFC 2217
// =========================================================================
// Из потока клиента вырезаются служебные последовательности IAC, на запросы
// согласования отвечаем корректно (WILL/WONT/DO/DONT), а подпереговоры
// COM-PORT-OPTION (RFC 2217) позволяют виртуальным COM-портам Windows/Linux
// задавать скорость UART прямо из терминальной программы.
#pragma once

#define TN_IAC   255
#define TN_DONT  254
#define TN_DO    253
#define TN_WONT  252
#define TN_WILL  251
#define TN_SB    250
#define TN_SE    240
#define TELOPT_ECHO     1
#define TELOPT_SGA      3
#define TELOPT_COMPORT  44

enum TelnetState : uint8_t { TS_DATA, TS_IAC, TS_VERB, TS_SB, TS_SB_IAC };

static TelnetState tnState = TS_DATA;
static uint8_t tnVerb = 0;
static uint8_t tnSb[24];
static uint8_t tnSbLen = 0;
static bool    tnComPort = false;     // клиент согласовал RFC 2217
static bool    tnPendingCR = false;   // для схлопывания CR LF / CR NUL

static const uint8_t TELNET_GREETING[] = {
  TN_IAC, TN_WILL, TELOPT_ECHO,   // эхо делает удалённая сторона (Cisco) — клиент отключит локальное
  TN_IAC, TN_WILL, TELOPT_SGA     // без Go-Ahead, посимвольный режим
};

void telnetReset() {
  tnState = TS_DATA;
  tnVerb = 0;
  tnSbLen = 0;
  tnComPort = false;
  tnPendingCR = false;
}

static void telnetSend(const uint8_t* d, size_t n) {
  if (client) client.write(d, n);
}

static void telnetReply(uint8_t verb, uint8_t opt) {
  uint8_t b[3] = {TN_IAC, verb, opt};
  telnetSend(b, 3);
}

// Ответ на подпереговоры RFC 2217: код сервера = код клиента + 100, 0xFF в данных удваивается
static void rfc2217Reply(uint8_t cmd, const uint8_t* val, size_t n) {
  uint8_t out[48];
  size_t k = 0;
  out[k++] = TN_IAC; out[k++] = TN_SB; out[k++] = TELOPT_COMPORT; out[k++] = (uint8_t)(cmd + 100);
  for (size_t i = 0; i < n && k < sizeof(out) - 4; i++) {
    out[k++] = val[i];
    if (val[i] == TN_IAC) out[k++] = TN_IAC;
  }
  out[k++] = TN_IAC; out[k++] = TN_SE;
  telnetSend(out, k);
}

static void rfc2217Handle(uint8_t cmd, const uint8_t* v, size_t n) {
  switch (cmd) {
    case 0: { // SIGNATURE: пустой запрос — сообщить свою подпись
      if (n == 0) {
        const char* sig = "ESP32 Console Server (" BOARD_NAME ")";
        rfc2217Reply(0, (const uint8_t*)sig, strlen(sig));
      }
      break;
    }
    case 1: { // SET-BAUDRATE (4 байта, big-endian; 0 = запрос текущей)
      if (n >= 4) {
        uint32_t b = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
        if (b >= 300 && b <= 921600) {
          applyUartBaud((long)b, false);
          Serial.printf("[RFC2217] Baud rate set to %lu by client\n", (unsigned long)b);
        }
      }
      uint32_t cur = (uint32_t)g_uartBaud;
      uint8_t r[4] = {(uint8_t)(cur >> 24), (uint8_t)(cur >> 16), (uint8_t)(cur >> 8), (uint8_t)cur};
      rfc2217Reply(1, r, 4);
      break;
    }
    case 2: { uint8_t r = 8; rfc2217Reply(2, &r, 1); break; } // DATASIZE — поддерживаем только 8
    case 3: { uint8_t r = 1; rfc2217Reply(3, &r, 1); break; } // PARITY — только NONE
    case 4: { uint8_t r = 1; rfc2217Reply(4, &r, 1); break; } // STOPSIZE — только 1
    case 5: { // SET-CONTROL: управления потоком и линий DTR/RTS у нас нет
      uint8_t req = n ? v[0] : 0, r;
      if (req == 0 || req == 1 || req == 2 || req == 3) r = 1;   // flow control: NONE
      else if (req == 7) r = 8;                                  // запрос DTR -> "включён"
      else if (req == 10) r = 11;                                // запрос RTS -> "включён"
      else r = req;                                              // остальное подтверждаем как есть
      rfc2217Reply(5, &r, 1);
      break;
    }
    case 10: case 11: { uint8_t r = n ? v[0] : 0; rfc2217Reply(cmd, &r, 1); break; } // маски состояний
    case 12: { // PURGE-DATA: 1 = приём, 2 = передача, 3 = оба
      uint8_t r = n ? v[0] : 0;
      if (r == 1 || r == 3) while (Serial1.available()) Serial1.read();
      rfc2217Reply(12, &r, 1);
      break;
    }
    default: break; // FLOWCONTROL-SUSPEND/RESUME и прочее — игнорируем
  }
}

static void telnetNegotiate(uint8_t verb, uint8_t opt) {
  switch (verb) {
    case TN_WILL:
      if (opt == TELOPT_COMPORT) {
        if (!tnComPort) { tnComPort = true; telnetReply(TN_DO, opt); }
      } else if (opt != TELOPT_ECHO && opt != TELOPT_SGA) {
        telnetReply(TN_DONT, opt);   // TERMINAL-TYPE, NAWS и т.п. — не нужны
      }
      break;
    case TN_DO:
      if (opt == TELOPT_COMPORT) {
        if (!tnComPort) { tnComPort = true; telnetReply(TN_WILL, opt); }
      } else if (opt != TELOPT_ECHO && opt != TELOPT_SGA) {
        telnetReply(TN_WONT, opt);
      }
      break;
    default: break;                  // WONT / DONT — просто принимаем
  }
}

// Фильтрует буфер на месте: возвращает длину полезных данных.
// normalizeCR=true — заодно схлопывает CR LF / CR NUL в одиночный CR (для консоли Cisco).
int telnetFilter(uint8_t* buf, int len, bool normalizeCR) {
  int out = 0;
  for (int i = 0; i < len; i++) {
    uint8_t b = buf[i];
    switch (tnState) {
      case TS_DATA:
        if (b == TN_IAC) { tnState = TS_IAC; break; }
        if (normalizeCR) {
          if (tnPendingCR) { tnPendingCR = false; if (b == 0x0A || b == 0x00) break; }
          if (b == 0x0D) tnPendingCR = true;
        }
        buf[out++] = b;
        break;
      case TS_IAC:
        if (b == TN_IAC) { buf[out++] = TN_IAC; tnState = TS_DATA; }       // экранированный 0xFF — данные
        else if (b == TN_SB) { tnSbLen = 0; tnState = TS_SB; }
        else if (b >= TN_WILL && b <= TN_DONT) { tnVerb = b; tnState = TS_VERB; }
        else tnState = TS_DATA;                                             // NOP, GA, AYT и т.п.
        break;
      case TS_VERB:
        telnetNegotiate(tnVerb, b);
        tnState = TS_DATA;
        break;
      case TS_SB:
        if (b == TN_IAC) tnState = TS_SB_IAC;
        else if (tnSbLen < sizeof(tnSb)) tnSb[tnSbLen++] = b;
        break;
      case TS_SB_IAC:
        if (b == TN_SE) {
          if (tnSbLen >= 2 && tnSb[0] == TELOPT_COMPORT) rfc2217Handle(tnSb[1], tnSb + 2, tnSbLen - 2);
          tnState = TS_DATA;
        } else {
          if (b == TN_IAC && tnSbLen < sizeof(tnSb)) tnSb[tnSbLen++] = TN_IAC; // 0xFF внутри SB
          tnState = TS_SB;
        }
        break;
    }
  }
  return out;
}

// Запись в клиента с удвоением 0xFF (по протоколу Telnet байт 0xFF в данных экранируется)
void clientWriteEscaped(const uint8_t* buf, size_t len) {
  size_t start = 0;
  static const uint8_t iac = TN_IAC;
  for (size_t i = 0; i < len; i++) {
    if (buf[i] == TN_IAC) {
      client.write(buf + start, i - start + 1);  // включая сам 0xFF
      client.write(&iac, 1);                     // и ещё один
      start = i + 1;
    }
  }
  if (start < len) client.write(buf + start, len - start);
}
