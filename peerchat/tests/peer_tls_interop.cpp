#include "../tls.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
static std::string hex(const peerchat::Blob& bytes){const char* digits="0123456789abcdef";std::string out;for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
static peerchat::Blob bytes(const std::string& text){if(text.size()>512*1024||text.size()%2)throw std::runtime_error("Invalid fixture input");peerchat::Blob out;for(size_t i=0;i<text.size();i+=2){auto pair=text.substr(i,2);if(pair.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid fixture hex");out.push_back(static_cast<unsigned char>(std::stoul(pair,nullptr,16)));}return out;}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;
    const std::string path=argv[1],expected=argv[2];
    peerchat::Storage storage(std::wstring(path.begin(),path.end()));auto profile=storage.profile();
    peerchat::TlsCredentials credentials(profile);peerchat::TlsSession tls(credentials,true);
    std::cout<<hex(profile.publicKey)<<std::endl;
    std::string line;
    while(std::getline(std::cin,line)){
        if(line=="X")return 0;
        if(line.rfind("R:",0)!=0)throw std::runtime_error("Invalid fixture command");
        tls.receive(bytes(line.substr(2)));
        if(tls.ready()&&peerchat::Storage::publicKeyId(tls.remotePublicKey())!=expected)throw std::runtime_error("Peer pin mismatch");
        auto plaintext=tls.takePlaintext();if(!plaintext.empty())tls.send(plaintext);
        // Private pipe to the synthetic test harness only; never diagnostics.
        std::cout<<"O:"<<hex(tls.takeOutput())<<"\nP:"<<hex(plaintext)<<"\nR:"<<(tls.ready()?1:0)<<std::endl;
    }
    return 0;
}catch(...){std::cerr<<"FAIL: Windows Schannel interoperability fixture\n";return 1;}}
