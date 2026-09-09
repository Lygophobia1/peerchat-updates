#include "share_id.hpp"
#include <iostream>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    using namespace peerchat;
    SharedContact original{L"friend.example.test",54321,std::string(64,'a')};
    auto encoded=encodeShareId(original);require(encoded.rfind(L"P2PCHAT1:",0)==0,"missing version prefix");
    SharedContact parsed;std::wstring error;require(parseShareId(encoded,parsed,error),"valid ID rejected");
    require(parsed.host==original.host&&parsed.port==original.port&&parsed.peerId==original.peerId,"round trip changed contact");
    require(!parseShareId(L"P2PCHAT1:not-valid",parsed,error),"malformed ID accepted");
    require(!parseShareId(L"P2PCHAT2:abc",parsed,error),"wrong version accepted");
    std::wcout<<L"Share ID tests passed.\n";return 0;
}
