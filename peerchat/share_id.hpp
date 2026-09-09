#pragma once
#include <cstdint>
#include <string>
namespace peerchat {
struct SharedContact { std::wstring host; std::uint16_t port=0; std::string peerId; };
std::wstring encodeShareId(const SharedContact& contact);
bool parseShareId(const std::wstring& value,SharedContact& contact,std::wstring& error);
}
