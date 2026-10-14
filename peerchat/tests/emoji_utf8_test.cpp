#include "../portable/utf8.hpp"
#include <iostream>
#include <vector>
int main(){
    const std::vector<std::string> corpus={u8"😀",u8"👩🏽‍💻",u8"👨‍👩‍👧‍👦",u8"🇺🇸",u8"1️⃣",u8"❤️",u8"🏳️‍🌈",u8"日本語 العربية é\n"};
    for(const auto& text:corpus)if(peerchat::unicode::encode(peerchat::unicode::decode(text))!=text)return 1;
    for(const auto& bad:std::vector<std::string>{"\xf0\x80\x80\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x9f"}){try{peerchat::unicode::decode(bad);return 2;}catch(const std::invalid_argument&){}}
    std::cout<<"PASS: portable strict UTF-8 emoji, ZWJ, modifiers, flags, selectors, multiline Unicode and malformed scalar rejection\n";
}
