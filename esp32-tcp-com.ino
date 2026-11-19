// =========================================================================
// ESP32 Cisco Console Server — удалённый COM-порт (TCP <-> UART) для консоли
// сетевого оборудования. Один скетч для ESP32-C3 и ESP32-S3: плата
// определяется автоматически по выбранному в Arduino IDE чипу.
// https://github.com/  (см. README.md)
// =========================================================================
#define ESP32-S3
// #define ESP32-C3
#define FW_VERSION "1.0.0"

// ===== ОПЦИИ СБОРКИ — должны стоять ВЫШЕ всех #include =====
// #define USE_BAN_LIST                    // блокировка IP на час после 3 неверных паролей консоли
// #define USE_WIREGUARD                   // страница /vpn и клиент WireGuard (нужна библиотека WireGuard-ESP32, см. README)
// Переопределение пинов (по умолчанию: C3 — RX 20 / TX 21, S3 — RX 12 / TX 13):
// #define RX1_PIN 12
// #define TX1_PIN 13

// ===== ЗАВОДСКИЕ ЗНАЧЕНИЯ (используются, пока в NVS ничего не сохранено) =====
#define DEFAULT_SSID            "your_ssid"
#define DEFAULT_PASSWORD        "your_password"
#define DEFAULT_HOSTNAME_PREFIX "console"      // имя хоста по умолчанию: console-XXXX (хвост MAC)
#define AP_DEFAULT_PASSWORD     "console1234"  // пароль резервной AP (8–63 символа); меняется на портале,
                                              // кнопка BOOT возвращает его к этому значению
#define DEFAULT_UART_BAUD       9600
#define DEFAULT_TX_POWER_Q      34             // 8.5 dBm — безопасно для плат со слабым питанием
#define CONSOLE_TCP_PORT        8888
#define WEB_PORT                80
#define PORTAL_USER             "admin"

// ===== ТАЙМИНГИ =====
#define WIFI_CONNECT_TIMEOUT_MS 15000UL        // одна попытка подключения при старте
#define AP_RETRY_INTERVAL_MS    (3UL * 60000)  // как часто из резервной AP пробовать вернуться в сеть
#define AP_RETRY_WINDOW_MS      20000UL        // сколько ждать подключения при такой попытке
#define STA_LOST_REBOOT_MS      (10UL * 60000) // нет Wi-Fi дольше — перезагрузка
#define AUTH_TIMEOUT_MS         30000UL        // время на ввод пароля консоли
#define TAKEOVER_IDLE_MS        60000UL        // повторное подключение с того же IP вытесняет сессию, простаивающую дольше
#define TCP_KEEPALIVE_IDLE_S    30             // TCP keepalive: «мёртвый» клиент отваливается
#define TCP_KEEPALIVE_INTVL_S   10             //   примерно через IDLE + INTVL * COUNT секунд
#define TCP_KEEPALIVE_COUNT     3
#define BOOT_RESET_HOLD_MS      5000UL         // удержание BOOT: сброс Wi-Fi и пароля портала

#define WHITELIST_SLOTS         5
#define MAX_SCAN_RESULTS        20
#define BAN_MAX_TRACKED_IPS     10
#define BAN_MAX_ATTEMPTS        3
#define BAN_LOCKOUT_MS          3600000UL

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <lwip/sockets.h>
#include <time.h>
#ifdef USE_WIREGUARD
  #include <WireGuard-ESP32.h>
#endif

#include "board_config.h"
#include "lang.h"
#include "settings.h"
#include "telnet.h"
#include "console.h"
#include "net_mgr.h"
#include "web.h"

// Удержание кнопки BOOT 5 с — сброс Wi-Fi и пароля портала (на случай, если забыли)
static void checkBootButton() {
  static unsigned long pressStart = 0;
  static bool pressed = false;
  bool now = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (now && !pressed) { pressed = true; pressStart = millis(); }
  else if (!now) pressed = false;
  if (pressed && millis() - pressStart >= BOOT_RESET_HOLD_MS) {
    Serial.println("[SYSTEM] BOOT held — resetting Wi-Fi settings and portal password.");
    clearWifiSettings(true);
    delay(200);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[SYSTEM] ESP32 Console Server %s (%s), reset reason %d\n", FW_VERSION, BOARD_NAME, (int)esp_reset_reason());
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  loadSettings();

  Serial1.setRxBufferSize(UART_RX_BUF);
  Serial1.setTxBufferSize(UART_TX_BUF);
  Serial1.begin(g_uartBaud, SERIAL_8N1, RX1_PIN, TX1_PIN);
  Serial.printf("[SYSTEM] UART1 RX=GPIO%d TX=GPIO%d %ld baud\n", RX1_PIN, TX1_PIN, g_uartBaud);

  networkBegin();

  server.begin();
  bootMillis = millis();
  Serial.printf("[SYSTEM] Console TCP server on port %d\n", CONSOLE_TCP_PORT);
  setupWebPortal();
}

void loop() {
  checkBootButton();
  webLoop();
  networkLoop();
  consoleLoop();
  delay(1);   // отдаём время системным задачам (Wi-Fi, lwIP)
}
