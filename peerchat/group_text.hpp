#pragma once
#include "protocol.hpp"
#include <wincrypt.h>

namespace peerchat::group_text {
struct Body {std::uint64_t time=0,order=0;std::wstring text;};
inline Blob encode(const Body& body){
    protocol::check(body.time>0&&body.time<=INT64_MAX&&body.order>0&&body.order<=INT64_MAX&&!body.text.empty()&&body.text.size()<=protocol::MaxChatCharacters);
    protocol::Writer writer(protocol::Text);writer.string("PeerChat.GroupText.v1");writer.number(body.time);writer.number(body.order);writer.text(body.text);return writer.data;
}
inline Body decode(const Blob& bytes){
    protocol::Reader reader(bytes);protocol::check(reader.type()==protocol::Text&&reader.string()=="PeerChat.GroupText.v1");
    Body body;body.time=reader.number();body.order=reader.number();body.text=reader.text();reader.end();
    protocol::check(body.time>0&&body.time<=INT64_MAX&&body.order>0&&body.order<=INT64_MAX&&!body.text.empty()&&body.text.size()<=protocol::MaxChatCharacters);return body;
}
inline bool valid(const Blob& bytes){try{decode(bytes);return true;}catch(...){return false;}}
inline std::wstring base64(const Blob& bytes){
    DWORD size=0;protocol::check(CryptBinaryToStringW(bytes.data(),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&size)!=FALSE);
    std::wstring text(size,L'\0');protocol::check(CryptBinaryToStringW(bytes.data(),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,text.data(),&size)!=FALSE);text.resize(size);return text;
}
}
