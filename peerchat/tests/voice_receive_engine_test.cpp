#define NOMINMAX
#include "../voice_receive_engine.hpp"
#include "../local_voice_source.hpp"
#include <opus/opus.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#define CHECK(value) do{if(!(value)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value);std::exit(1);}}while(0)
void put32(unsigned char* b,std::uint32_t n){b[0]=n>>24;b[1]=n>>16;b[2]=n>>8;b[3]=n;}
int main(int argc,char** argv){
 CHECK(argc==2);std::wstring dll;for(const unsigned char* p=reinterpret_cast<const unsigned char*>(argv[1]);*p;++p)dll.push_back(*p);
 peerchat::VoiceReceiveEngine receive;CHECK(!receive.open(L"relative.dll"));CHECK(receive.open(dll));
 CHECK(receive.authorize("account:1/device-a",1));CHECK(receive.authorize("account:2/device-b",1));
 peerchat::LocalVoiceSource source;peerchat::VoiceMedia::ProcessingOptions processing;
 CHECK(source.open(processing));CHECK(source.processing().active());
 std::uint64_t energy=0;
 for(unsigned frame=0;frame<60;++frame){
  std::array<std::int16_t,320> input{};for(unsigned n=0;n<320;++n)input[n]=std::int16_t(12000*std::sin((frame*320+n)*.085));
  std::array<unsigned char,4108> rtp{};rtp[0]=0x80;rtp[1]=111;rtp[2]=frame>>8;rtp[3]=frame;put32(rtp.data()+4,frame*960);put32(rtp.data()+8,1234);
  peerchat::LocalVoiceSource::Packet encoded;
  CHECK(source.process20ms(reinterpret_cast<const unsigned char*>(input.data()),640,encoded));
  CHECK(encoded.timestamp==frame*960);int count=encoded.size;CHECK(count>0);std::copy_n(encoded.bytes.data(),count,rtp.data()+12);
  CHECK(receive.insertRtp("account:1/device-a",1,rtp.data(),count+12)==PC_RX_OK);
  // Same SSRC is safe: authenticated endpoint identity is distinct from SSRC.
  if(frame<30)CHECK(receive.insertRtp("account:2/device-b",1,rtp.data(),count+12)==PC_RX_OK);
  if(frame==30){receive.retire("account:2/device-b");CHECK(receive.insertRtp("account:2/device-b",1,rtp.data(),count+12)==PC_RX_INVALID);}
  std::array<std::int16_t,322> output{};output.front()=1122;output.back()=2211;
  CHECK(receive.pull20ms(output.data()+1,320,frame>=40));CHECK(output.front()==1122&&output.back()==2211);
  for(unsigned n=1;n<=320;++n){if(frame>=40)CHECK(output[n]==0);else energy+=std::abs(int(output[n]));}
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
 }
 CHECK(source.encodedPackets()==60&&source.captureFrames()==60&&source.processing().captureCalls()==120);
 source.setMuted(true);peerchat::LocalVoiceSource::Packet mutedPacket;std::array<unsigned char,640> mutedPcm{};
 CHECK(!source.process20ms(mutedPcm.data(),640,mutedPacket)&&mutedPacket.size==0&&source.encodedPackets()==60);
 source.setMuted(false);CHECK(!source.process20ms(mutedPcm.data(),639,mutedPacket));
 auto state=receive.snapshot();CHECK(energy>100000&&state.authorizedSources==1&&state.activeSources==1&&state.mixedBlocks==120&&state.underrunBlocks==0);
 CHECK(state.sources.at("account:1/device-a").pulled_blocks==120);
 std::array<unsigned char,12> malformed{};CHECK(receive.insertRtp("account:1/device-a",1,malformed.data(),12)==PC_RX_INVALID);
 malformed[1]=111;put32(malformed.data()+8,1234);
 for(unsigned char flags:{0x90,0xa0,0x8f}){malformed[0]=flags;CHECK(receive.insertRtp("account:1/device-a",1,malformed.data(),12)==PC_RX_INVALID);}
 CHECK(receive.authorize("account:1/device-a",2));CHECK(!receive.authorize("account:1/device-a",1));
 CHECK(receive.insertRtp("account:1/device-a",1,malformed.data(),12)==PC_RX_INVALID);
 CHECK(!receive.pull20ms(nullptr,320,false));std::array<std::int16_t,320> output{};CHECK(!receive.pull20ms(output.data(),319,false));
 for(unsigned cycle=0;cycle<50;++cycle){receive.close();CHECK(receive.open(dll));CHECK(receive.pull20ms(output.data(),320,false));for(auto sample:output)CHECK(sample==0);}
 receive.close();CHECK(!receive.pull20ms(output.data(),320,false));source.close();
 std::puts("PASS: reusable receiver, authenticated source/generation checks, same-SSRC isolation, retire-one/preserve-other, 20 ms paired pulls, deafen advances silently, bounds and 50 lifetimes");
}
