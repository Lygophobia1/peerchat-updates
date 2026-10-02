#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include "peerchat_audio_receive.h"
#include <opus/opus.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Api {
 HMODULE dll;
#define LOAD(name) decltype(&name) name=nullptr;
 LOAD(pc_rx_create) LOAD(pc_rx_destroy) LOAD(pc_rx_source_add) LOAD(pc_rx_source_remove) LOAD(pc_rx_source_reset) LOAD(pc_rx_insert) LOAD(pc_rx_pull_10ms) LOAD(pc_rx_get_source_stats) LOAD(pc_rx_get_mixer_stats)
#undef LOAD
 explicit Api(const char* path){dll=LoadLibraryExA(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(dll);
#define LOAD(name) {auto p=GetProcAddress(dll,#name);static_assert(sizeof(p)==sizeof(name));std::memcpy(&name,&p,sizeof(p));CHECK(name);}
 LOAD(pc_rx_create) LOAD(pc_rx_destroy) LOAD(pc_rx_source_add) LOAD(pc_rx_source_remove) LOAD(pc_rx_source_reset) LOAD(pc_rx_insert) LOAD(pc_rx_pull_10ms) LOAD(pc_rx_get_source_stats) LOAD(pc_rx_get_mixer_stats)
#undef LOAD
 }
 ~Api(){FreeLibrary(dll);}
};
struct Packet {std::vector<uint8_t> bytes;uint32_t timestamp;uint16_t seq;uint64_t arrival;};
std::vector<Packet> fixture(int count,uint32_t seed=0,bool fec=true,bool silence=false){
 int err=0;auto* enc=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&err);CHECK(enc&&err==0);
 CHECK(opus_encoder_ctl(enc,OPUS_SET_BITRATE(24000))==0);CHECK(opus_encoder_ctl(enc,OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE))==0);
 CHECK(opus_encoder_ctl(enc,OPUS_SET_INBAND_FEC(fec?1:0))==0);CHECK(opus_encoder_ctl(enc,OPUS_SET_PACKET_LOSS_PERC(30))==0);
 std::vector<Packet> out;
 for(int n=0;n<count;++n){std::array<int16_t,320> pcm{};for(int i=0;i<320;++i){double t=(n*320+i)/16000.;double envelope=.65+.3*std::sin(t*11);pcm[i]=static_cast<int16_t>(envelope*(10000*std::sin(t*2*3.141592653589793*220)+4000*std::sin(t*2*3.141592653589793*441)));}
  if(silence&&n>=count/2)pcm.fill(0);
  std::array<uint8_t,4096> encoded{};int len=opus_encode(enc,pcm.data(),320,encoded.data(),encoded.size());CHECK(len>0);
  out.push_back({std::vector<uint8_t>(encoded.begin(),encoded.begin()+len),seed+uint32_t(n*960),uint16_t(65500+n),uint64_t(n*20+40)});
 }
 opus_encoder_destroy(enc);return out;
}
pc_rx_config config(){return {sizeof(pc_rx_config),1,32,100,200};}
pc_rx_source_config source(int n,uint64_t generation=1){return {sizeof(pc_rx_source_config),1,uint32_t(123+n),111,generation};}
pc_rx_source_stats stats(Api& a,pc_rx_engine e,pc_rx_source s){pc_rx_source_stats v{};v.size=sizeof(v);v.version=1;CHECK(a.pc_rx_get_source_stats(e,s,&v)==0);return v;}
int insert(Api& a,pc_rx_engine e,pc_rx_source s,const pc_rx_source_config& c,const Packet& p,uint64_t now){pc_rx_packet h{sizeof(h),1,c.ssrc,p.timestamp,p.seq,111,c.endpoint_generation,now};return a.pc_rx_insert(e,s,&h,p.bytes.data(),p.bytes.size());}
struct Result {pc_rx_source_stats stats;uint64_t energy,hash;uint32_t fec_decode_rate;};
Result scenario(Api& a,const char* label,int mode,bool fec=true){
 auto packets=fixture(180,mode==5?0xffffe000u:0,fec,mode==11);
 size_t fecDrop=70;
 if(mode==2){for(size_t i=60;i<100;++i)if(opus_packet_has_lbrr(packets[i+1].bytes.data(),packets[i+1].bytes.size())==1){fecDrop=i;break;}}
 for(size_t i=0;i<packets.size();++i){
  if(mode==1&&i%10==3)packets[i].arrival+=40;
  if(mode==2&&i==fecDrop)packets[i].bytes.clear();
  if(mode==3&&i>=70&&i<=76)packets[i].bytes.clear();
  if(mode==4&&i==70)packets[i].arrival+=500;
  if(mode==6)packets[i].arrival+=(i%7)*10;
  if(mode==7)packets[i].arrival+=std::min<size_t>(i,15)*10;
  if(mode==8)packets[i].arrival+=(15-std::min<size_t>(i,15))*10;
  if(mode==9&&i>=70)packets[i].timestamp+=48000;
 }
 if(mode==10){auto p=packets[70];packets.push_back(p);}
 std::stable_sort(packets.begin(),packets.end(),[](const Packet& x,const Packet& y){return x.arrival<y.arrival;});
 auto cfg=config();pc_rx_engine e=0;CHECK(a.pc_rx_create(&cfg,&e)==0);auto sc=source(0);pc_rx_source s=0;CHECK(a.pc_rx_source_add(e,&sc,&s)==0);
 size_t next=0;uint64_t energy=0,hash=1469598103934665603ull;uint32_t fecRate=0;uint64_t clockStart=0,loudEnergy=0,silentEnergy=0;
 for(uint64_t t=0;t<4600;t+=10){while(next<packets.size()&&packets[next].arrival<=t){if(!packets[next].bytes.empty()){int r=insert(a,e,s,sc,packets[next],t);CHECK(r==0||r==PC_RX_FAILED);}++next;}
  std::array<int16_t,162> frame{};frame[0]=1234;frame[161]=4321;CHECK(a.pc_rx_pull_10ms(e,t,frame.data()+1,160)==0);CHECK(frame[0]==1234&&frame[161]==4321);
  for(int i=1;i<=160;++i){auto level=std::abs(int(frame[i]));energy+=level;if(t>=500&&t<900)loudEnergy+=level;if(t>=2600&&t<3000)silentEnergy+=level;hash=(hash^uint16_t(frame[i]))*1099511628211ull;}
  auto current=stats(a,e,s);fecRate=std::max(fecRate,current.secondary_decoded_rate_q14);
  if(mode==0&&t==2000)clockStart=current.playout_timestamp;
  if(mode==0&&t==2020)CHECK(uint32_t(current.playout_timestamp-clockStart)==960);
 }
 auto v=stats(a,e,s);CHECK(v.sample_rate==16000&&v.channels==1&&v.rtp_clock_rate==48000);CHECK(v.pulled_blocks==460&&v.pull_failures==0);CHECK(v.target_delay_ms<=200);CHECK(energy>1000000);CHECK(v.playout_timestamp_valid);
 std::printf("PASS %s: pulls=%llu concealed=%llu fec=%llu delay=%u/%u energy=%llu hash=%llu\n",label,(unsigned long long)v.pulled_blocks,(unsigned long long)v.concealed_samples,(unsigned long long)v.fec_packets_received,v.current_delay_ms,v.target_delay_ms,(unsigned long long)energy,(unsigned long long)hash);
 std::printf("FEC secondary decoded max Q14=%u\n",fecRate);
 if(mode==11)CHECK(silentEnergy<loudEnergy/10);
 CHECK(a.pc_rx_destroy(e)==0);CHECK(a.pc_rx_destroy(e)==PC_RX_INVALID);return {v,energy,hash,fecRate};
}
void lifecycle(Api& a){
 auto cfg=config();CHECK(a.pc_rx_create(nullptr,nullptr)==PC_RX_INVALID);cfg.max_sources=33;pc_rx_engine e=0;CHECK(a.pc_rx_create(&cfg,&e)==PC_RX_INVALID);cfg=config();auto packets=fixture(10);
 for(int cycle=0;cycle<100;++cycle){CHECK(a.pc_rx_create(&cfg,&e)==0);auto c=source(0);pc_rx_source s=0;CHECK(a.pc_rx_source_add(e,&c,&s)==0);auto first=s;
  CHECK(insert(a,e,s,c,packets[0],0)==0);std::array<int16_t,160> frame{};CHECK(a.pc_rx_pull_10ms(e,0,frame.data(),159)==PC_RX_INVALID);CHECK(a.pc_rx_pull_10ms(e,0,frame.data(),160)==0);CHECK(a.pc_rx_pull_10ms(e,0,frame.data(),160)==PC_RX_INVALID);
  c.endpoint_generation=2;CHECK(a.pc_rx_source_reset(e,s,&c)==0);auto old=c;old.endpoint_generation=1;CHECK(insert(a,e,s,old,packets[0],0)==PC_RX_INVALID);
  CHECK(a.pc_rx_source_remove(e,s)==0);CHECK(a.pc_rx_source_remove(e,s)==PC_RX_INVALID);CHECK(a.pc_rx_source_add(e,&c,&s)==0);CHECK(s!=first);CHECK(insert(a,e,first,c,packets[0],0)==PC_RX_INVALID);
  CHECK(a.pc_rx_destroy(e)==0);CHECK(a.pc_rx_pull_10ms(e,10,frame.data(),160)==PC_RX_INVALID);
 }
 std::printf("PASS 100 create/add/process/reset/remove/re-add/destroy lifetimes\n");
}
uint64_t memory(){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);CHECK(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)));return p.PrivateUsage;}
uint64_t cpu100ns(){FILETIME created,exited,kernel,user;CHECK(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user));ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;}
void boundariesAndConcurrency(Api& a){
 auto cfg=config();cfg.max_sources=2;pc_rx_engine e=0;CHECK(a.pc_rx_create(&cfg,&e)==0);auto c=source(0);pc_rx_source s=0,b=0;
 CHECK(a.pc_rx_source_add(e,&c,&s)==0);CHECK(a.pc_rx_source_add(e,&c,&b)==0);pc_rx_source extra=0;CHECK(a.pc_rx_source_add(e,&c,&extra)==PC_RX_LIMIT);
 auto packets=fixture(2);pc_rx_packet h{sizeof(h),1,c.ssrc,0,0,111,1,0};
 CHECK(a.pc_rx_insert(e,s,&h,packets[0].bytes.data(),4097)==PC_RX_INVALID);h.ssrc=999;CHECK(a.pc_rx_insert(e,s,&h,packets[0].bytes.data(),packets[0].bytes.size())==PC_RX_INVALID);
 h.ssrc=c.ssrc;h.sequence=65536;CHECK(a.pc_rx_insert(e,s,&h,packets[0].bytes.data(),packets[0].bytes.size())==PC_RX_INVALID);
 auto original=stats(a,e,b);c.endpoint_generation=2;CHECK(a.pc_rx_source_reset(e,s,&c)==0);CHECK(stats(a,e,b).endpoint_generation==original.endpoint_generation);
 CHECK(a.pc_rx_destroy(e)==0);
 for(int cycle=0;cycle<30;++cycle){CHECK(a.pc_rx_create(&cfg,&e)==0);c=source(0);CHECK(a.pc_rx_source_add(e,&c,&s)==0);std::atomic<bool> ready{false},go{false};
  std::thread worker([&]{ready=true;while(!go.load())std::this_thread::yield();for(uint64_t t=0;t<1000;t+=10){std::array<int16_t,160> frame{};int r=a.pc_rx_pull_10ms(e,t,frame.data(),160);CHECK(r==PC_RX_OK||r==PC_RX_BUSY||r==PC_RX_INVALID);}});
  while(!ready.load())std::this_thread::yield();
  go=true;CHECK(a.pc_rx_destroy(e)==0);worker.join();
 }
 std::puts("PASS: limits, malformed input, independent same-SSRC endpoints, reset isolation, 30 concurrent teardown lifetimes");
}
void mix(Api& a,int count){
 auto cfg=config();pc_rx_engine e=0;CHECK(a.pc_rx_create(&cfg,&e)==0);auto packets=fixture(1600);std::vector<pc_rx_source> handles(count);std::vector<pc_rx_source_config> configs;
 for(int i=0;i<count;++i){configs.push_back(source(i));CHECK(a.pc_rx_source_add(e,&configs.back(),&handles[i])==0);}
 double total=0,worst=0;uint64_t before=0,after=0,energy=0,cpuStart=cpu100ns();int pulls=0;
 for(uint64_t t=0;t<30000;t+=10){if(t%20==0){auto& packet=packets[t/20];for(int i=0;i<count;++i){if(i==0&&t>=12000&&t<12400)continue;CHECK(insert(a,e,handles[i],configs[i],packet,t)==0);}}
  auto start=std::chrono::steady_clock::now();std::array<int16_t,160> out{};CHECK(a.pc_rx_pull_10ms(e,t,out.data(),160)==0);auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();total+=us;worst=std::max(worst,us);++pulls;for(auto x:out)energy+=std::abs(int(x));
  if(t==5000)before=memory();
  if(t==29990)after=memory();
 }
 CHECK(energy>1000000);for(int i=0;i<count;++i){auto v=stats(a,e,handles[i]);CHECK(v.pulled_blocks==3000&&v.pull_failures==0);if(i>0)CHECK(v.packets_inserted==1500);}
 if(count>1){auto beforeB=stats(a,e,handles[1]);configs[0].endpoint_generation=2;CHECK(a.pc_rx_source_reset(e,handles[0],&configs[0])==0);std::array<int16_t,160> out{};CHECK(a.pc_rx_pull_10ms(e,30000,out.data(),160)==0);CHECK(stats(a,e,handles[1]).pulled_blocks==beforeB.pulled_blocks+1);CHECK(a.pc_rx_source_remove(e,handles[0])==0);CHECK(a.pc_rx_pull_10ms(e,30010,out.data(),160)==0);CHECK(stats(a,e,handles[1]).pulled_blocks==beforeB.pulled_blocks+2);}
 std::printf("MEASURE sources=%d simulated_s=30 average_pull_us=%.2f worst_pull_us=%.2f cpu_ms=%.2f private_bytes_warm=%llu end=%llu\n",count,total/pulls,worst,(cpu100ns()-cpuStart)/10000.,(unsigned long long)before,(unsigned long long)after);CHECK(after<=before+32*1024*1024);CHECK(a.pc_rx_destroy(e)==0);
}
void devicePaced(Api& a){
 auto cfg=config();cfg.version=PC_RX_DEVICE_PACED;pc_rx_engine e=0;CHECK(a.pc_rx_create(&cfg,&e)==PC_RX_OK);
 auto packets=fixture(40);auto c=source(0);pc_rx_source s=0;CHECK(a.pc_rx_source_add(e,&c,&s)==PC_RX_OK);
 uint64_t energy=0;
 // A completed 20 ms device buffer pulls two blocks together. Arrival can occur
 // between callbacks; it must never invalidate the next playout block's clock.
 for(size_t i=0;i<packets.size();++i){
  CHECK(insert(a,e,s,c,packets[i],UINT64_MAX-i)==PC_RX_OK);
  std::array<int16_t,322> pcm{};pcm.front()=1234;pcm.back()=4321;
  CHECK(a.pc_rx_pull_10ms(e,0,pcm.data()+1,160)==PC_RX_OK);
  CHECK(a.pc_rx_pull_10ms(e,0,pcm.data()+161,160)==PC_RX_OK);
  CHECK(pcm.front()==1234&&pcm.back()==4321);for(size_t n=1;n<=320;++n)energy+=std::abs(int(pcm[n]));
  std::this_thread::sleep_for(std::chrono::milliseconds(i%2?18:22));
 }
 CHECK(energy>100000);auto v=stats(a,e,s);CHECK(v.pulled_blocks==80&&v.pull_failures==0);
 pc_rx_mixer_stats m{};m.size=sizeof(m);m.version=1;CHECK(a.pc_rx_get_mixer_stats(e,&m)==PC_RX_OK);CHECK(m.mixed_blocks==80&&m.clock_ms>0);
 CHECK(a.pc_rx_destroy(e)==PC_RX_OK);
 std::puts("PASS: real-clock receive, irregular device-paced 20 ms pairs, independent packet arrival, fixed buffers");
}
int main(int argc,char** argv){CHECK(argc==2);Api a(argv[1]);lifecycle(a);boundariesAndConcurrency(a);devicePaced(a);auto clean=scenario(a,"in-order / seq wrap / 48k RTP to 16k PCM",0);auto repeat=scenario(a,"repeat deterministic",0);CHECK(clean.hash==repeat.hash);
 scenario(a,"moderate reorder",1);auto fec=scenario(a,"single loss/FEC",2);CHECK(fec.fec_decode_rate>0);auto plc=scenario(a,"burst loss/PLC",3,false);CHECK(plc.stats.concealed_samples>clean.stats.concealed_samples);scenario(a,"late packet",4);scenario(a,"timestamp wrap",5);scenario(a,"jitter variation",6);scenario(a,"increasing delay",7);scenario(a,"decreasing delay",8);scenario(a,"timestamp discontinuity",9);auto duplicate=scenario(a,"duplicate",10);CHECK(duplicate.hash==clean.hash);scenario(a,"source becomes silent",11);
 for(int n:{1,2,3,4,8})mix(a,n);
 std::puts("PASS: standalone receive/mix proof tests completed");}
