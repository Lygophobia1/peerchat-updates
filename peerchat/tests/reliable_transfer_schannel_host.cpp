#include "reliable_transfer_interop_fixture.hpp"
int main(int argc,char** argv){using namespace transfer_interop;try{
    protocol::check(argc==3);const std::string path=argv[1];Side side(unicode::decode(path),argv[2]);
    std::cout<<side.profile.peerId<<std::endl;std::string line;
    while(std::getline(std::cin,line)){
        if(line=="V"){side.verify();std::cout<<"VERIFIED"<<std::endl;return 0;}
        protocol::check(line.rfind("R:",0)==0);
        try{auto output=side.exchange(unhex(line.substr(2)));std::cout<<"O:"<<hex(output)<<"\nS:"<<(side.complete()?1:0)<<std::endl;}
        catch(const std::exception& e){std::cerr<<"FAIL Windows stage="<<side.stage<<" endpoint="<<side.endpoint->diagnosticStage()<<" reason="<<e.what()<<'\n';throw;}
    }return 2;
}catch(const std::exception& e){std::cerr<<"FAIL Schannel transfer fixture: "<<e.what()<<'\n';return 1;}}
