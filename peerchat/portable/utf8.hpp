#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace peerchat::unicode {
// Strict standard UTF-8, never JNI Modified UTF-8. Limits elsewhere remain
// measured in UTF-16 code units to preserve the Windows wire contract.
inline std::wstring decode(const std::string& bytes) {
    std::wstring out;
    for (size_t i=0;i<bytes.size();) {
        auto first=static_cast<unsigned char>(bytes[i++]);
        uint32_t cp=first; unsigned extra=0;
        if(first>=0xc2&&first<=0xdf){cp=first&31;extra=1;}
        else if(first>=0xe0&&first<=0xef){cp=first&15;extra=2;}
        else if(first>=0xf0&&first<=0xf4){cp=first&7;extra=3;}
        else if(first>=0x80)throw std::invalid_argument("Invalid UTF-8");
        if(extra>bytes.size()-i)throw std::invalid_argument("Truncated UTF-8");
        for(unsigned n=0;n<extra;++n){auto next=static_cast<unsigned char>(bytes[i++]);if((next&0xc0)!=0x80)throw std::invalid_argument("Invalid UTF-8 continuation");cp=(cp<<6)|(next&63);}
        if((extra==1&&cp<0x80)||(extra==2&&cp<0x800)||(extra==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::invalid_argument("Invalid Unicode scalar");
        if constexpr(sizeof(wchar_t)==2){if(cp>0xffff){cp-=0x10000;out.push_back(static_cast<wchar_t>(0xd800+(cp>>10)));out.push_back(static_cast<wchar_t>(0xdc00+(cp&1023)));}else out.push_back(static_cast<wchar_t>(cp));}
        else out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}
inline std::string encode(const std::wstring& text) {
    std::string out;
    for(size_t i=0;i<text.size();++i){uint32_t cp=static_cast<uint32_t>(text[i]);
        if constexpr(sizeof(wchar_t)==2){if(cp>=0xd800&&cp<=0xdbff){if(++i>=text.size())throw std::invalid_argument("Truncated surrogate");uint32_t low=static_cast<uint32_t>(text[i]);if(low<0xdc00||low>0xdfff)throw std::invalid_argument("Invalid surrogate");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}}
        if(cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::invalid_argument("Invalid Unicode scalar");
        if(cp<0x80)out.push_back(static_cast<char>(cp));
        else if(cp<0x800){out.push_back(static_cast<char>(0xc0|(cp>>6)));out.push_back(static_cast<char>(0x80|(cp&63)));}
        else if(cp<0x10000){out.push_back(static_cast<char>(0xe0|(cp>>12)));out.push_back(static_cast<char>(0x80|((cp>>6)&63)));out.push_back(static_cast<char>(0x80|(cp&63)));}
        else{out.push_back(static_cast<char>(0xf0|(cp>>18)));out.push_back(static_cast<char>(0x80|((cp>>12)&63)));out.push_back(static_cast<char>(0x80|((cp>>6)&63)));out.push_back(static_cast<char>(0x80|(cp&63)));}
    }
    return out;
}
inline size_t scalarUtf16Units(uint32_t scalar){return scalar>0xffff?2:1;}
inline size_t utf16Units(const std::wstring& text){
    if constexpr(sizeof(wchar_t)==2)return text.size();
    size_t units=0;for(auto c:text)units+=scalarUtf16Units(static_cast<uint32_t>(c));return units;
}
}
