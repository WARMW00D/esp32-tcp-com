// =========================================================================
// board_config.h — автоматический выбор параметров под плату
// =========================================================================
// Плата определяется при сборке по выбранному в Arduino IDE чипу
// (ESP32C3 Dev Module / ESP32S3 Dev Module). Любой параметр можно
// переопределить, объявив его #define в начале esp32-tcp-com.ino.
#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define BOARD_NAME            "ESP32-S3"
  #define BOARD_RX1_PIN         12    // GPIO12 <- TXD модуля MAX3232 (диапазон GPIO0-13, без strapping-пинов)
  #define BOARD_TX1_PIN         13    // GPIO13 -> RXD модуля MAX3232
  #define BOARD_BOOT_PIN        0     // штатная кнопка BOOT на S3
  #define BOARD_UART_RX_BUF     8192
  #define BOARD_UART_TX_BUF     2048
  #define BOARD_BRIDGE_BUF      1024
  #define BOARD_AP_SSID_PREFIX  "ESP32-S3-Console"
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  #define BOARD_NAME            "ESP32-C3"
  #define BOARD_RX1_PIN         20    // GPIO20 <- TXD модуля MAX3232
  #define BOARD_TX1_PIN         21    // GPIO21 -> RXD модуля MAX3232
  #define BOARD_BOOT_PIN        9     // штатная кнопка BOOT на C3 Super Mini
  #define BOARD_UART_RX_BUF     4096
  #define BOARD_UART_TX_BUF     1024
  #define BOARD_BRIDGE_BUF      512
  #define BOARD_AP_SSID_PREFIX  "ESP32-Console"
#else
  #error "Поддерживаются только ESP32-C3 и ESP32-S3 (выберите ESP32C3 Dev Module или ESP32S3 Dev Module)"
#endif

#ifndef RX1_PIN
  #define RX1_PIN BOARD_RX1_PIN
#endif
#ifndef TX1_PIN
  #define TX1_PIN BOARD_TX1_PIN
#endif
#ifndef BOOT_BUTTON_PIN
  #define BOOT_BUTTON_PIN BOARD_BOOT_PIN
#endif
#ifndef UART_RX_BUF
  #define UART_RX_BUF BOARD_UART_RX_BUF
#endif
#ifndef UART_TX_BUF
  #define UART_TX_BUF BOARD_UART_TX_BUF
#endif
#ifndef BRIDGE_BUF_SIZE
  #define BRIDGE_BUF_SIZE BOARD_BRIDGE_BUF
#endif
#ifndef AP_SSID_PREFIX
  #define AP_SSID_PREFIX BOARD_AP_SSID_PREFIX
#endif
