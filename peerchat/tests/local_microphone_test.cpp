#define main preserved_audio_lifecycle_main
#include "audio_lifecycle_test.cpp"
#undef main
#define PEERCHAT_TEST_MICROPHONE
#include "local_microphone_test.hpp"
int main(){try{
    peerchat::LocalMicrophoneTest test;require(!test.state().available,"Test auto-retained a recording");
    require(test.start(0,0,100,false,true,true,1,1),"Local raw start");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(test.state().frames<4&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    test.stopRecording();require(test.state().frames==4&&!test.state().recording,"Explicit stop");
    require(test.listen(1),"Raw listen");std::array<std::int16_t,320> raw{};require(test.testPull(raw.data())&&raw[0]==16384,"Raw bytes changed");
    require(test.listen(2),"Bypassed comparison listen");std::array<std::int16_t,320> processed{};require(test.testPull(processed.data())&&processed==raw,"Bypass changed PCM");
    test.clear();require(!test.state().available&&test.state().frames==0&&fake::inputs==0&&fake::outputs==0,"Clear retained samples/device handles");
    require(test.start(0,0,100,true,true,true,2,0),"Existing APM local start");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(test.state().frames<4&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::vector<unsigned char> frame(640);for(std::size_t i=0;i<640;i+=2){frame[i]=0x34;frame[i+1]=0x12;}
    for(unsigned i=0;i<450;++i)test.testCapture(frame);
    auto state=test.state();require(state.frames==400&&!state.recording&&state.processing,"Eight-second buffer/autostop bound");
    require(state.rawRms>0&&state.processedRms>=0,"Actual PCM measurements missing");
    test.clear();require(!test.listen(1)&&!test.state().available,"Stale samples after clear");
    require(!test.start(0,0,201,false,false,false,0,0),"Invalid volume accepted");
    std::cout<<"PASS: real Audio worker + existing APM, fake devices; explicit record/stop/raw/processed, exact bypass, bounded 400 frames/autostop, clearing and handle cleanup. No files/network or real microphone.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
