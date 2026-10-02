#include "voice_media.hpp"
#include "audio_receive/peerchat_audio_receive.h"
#include "call_manager.hpp"
#include <opus/opus.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void put32(unsigned char* p,std::uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
int main(int argc,char**){try{
    if(argc>1){
        peerchat::VoiceMedia::ReceiveBinding binding{std::string(64,'a'),std::string(32,'b'),1,1};
        peerchat::VoiceMedia media(peerchat::MediaIceConfiguration::directForTests(),-1,-1,1,"",{},binding);
        std::wstring failure;check(media.startOffer(failure),"receive DLL failure must not fail transport creation");
        bool audioFailure=false;
        for(const auto& event:media.poll()){
            check(event.kind!=peerchat::VoiceMedia::EventKind::Failed&&event.kind!=peerchat::VoiceMedia::EventKind::Disconnected,"no transport failure/recovery");
            if(event.kind==peerchat::VoiceMedia::EventKind::AudioError)audioFailure=true;
        }
        check(audioFailure,"bounded audio-scoped error");
        auto diagnostics=media.diagnostics();
        check(diagnostics.find(L"receive_failure_code=3")!=std::wstring::npos&&diagnostics.find(L"receive_media_generation=1")!=std::wstring::npos,"failure diagnostics and same generation");
        for(const auto& event:media.poll())check(event.kind!=peerchat::VoiceMedia::EventKind::AudioError,"no error spam");
        media.stop();std::cout<<"PASS: missing receiver DLL silences audio, retains generation/transport and emits one scoped error.\n";return 0;
    }
    int error=0;auto* encoder=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&error);
    check(encoder&&error==OPUS_OK,"encoder");
    std::array<std::int16_t,320> input{};for(unsigned i=0;i<320;++i)input[i]=std::int16_t(10000*std::sin(i*.1));
    std::array<unsigned char,4108> packet{};packet[0]=0x80;packet[1]=111;put32(packet.data()+8,1234);
    int size=opus_encode(encoder,input.data(),320,packet.data()+12,4096);check(size>0,"20 ms encode");
    opus_encoder_destroy(encoder);
    std::function<int(const unsigned char*,std::size_t)> previous;
    for(unsigned cycle=1;cycle<=100;++cycle){
        peerchat::VoiceMedia::ReceiveBinding binding{std::string(64,'a'),std::string(32,'b'),cycle,cycle};
        peerchat::VoiceMedia media(peerchat::MediaIceConfiguration::directForTests(),-1,-1,cycle,"",{},binding);
        std::wstring failure;check(media.startOffer(failure),"receive generation start");
        auto sink=media.testReceiveSink();
        if(previous)check(previous(packet.data(),size+12)==PC_RX_INVALID,"old callback rejected");
        packet[2]=0xff;packet[3]=0xff;put32(packet.data()+4,0xfffffc40u);
        check(sink(packet.data(),size+12)==PC_RX_OK,"first packet");
        packet[2]=packet[3]=0;put32(packet.data()+4,0); // sequence and 48 kHz timestamp wrap
        check(sink(packet.data(),size+12)==PC_RX_OK,"wrap packet");
        auto diagnostics=media.diagnostics();
        check(diagnostics.find(L"receive_active_sources=1")!=std::wstring::npos,"exactly one source");
        check(diagnostics.find(L"receive_media_generation="+std::to_wstring(cycle))!=std::wstring::npos,"generation diagnostics");
        std::array<std::int16_t,322> output{};output.front()=123;output.back()=456;
        media.setDeafened(true);check(media.testPullReceive(output.data()+1,320),"deafened paired pull");
        for(unsigned n=1;n<=320;++n)check(output[n]==0,"deafen silence");
        check(output.front()==123&&output.back()==456,"fixed buffer bounds");
        check(media.diagnostics().find(L"neteq_pulled_10ms_blocks=2")!=std::wstring::npos,"NetEq advances while deafened");
        media.setDeafened(false);media.setVolumes(.5f,.25f);
        check(media.diagnostics().find(L"output_volume=25")!=std::wstring::npos,"output volume preserved");
        std::atomic<bool> run{true};std::thread arrival([&]{while(run){sink(packet.data(),size+12);std::this_thread::yield();}});
        auto start=std::chrono::steady_clock::now();media.stop();run=false;arrival.join();
        check(std::chrono::steady_clock::now()-start<std::chrono::seconds(2),"bounded concurrent teardown");
        check(sink(packet.data(),size+12)==PC_RX_INVALID,"retired callback rejected");
        check(media.diagnostics().find(L"receive_state=closed")!=std::wstring::npos,"engine cleanup");
        previous=std::move(sink);
    }
    std::cout<<"PASS: 100 one-source call generations; wrap, paired pulls, deafen, volumes, concurrent RTP/stop, retained stale callbacks and bounded cleanup.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
