#pragma once
#include "pch.h"

struct HttpRequest {
    std::string method, path, body;
    std::map<std::string, std::string> params;
};

HttpRequest ParseRequest(const std::string& raw);
void        SendResponse(SOCKET client, int code, const json& body);
void        RunHttpServer();
void        RestartHttpServer();
