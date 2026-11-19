/*
 * WireGuard implementation for ESP32 Arduino by Kenta Ida (fuga@fugafuga.org)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <IPAddress.h>

// Пропатченная версия: есть setPersistentKeepalive() и блокировка TCPIP-ядра для ESP-IDF 5.x
#define WIREGUARD_HAS_KEEPALIVE 1

class WireGuard
{
private:
    bool _is_initialized = false;
    uint16_t _keepalive = 0xFFFF; // 0xFFFF = значение библиотеки по умолчанию (15 с), 0 = выключено

public:
    bool begin(const IPAddress &localIP, const IPAddress &Subnet, const IPAddress &Gateway, const char *privateKey, const char *remotePeerAddress, const char *remotePeerPublicKey, uint16_t remotePeerPort, const char *presharedKey = NULL);
    bool begin(const IPAddress &localIP, const char *privateKey, const char *remotePeerAddress, const char *remotePeerPublicKey, uint16_t remotePeerPort, const char *presharedKey = NULL);
    void end();
    // Persistent keepalive в секундах (вызывать до begin()); 0 — выключить
    void setPersistentKeepalive(uint16_t seconds) { this->_keepalive = seconds; }
    bool is_initialized() const { return this->_is_initialized; }
};
