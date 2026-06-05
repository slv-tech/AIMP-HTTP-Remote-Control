#include "network.h"
#include "globals.h"

std::vector<NetworkInterface> EnumNetworkInterfaces() {
    std::vector<NetworkInterface> result;
    ULONG bufLen = 15000;
    std::vector<BYTE> buf(bufLen);
    for (int attempt = 0; attempt < 3; ++attempt) {
        ULONG ret = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            buf.resize(bufLen);
            continue;
        }
        if (ret != ERROR_SUCCESS) break;
        for (auto* a = (IP_ADAPTER_ADDRESSES*)buf.data(); a; a = a->Next) {
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (a->OperStatus != IfOperStatusUp) continue;
            for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
                auto* sin = (sockaddr_in*)ua->Address.lpSockaddr;
                wchar_t ipBuf[INET_ADDRSTRLEN] = {};
                InetNtopW(AF_INET, &sin->sin_addr, ipBuf, INET_ADDRSTRLEN);
                std::wstring friendly = a->FriendlyName ? std::wstring(a->FriendlyName) : L"Unknown";
                NetworkInterface iface;
                iface.ip          = ipBuf;
                iface.displayName = friendly + L" (" + ipBuf + L")";
                result.push_back(std::move(iface));
            }
        }
        break;
    }
    return result;
}

static bool ParseIPv4(const std::string& s, uint32_t& out) {
    unsigned int a, b, c, d;
    if (sscanf(s.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}

static bool MatchCIDR(uint32_t ip, const std::string& entry) {
    auto slash = entry.find('/');
    if (slash == std::string::npos) {
        uint32_t addr = 0;
        return ParseIPv4(entry, addr) && (ip == addr);
    }
    std::string addrPart   = entry.substr(0, slash);
    std::string prefixPart = entry.substr(slash + 1);
    uint32_t addr = 0;
    if (!ParseIPv4(addrPart, addr)) return false;
    int prefix = 0;
    try { prefix = std::stoi(prefixPart); } catch (...) { return false; }
    if (prefix < 0 || prefix > 32) return false;
    if (prefix == 0) return true;
    uint32_t mask = prefix == 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1u);
    return (ip & mask) == (addr & mask);
}

static std::vector<std::string> SplitTrim(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim)) {
        auto start = token.find_first_not_of(" \t\r\n");
        auto end   = token.find_last_not_of(" \t\r\n");
        if (start != std::string::npos)
            result.push_back(token.substr(start, end - start + 1));
    }
    return result;
}

bool IsAllowedClient(uint32_t clientIpNetworkOrder) {
    if (!g_allowListEnabled) return true;
    if (g_allowList.empty()) return false;
    uint32_t ip = ntohl(clientIpNetworkOrder);
    for (const auto& entry : SplitTrim(g_allowList, ',')) {
        if (MatchCIDR(ip, entry)) return true;
    }
    return false;
}
