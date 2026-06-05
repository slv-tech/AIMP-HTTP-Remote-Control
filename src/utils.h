#pragma once
#include "pch.h"

// wchar_t* -> UTF-8 string
std::string WStr(const wchar_t* w);

// wstring -> UTF-8 string
std::string WStrToUtf8(const std::wstring& w);

// UTF-8 string -> wstring
std::wstring Utf8ToWStr(const std::string& s);
