#include "peerchat_audio_receive.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include "api/audio/audio_frame.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/environment/environment_factory.h"
#include "api/make_ref_counted.h"
#include "api/neteq/default_neteq_factory.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "system_wrappers/include/clock.h"
namespace {
class MonoOpusFactory : public webrtc::AudioDecoderFactory {
 public:
 std::vector<webrtc::AudioCodecSpec> GetSupportedDecoders() override {std::vector<webrtc::AudioCodecSpec> s;webrtc::AudioDecoderOpus::AppendSupportedDecoders(&s);return s;}
 bool IsSupportedDecoder(const webrtc::SdpAudioFormat& f) override {return webrtc::AudioDecoderOpus::SdpToConfig(f).has_value();}
 std::unique_ptr<webrtc::AudioDecoder> Create(const webrtc::Environment& env,const webrtc::SdpAudioFormat& f) override {
  auto c=webrtc::AudioDecoderOpus::SdpToConfig(f);if(!c)return nullptr;c->sample_rate_hz=16000;c->num_channels=1;return webrtc::AudioDecoderOpus::MakeAudioDecoder(env,*c);
 }
};
struct Source final : webrtc::AudioMixer::Source {
 pc_rx_source_config config;std::unique_ptr<webrtc::NetEq> neteq;uint64_t inserted=0,rejected=0,pulls=0,failures=0;bool muted=false;uint32_t secondaryRate=0;
 Source(const pc_rx_source_config& c,const pc_rx_config& limits,const webrtc::Environment& env):config(c){
  webrtc::NetEq::Config n;n.sample_rate_hz=16000;n.max_packets_in_buffer=limits.max_packets;n.max_delay_ms=limits.max_delay_ms;n.enable_muted_state=true;
  neteq=webrtc::DefaultNetEqFactory().Create(env,n,webrtc::make_ref_counted<MonoOpusFactory>());
  if(!neteq||!neteq->RegisterPayloadType(c.payload_type,webrtc::SdpAudioFormat("opus",48000,2,{{"stereo","0"},{"useinbandfec","1"}})))throw 1;
  if(!neteq->CreateDecoder(c.payload_type))throw 1;
 }
 AudioFrameInfo GetAudioFrameWithInfo(int rate,webrtc::AudioFrame* out) override {
  if(rate!=16000||neteq->GetAudio(out,&muted)!=0||out->sample_rate_hz_!=16000||out->num_channels_!=1||out->samples_per_channel_!=160){++failures;return AudioFrameInfo::kError;}
  ++pulls;
  // CurrentNetworkStatistics only populates delay fields at this revision.
  // Collect rate statistics once per 100 ms, not on each observer read.
  if(pulls%10==0){webrtc::NetEqNetworkStatistics n{};neteq->NetworkStatistics(&n);secondaryRate=n.secondary_decoded_rate;}
  return muted?AudioFrameInfo::kMuted:AudioFrameInfo::kNormal;
 }
 int Ssrc() const override {return static_cast<int>(config.ssrc);}
 int PreferredSampleRate() const override {return 16000;}
};
struct Engine {
 std::mutex mutex;bool retired=false;pc_rx_config config;webrtc::SimulatedClock clock{0};webrtc::Clock* active_clock;webrtc::Environment env;
 webrtc::scoped_refptr<webrtc::AudioMixerImpl> mixer=webrtc::AudioMixerImpl::Create();std::map<uint64_t,std::unique_ptr<Source>> sources;
 uint64_t time=0,blocks=0,last_pull=0;bool pulled=false;
 explicit Engine(const pc_rx_config& c):config(c),active_clock(c.version==PC_RX_DEVICE_PACED?webrtc::Clock::GetRealTimeClock():&clock),env(webrtc::CreateEnvironment(active_clock)){}
 ~Engine(){for(auto& s:sources)mixer->RemoveSource(s.second.get());}
 bool advance(uint64_t t){if(config.version==PC_RX_DEVICE_PACED){time=active_clock->TimeInMilliseconds();return true;}if(t<time||t>UINT64_C(86400000000))return false;clock.AdvanceTimeMilliseconds(t-time);time=t;return true;}
};
std::mutex registry_mutex;std::map<uint64_t,std::shared_ptr<Engine>> registry;uint64_t next_engine=1;
std::atomic<uint64_t> next_source{1};
template<class F> int access(pc_rx_engine id,F fn) noexcept {
 try{std::shared_ptr<Engine> e;{std::lock_guard<std::mutex> l(registry_mutex);auto it=registry.find(id);if(it==registry.end())return PC_RX_INVALID;e=it->second;}
  std::unique_lock<std::mutex> l(e->mutex,std::try_to_lock);if(!l.owns_lock())return PC_RX_BUSY;if(e->retired)return PC_RX_INVALID;return fn(*e);
 }catch(...){return PC_RX_FAILED;}
}
bool valid(const pc_rx_source_config* c){return c&&c->size==sizeof(*c)&&c->version==1&&c->ssrc&&c->payload_type<=127&&c->endpoint_generation;}
}
extern "C" PC_RX_API int pc_rx_create(const pc_rx_config* c,pc_rx_engine* out){
 if(!c||!out||c->size!=sizeof(*c)||(c->version!=PC_RX_DETERMINISTIC&&c->version!=PC_RX_DEVICE_PACED)||!c->max_sources||c->max_sources>32||!c->max_packets||c->max_packets>200||c->max_delay_ms<20||c->max_delay_ms>500)return PC_RX_INVALID;
 *out=0;try{auto e=std::make_shared<Engine>(*c);std::lock_guard<std::mutex> l(registry_mutex);if(registry.size()>=64)return PC_RX_LIMIT;auto id=next_engine++;registry.emplace(id,e);*out=id;return PC_RX_OK;}catch(...){return PC_RX_FAILED;}
}
extern "C" PC_RX_API int pc_rx_destroy(pc_rx_engine id){
 try{std::shared_ptr<Engine> e;{std::lock_guard<std::mutex> l(registry_mutex);auto it=registry.find(id);if(it==registry.end())return PC_RX_INVALID;e=it->second;registry.erase(it);}
  std::lock_guard<std::mutex> l(e->mutex);e->retired=true;return PC_RX_OK;
 }catch(...){return PC_RX_FAILED;}
}
extern "C" PC_RX_API int pc_rx_source_add(pc_rx_engine id,const pc_rx_source_config* c,pc_rx_source* out){
 if(!valid(c)||!out)return PC_RX_INVALID;*out=0;return access(id,[&](Engine& e)->int{if(e.sources.size()>=e.config.max_sources)return PC_RX_LIMIT;
 // Identical SSRCs on independent authenticated endpoints are not account identity collisions.
 auto source=std::make_unique<Source>(*c,e.config,e.env);auto token=next_source.fetch_add(1);auto* p=source.get();e.sources.emplace(token,std::move(source));
 if(!e.mixer->AddSource(p)){e.sources.erase(token);return PC_RX_FAILED;}*out=token;return PC_RX_OK;});
}
extern "C" PC_RX_API int pc_rx_source_remove(pc_rx_engine id,pc_rx_source s){return access(id,[&](Engine& e)->int{auto it=e.sources.find(s);if(it==e.sources.end())return PC_RX_INVALID;e.mixer->RemoveSource(it->second.get());e.sources.erase(it);return PC_RX_OK;});}
extern "C" PC_RX_API int pc_rx_source_reset(pc_rx_engine id,pc_rx_source s,const pc_rx_source_config* c){
 if(!valid(c))return PC_RX_INVALID;return access(id,[&](Engine& e)->int{auto it=e.sources.find(s);if(it==e.sources.end()||c->endpoint_generation<=it->second->config.endpoint_generation)return PC_RX_INVALID;
 auto fresh=std::make_unique<Source>(*c,e.config,e.env);e.mixer->RemoveSource(it->second.get());it->second=std::move(fresh);return e.mixer->AddSource(it->second.get())?PC_RX_OK:PC_RX_FAILED;});
}
extern "C" PC_RX_API int pc_rx_insert(pc_rx_engine id,pc_rx_source s,const pc_rx_packet* p,const uint8_t* bytes,uint32_t count){
 if(!p||p->size!=sizeof(*p)||p->version!=1||!bytes||!count||count>4096||p->sequence>65535)return PC_RX_INVALID;
 return access(id,[&](Engine& e)->int{auto it=e.sources.find(s);if(it==e.sources.end())return PC_RX_INVALID;auto& a=*it->second;
 if(p->ssrc!=a.config.ssrc||p->payload_type!=a.config.payload_type||p->endpoint_generation!=a.config.endpoint_generation||!e.advance(p->arrival_ms))return PC_RX_INVALID;
 webrtc::RTPHeader h;h.ssrc=p->ssrc;h.timestamp=p->timestamp;h.sequenceNumber=p->sequence;h.payloadType=p->payload_type;
 if(a.neteq->InsertPacket(h,std::span<const uint8_t>(bytes,count),webrtc::Timestamp::Millis(e.time))!=0){++a.rejected;return PC_RX_FAILED;}++a.inserted;return PC_RX_OK;});
}
extern "C" PC_RX_API int pc_rx_pull_10ms(pc_rx_engine id,uint64_t t,int16_t* out,uint32_t capacity){
 if(!out||capacity!=160)return PC_RX_INVALID;return access(id,[&](Engine& e)->int{if((e.config.version==PC_RX_DETERMINISTIC&&e.pulled&&t!=e.last_pull+10)||!e.advance(t))return PC_RX_INVALID;
 // Empty upstream mixer defaults to 48 kHz; this fixed-format adapter emits silence.
 if(e.sources.empty())std::fill_n(out,160,0);else{webrtc::AudioFrame f;e.mixer->Mix(1,&f);if(f.sample_rate_hz_!=16000||f.num_channels_!=1||f.samples_per_channel_!=160)return PC_RX_FAILED;std::copy_n(f.data(),160,out);}
 e.last_pull=t;e.pulled=true;++e.blocks;return PC_RX_OK;});
}
extern "C" PC_RX_API int pc_rx_get_source_stats(pc_rx_engine id,pc_rx_source s,pc_rx_source_stats* out){
 if(!out||out->size!=sizeof(*out)||out->version!=1)return PC_RX_INVALID;return access(id,[&](Engine& e)->int{auto it=e.sources.find(s);if(it==e.sources.end())return PC_RX_INVALID;auto& a=*it->second;auto v=a.neteq->GetLifetimeStatistics();auto op=a.neteq->GetOperationsAndState();auto stamp=a.neteq->GetPlayoutTimestamp();
 *out={};out->size=sizeof(*out);out->version=1;out->sample_rate=a.neteq->last_output_sample_rate_hz();out->channels=1;out->rtp_clock_rate=48000;out->current_delay_ms=a.neteq->FilteredCurrentDelayMs();out->target_delay_ms=a.neteq->TargetDelayMs();out->endpoint_generation=a.config.endpoint_generation;
 out->packets_inserted=a.inserted;out->insert_failures=a.rejected;out->pulled_blocks=a.pulls;out->pull_failures=a.failures;out->concealed_samples=v.concealed_samples;out->concealment_events=v.concealment_events;out->fec_packets_received=v.fec_packets_received;out->fec_packets_discarded=v.fec_packets_discarded;out->packets_discarded=v.packets_discarded;out->accelerated_samples=v.removed_samples_for_acceleration;out->decelerated_samples=v.inserted_samples_for_deceleration;out->buffer_flushes=op.packet_buffer_flushes;out->playout_timestamp_valid=stamp.has_value();out->playout_timestamp=stamp.value_or(0);out->last_frame_muted=a.muted;out->secondary_decoded_rate_q14=a.secondaryRate;return PC_RX_OK;});
}
extern "C" PC_RX_API int pc_rx_get_mixer_stats(pc_rx_engine id,pc_rx_mixer_stats* out){if(!out||out->size!=sizeof(*out)||out->version!=1)return PC_RX_INVALID;return access(id,[&](Engine& e)->int{*out={};out->size=sizeof(*out);out->version=1;out->active_sources=e.sources.size();out->sample_rate=16000;out->channels=1;out->mixed_blocks=e.blocks;out->clock_ms=e.time;return PC_RX_OK;});}
