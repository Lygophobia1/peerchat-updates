// HOST ONLY: genuine group Opus/ICE/DTLS-SRTP/NetEq through a local TURN fixture.
// Audio device pacing and PCM are deterministic; no Android OS or microphone.
#include "group_voice_session.hpp"
#include "android_voice_media_integration_audio.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace peerchat;
namespace device=voice_integration_audio;
using Clock=std::chrono::steady_clock;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
std::uint64_t utc(){return static_cast<std::uint64_t>(std::time(nullptr));}
struct Side {
    std::uint64_t account;std::string id,participation;
    device::OwnerScope creationOwner;
    GroupVoiceSession media{std::make_unique<RelayMeshVoiceTopology>()};
    bool joined=false;
    Side(std::uint64_t account,char deviceChar,char tokenChar):account(account),id(64,deviceChar),
        participation(32,tokenChar),creationOwner(id){}
    std::string key()const{return std::to_string(account)+":"+id+":"+participation;}
    AuthorizedVoiceSource source()const{return {account,id,participation};}
    VoiceRoomBinding binding(std::uint64_t revision)const{return {std::string(32,'1'),std::string(32,'2'),
        std::string(32,'3'),id,participation,account,revision,1,1};}
    void start(const MediaIceConfiguration& ice,const std::wstring& receive,std::uint64_t revision){
        VoiceMedia::ProcessingOptions processing{};processing.enabled=false;
        std::wstring error;device::OwnerScope owner(id);
        if(!media.start(binding(revision),ice,receive,processing,-1,-1,1.f,1.f,error)){
            std::wcerr<<L"Group media startup: "<<error<<L'\n';throw std::runtime_error("Genuine group media startup failed");
        }joined=true;
    }
    void roster(std::uint64_t revision,const std::vector<AuthorizedVoiceSource>& sources){
        check(media.reconcile(revision,1,sources),"Authorized group source reconciliation failed");
    }
    void stop(){media.stop();joined=false;}
    device::Snapshot audio()const{for(auto s:device::snapshots())if(s.owner==id&&s.active)return s;return {};}
    GroupVoiceSession::Snapshot state(){return media.snapshot();}
};
struct Fixture {
    Side a{1,'a','4'},b{2,'b','5'},c{3,'c','6'};
    VoiceTransportSignal capturedFromC{};bool savedC=false;
    void pump(){
        Side* all[]={&a,&b,&c};
        for(int round=0;round<8;++round){bool moved=false;
            for(auto* sender:all)if(sender->joined)for(auto signal:sender->media.takeSignals()){
                for(auto* target:all)if(target->joined&&target->key()==signal.sourceKey){
                    signal.sourceKey=sender->key();if(sender==&c&&!savedC){capturedFromC=signal;savedC=true;}
                    check(target->media.signaling(signal),"Authorized in-memory group signaling was rejected");moved=true;break;
                }
            }
            if(!moved)break;
        }
    }
    void step(){pump();device::step();pump();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    template<class Predicate> void wait(Predicate ready,const char* why,unsigned milliseconds=20000){
        const auto until=Clock::now()+std::chrono::milliseconds(milliseconds);
        do{step();if(ready())return;}while(Clock::now()<until);
        std::cerr<<"Timed out routes A="<<a.state().routes.size()<<" B="<<b.state().routes.size()<<" C="<<c.state().routes.size()<<'\n';
        throw std::runtime_error(why);
    }
    void run(unsigned blocks){for(unsigned i=0;i<blocks;++i)step();}
};
bool connected(Side& side,std::size_t count){auto value=side.state();return value.routes.size()==count&&
    std::all_of(value.routes.begin(),value.routes.end(),[](const auto& route){return route.connected;});}
}
int main(int argc,char** argv){try{
    check(argc==6,"Usage: executable receiveDll TURN-host TURN-port TURN-user TURN-password");
    std::wstring receive;for(const char* p=argv[1];*p;++p)receive.push_back(static_cast<unsigned char>(*p));
    MediaIceConfiguration ice;ice.host=argv[2];ice.port=static_cast<std::uint16_t>(std::stoi(argv[3]));
    ice.username=argv[4];ice.password=argv[5];ice.expiresUtc=static_cast<std::int64_t>(utc()+3600);
    ice.profileSessionGeneration=1;ice.discoveryGeneration=1;ice.generation=1;
    check(ice.valid(std::time(nullptr))&&ice.policy==MediaIceConfiguration::Policy::RelayOnly,"TURN fixture configuration invalid");
    Fixture f;f.a.start(ice,receive,1);f.a.roster(1,{});
    f.run(4);check(f.a.state().active&&f.a.state().routes.empty()&&f.a.audio().captures>0,
        "One-participant room did not capture through genuine codec");
    f.b.start(ice,receive,2);f.a.roster(2,{f.b.source()});f.b.roster(2,{f.a.source()});
    f.wait([&]{return connected(f.a,1)&&connected(f.b,1);},"Two participants did not establish TURN media");
    f.wait([&]{return f.a.audio().energy>100000&&f.b.audio().energy>100000&&
        f.a.state().receive.acceptedPackets>10&&f.b.state().receive.acceptedPackets>10;},
        "Bidirectional genuine decoded PCM did not arrive");
    auto firstA=f.a.audio(),firstB=f.b.audio();
    check(firstA.tone660>firstA.tone330*2&&firstB.tone330>firstB.tone660*2,
        "Bidirectional decoded tones were not attributed to remote devices");
    f.c.start(ice,receive,3);f.a.roster(3,{f.b.source(),f.c.source()});
    f.b.roster(3,{f.a.source(),f.c.source()});f.c.roster(3,{f.a.source(),f.b.source()});
    f.wait([&]{return connected(f.a,2)&&connected(f.b,2)&&connected(f.c,2);},
        "Late third participant did not establish both TURN routes");
    f.wait([&]{return f.a.state().receive.activeSources==2&&f.b.state().receive.activeSources==2&&
        f.c.state().receive.activeSources==2&&f.c.audio().energy>100000;},
        "Three-participant NetEq source attribution or mixed PCM failed");
    auto threeA=f.a.state(),threeB=f.b.state(),threeC=f.c.state();f.run(30);
    auto afterA=f.a.state(),afterB=f.b.state();
    check(threeA.receive.mixedBlocks>0&&threeB.receive.mixedBlocks>0&&threeC.receive.mixedBlocks>0&&
        afterA.fanoutCopies-threeA.fanoutCopies>afterA.encodedPackets-threeA.encodedPackets&&
        afterB.fanoutCopies-threeB.fanoutCopies>afterB.encodedPackets-threeB.encodedPackets,
        "Shared one-encode multi-source fanout or genuine mixing absent");
    std::wstring error;check(f.b.media.mute(true,error),"Mute failed");auto mutedB=f.b.audio();
    f.run(15);check(f.b.audio().captures==mutedB.captures&&f.a.audio().energy>firstA.energy,
        "Mute failed to stop only the selected microphone");
    f.a.media.deafen(true);auto deafA=f.a.audio();f.run(8);
    check(f.a.audio().energy==deafA.energy&&f.a.audio().supplied>deafA.supplied,
        "Deafen did not suppress PCM while NetEq continued");
    f.a.media.deafen(false);check(f.b.media.mute(false,error),"Unmute failed");
    check(f.savedC,"No genuine late-join signal available for revocation replay");
    f.c.stop();f.a.roster(4,{f.b.source()});f.b.roster(4,{f.a.source()});
    check(!f.a.media.signaling(f.capturedFromC),"Revoked participant signaling was accepted");
    f.wait([&]{return connected(f.a,1)&&connected(f.b,1)&&f.a.state().receive.authorizedSources==1&&
        f.b.state().receive.authorizedSources==1;},"Revoked source was not retired");
    f.a.stop();f.b.stop();auto retired=device::snapshots();f.run(5);
    auto final=device::snapshots();check(retired.size()==final.size(),"Teardown created an unexpected device");
    for(std::size_t i=0;i<final.size();++i)check(!final[i].active&&retired[i].captures==final[i].captures,
        "Retired group microphone continued capturing");
    std::cout<<"PASS: host genuine TURN group media, 3 participants, late join, decoded bidirectional tones, two NetEq sources, mute/deafen, revoked signal rejection and teardown. HOST ONLY synthetic PCM.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
