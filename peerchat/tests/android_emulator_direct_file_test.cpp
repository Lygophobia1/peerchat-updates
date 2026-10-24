#include "reliable_transfer_interop_fixture.hpp"
#include <winsock.h>
using namespace transfer_interop;
struct Bridge {
 SOCKET listener=INVALID_SOCKET,client=INVALID_SOCKET;
 explicit Bridge(unsigned port){WSADATA startup{};protocol::check(WSAStartup(MAKEWORD(2,2),&startup)==0);listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);protocol::check(listener!=INVALID_SOCKET);sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(static_cast<u_short>(port));protocol::check(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0&&listen(listener,1)==0);std::cout<<"LISTENING "<<port<<std::endl;client=accept(listener,nullptr,nullptr);protocol::check(client!=INVALID_SOCKET);DWORD timeout=45000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));}
 ~Bridge(){if(client!=INVALID_SOCKET)closesocket(client);if(listener!=INVALID_SOCKET)closesocket(listener);WSACleanup();}
 std::string read(){std::string result;for(;;){char c=0;protocol::check(recv(client,&c,1,0)==1);if(c=='\n')break;if(c!='\r')result+=c;protocol::check(result.size()<=131080);}return result;}
 void write(const std::string& value){auto data=value+"\n";size_t at=0;while(at<data.size()){auto n=send(client,data.data()+at,static_cast<int>(data.size()-at),0);protocol::check(n>0);at+=n;}}
};
int main(int argc,char** argv){try{
 protocol::check(argc==3);auto port=std::stoul(argv[2]);protocol::check(port>=1024&&port<=65535);Bridge bridge(static_cast<unsigned>(port));auto remote=bridge.read();protocol::check(protocol::id(remote));Side side(unicode::decode(argv[1]),remote);bridge.write(side.profile.peerId);
 for(;;){auto line=bridge.read();if(line=="V"){side.verify();bridge.write("VERIFIED");std::cout<<"PASS Windows direct payload, completion ACK, v2 window16 and bounded opaque output; Android peer uses real emulator JNI/POSIX/Keystore"<<std::endl;return 0;}protocol::check(line.rfind("R:",0)==0);Blob out;try{out=side.exchange(unhex(line.substr(2)));}catch(const std::exception& e){std::cerr<<"stage="<<side.stage<<" endpoint="<<side.endpoint->diagnosticStage()<<" reason="<<e.what()<<std::endl;throw;}bridge.write("O:"+hex(out));bridge.write(std::string("S:")+(side.complete()?"1":"0"));}
}catch(const std::exception& e){std::cerr<<"FAIL direct emulator fixture: "<<e.what()<<std::endl;return 1;}}
