#include "../group.hpp"
#include "../portable/identity_format.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv){
    if(argc!=2)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    peerchat::Blob bytes((std::istreambuf_iterator<char>(input)),{});
    if(bytes.size()!=136||!peerchat::identity_format::validPublicRepresentation(bytes.data(),72))return 3;
    peerchat::Blob publicKey(bytes.begin(),bytes.begin()+72),signature(bytes.begin()+72,bytes.end());
    // Include the embedded NUL in the length passed to the existing verifier.
    const char payload[]=u8"PeerChat deterministic restart proof / 日本語\0😀";
    peerchat::Blob message(payload,payload+sizeof(payload)-1);
    if(!peerchat::verifyDevicePayload(message,signature,publicKey))return 4;
    message[0]^=1;if(peerchat::verifyDevicePayload(message,signature,publicKey))return 5;
    std::cout<<"PASS: actual Android Keystore signature verified by unchanged Windows CNG; tamper rejected\n";
    return 0;
}
