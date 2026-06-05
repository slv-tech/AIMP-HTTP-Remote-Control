#pragma once
#include "pch.h"

struct NetworkInterface {
    std::wstring displayName;  // "Ethernet (192.168.1.5)"
    std::wstring ip;           // "192.168.1.5"
};

// Перечислить активные IPv4-интерфейсы системы (кроме loopback)
std::vector<NetworkInterface> EnumNetworkInterfaces();

// Проверить входящий IP (network byte order) против allowlist
// Возвращает true если доступ разрешён
bool IsAllowedClient(uint32_t clientIpNetworkOrder);
