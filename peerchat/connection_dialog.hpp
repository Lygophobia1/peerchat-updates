#pragma once
#include <windows.h>
#include <string>

struct ConnectionOptions {
    bool hosting = true;
    std::wstring name = L"You";
    std::wstring address = L"0.0.0.0";
    unsigned short port = 7777;
    std::wstring key;
};

bool showConnectionDialog(HWND owner, ConnectionOptions& options);
