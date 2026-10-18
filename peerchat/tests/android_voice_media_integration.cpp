// HOST integration of the unchanged shared Android controller and real media.
// Real Windows Storage/Schannel, synthetic device port; NOT Android/JNI/OS proof.
#define _WIN32_WINNT 0x0602
#include "portable/android_voice_application.hpp"
#include "android_voice_media_integration_audio.hpp"
#include "reliable_transfer_interop_fixture.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace peerchat;
namespace device=voice_integration_audio;
using Clock=std::chrono::steady_clock;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
struct Side {
    Storage store;Profile profile;delivery::DeliveryManager deliveries;
    session::SessionTransportManager sessions;reliable::AuthenticatedFriendSnapshot friends;
    transfer::TransferManager transfers{[]{return std::make_unique<transfer::windows::Sha256>();}};
    std::shared_ptr<transfer_interop::Received> received=std::make_shared<transfer_interop::Received>();
    calls::CallManager calls;std::map<std::uint64_t,std::unique_ptr<portable::ReliableEndpoint>> peers;
    std::uint64_t network=1,transport=0;portable::AndroidVoiceApplication voice;
    Blob candidateReplay,endReplay;unsigned pins=0,messages=0;std::string remote;
    std::uint64_t transferFrames=0,maxTransferFrame=0;
    explicit Side(const std::wstring& path):store(path),profile(store.profile()),deliveries(store,profile.peerId),
        sessions(profile.peerId,profile.peerId,{1}),voice(calls,store,profile,friends,sessions,peers,[this]{return network;}){
        transfers.defaults({transfer::ExtendedChunkBytes,16,10000000},10000000);
    }
    ~Side(){voice.invalidate();peers.clear();}
    portable::ReliableEndpoint& endpoint(){return *peers.at(transport);}
    void authorize(const Side& other,std::uint64_t localAccount,std::uint64_t remoteAccount){remote=other.profile.peerId;friends.replace(localAccount,1,{{remoteAccount,remote,{remote}}});}
    void open(bool incoming){
        const auto id=++transport;
        session::RouteObservation route{{id},"relay:voice-integration","isolated authenticated friend input","opaque TLS test carriage",incoming,network,session::TransportKind::Relay,session::RouteClass::ServerRelay};
        auto credentials=std::make_unique<TlsCredentials>(profile);
        // TlsSession owns its platform session; credentials must remain live.
        tlsCredentials.push_back(std::move(credentials));
        auto endpoint=std::make_unique<portable::ReliableEndpoint>(profile,store,sessions,deliveries,friends,session::SessionEpoch{1},session::TransportInstanceId{id},
            std::make_unique<TlsSession>(*tlsCredentials.back(),incoming),remote,route,now(),std::nullopt,
            portable::ReliableEndpoint::TransferReceiver{[this](const std::string& peer,const Blob& frame,std::uint64_t time){
                ++transferFrames;maxTransferFrame=std::max<std::uint64_t>(maxTransferFrame,frame.size());
                transfers.capability(peer,transport,this->endpoint().permitsTransferPipeline());transfers.receive(peer,frame,time);
            }},std::nullopt,
            portable::ReliableEndpoint::VoiceAdmission{[]{return androidVoiceAvailable();},[this](const std::string& peer,const Blob& bytes,std::uint64_t time){
                protocol::Reader r(bytes);if(r.type()==protocol::CallCandidate&&candidateReplay.empty())candidateReplay=bytes;
                if(r.type()==protocol::CallEnd)endReplay=bytes;
                device::OwnerScope owner(profile.peerId);voice.app.receive(peer,bytes,time);
            }});
        peers.emplace(id,std::move(endpoint));this->endpoint().receive({},now());
    }
    std::vector<std::unique_ptr<TlsCredentials>> tlsCredentials;
    void drain(){while(auto event=endpoint().poll()){
        if(event->kind==portable::ReliableEndpoint::EventKind::TrustRequired){
            reliable::InitialPinDecision decision{};check(endpoint().initialPin(friends.generation(),now(),&decision)&&decision==reliable::InitialPinDecision::Allowed,"Real authenticated initial pin failed");++pins;
        }else if(event->kind==portable::ReliableEndpoint::EventKind::Message)++messages;
    }}
    void tick(){
        device::OwnerScope owner(profile.peerId);const auto time=now();voice.tick(time);endpoint().tick(time);
        transfers.mediaActive(calls.active()&&calls.active()->mediaState==calls::MediaState::Connected);
        transfers.capability(remote,transport,endpoint().permitsTransferPipeline());
        for(const auto& row:transfers.snapshots())if(row.state==transfer::State::AwaitingAcceptance)
            transfers.accept(row.metadata.id,std::make_unique<transfer_interop::Sink>(received),time);
        transfers.tick(time,[this](const std::string& peer){return peer==remote&&endpoint().permitsTransferPipeline()?transport:0;},
            [this](const std::string& peer,const Blob& frame){return peer==remote&&endpoint().sendTransfer(frame);});
        endpoint().pump();
    }
    bool start(const std::string& id){device::OwnerScope owner(profile.peerId);return voice.app.start(remote,id,now());}
    bool answer(const std::string& id){device::OwnerScope owner(profile.peerId);return voice.app.answer(id,now());}
    void receive(const Blob& bytes){device::OwnerScope owner(profile.peerId);endpoint().receive(bytes,now());}
    void ice(){check(voice.installIce(MediaIceConfiguration::directForTests()),"Test-only direct ICE configuration rejected");}
};
void exchange(Side& a,Side& b){
    for(unsigned round=0;round<100;++round){a.drain();b.drain();auto left=a.endpoint().takeOutput(),right=b.endpoint().takeOutput();if(left.empty()&&right.empty())break;
        for(std::size_t offset=0;offset<left.size();offset+=113)b.receive(Blob(left.begin()+offset,left.begin()+std::min(left.size(),offset+113)));
        for(std::size_t offset=0;offset<right.size();offset+=113)a.receive(Blob(right.begin()+offset,right.begin()+std::min(right.size(),offset+113)));
    }a.drain();b.drain();
}
void step(Side& a,Side& b){exchange(a,b);a.tick();b.tick();exchange(a,b);device::step();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
template<class Predicate> void wait(Side& a,Side& b,Predicate predicate,const char* why,unsigned milliseconds=12000){
    const auto deadline=Clock::now()+std::chrono::milliseconds(milliseconds);
    do{step(a,b);if(predicate())return;}while(Clock::now()<deadline);
    std::cerr<<"Timeout controller A="<<a.voice.app.diagnostic()<<" B="<<b.voice.app.diagnostic()<<'\n';throw std::runtime_error(why);
}
void run(Side& a,Side& b,unsigned blocks){for(unsigned i=0;i<blocks;++i)step(a,b);}
bool connected(const Side& side){auto active=side.calls.active();return active&&active->state==calls::State::Connected&&active->mediaState==calls::MediaState::Connected;}
device::Snapshot activeDevice(const Side& side){auto all=device::snapshots();for(auto i=all.rbegin();i!=all.rend();++i)if(i->active&&i->owner==side.profile.peerId)return *i;return {};}
std::uint64_t captures(){std::uint64_t count=0;for(const auto& value:device::snapshots())count+=value.captures;return count;}
unsigned activeDevices(){unsigned count=0;for(const auto& value:device::snapshots())if(value.active)++count;return count;}
transfer::Digest digest(const Blob& bytes){
    transfer::windows::Sha256 hash;
    for(std::size_t at=0;at<bytes.size();at+=transfer::ChunkBytes)
        hash.update(bytes.data()+at,std::min<std::size_t>(transfer::ChunkBytes,bytes.size()-at));
    return hash.finish();
}
void measured(Side& a,Side& b,const char* label){
    wait(a,b,[&]{auto x=activeDevice(a),y=activeDevice(b);return x.captures>=20&&y.captures>=20&&x.supplied>=20&&y.supplied>=20&&x.energy>100000&&y.energy>100000;},"Bidirectional genuine decoded PCM did not arrive",6000);
    auto x=activeDevice(a),y=activeDevice(b);
    const auto remoteTone=[](const device::Snapshot& receiver,const device::Snapshot& sender){
        return sender.id%2?receiver.tone330>receiver.tone660*2:receiver.tone660>receiver.tone330*2;
    };
    check(remoteTone(x,y)&&remoteTone(y,x),"Decoded PCM did not match each remote endpoint's distinct synthetic tone");
    std::cout<<"MEASURE "<<label<<" A_capture="<<x.captures<<" A_device_pulls="<<x.pulls<<" A_paired_neteq_pulls="<<x.supplied<<" A_decoded_energy="<<x.energy
        <<" B_capture="<<y.captures<<" B_device_pulls="<<y.pulls<<" B_paired_neteq_pulls="<<y.supplied<<" B_decoded_energy="<<y.energy<<" remote_tone_attribution=passed"<<'\n';
}
}
int main(int argc,char** argv){try{
    check(argc==2,"Pass an isolated profile directory");check(androidVoiceAvailable(),"Genuine media fixture capability not enabled");
    const auto root=std::filesystem::path(argv[1]);Side a((root/"a").wstring()),b((root/"b").wstring());
    const auto first=Storage::newMessageId();check(!a.start(first),"Unadmitted friend started media");check(device::snapshots().empty(),"Authorization rejection created an audio device");
    a.authorize(b,10,20);b.authorize(a,20,10);a.open(false);b.open(true);
    check(!a.start(first),"Call started before authenticated winner");
    wait(a,b,[&]{return a.endpoint().ready()&&b.endpoint().ready();},"Real TLS/Hello/pin/winner handshake failed");
    check(a.pins==1&&b.pins==1,"Both real peer presentations were not initially pinned");
    check((a.endpoint().voiceCapabilities()&protocol::CapabilityVoiceCalls)&&(b.endpoint().voiceCapabilities()&protocol::CapabilityVoiceCalls),"Voice capability was not mutually negotiated");
    a.ice();b.ice();check(a.start(first),"Outgoing controller command failed");
    wait(a,b,[&]{return b.calls.state()==calls::State::Incoming;},"Authenticated voice offer did not ring");run(a,b,12);
    check(device::snapshots().size()==1&&captures()==0&&activeDevices()==0,"Unanswered incoming call created media/captured PCM");
    check(b.answer(first),"Explicit Answer failed");wait(a,b,[&]{return connected(a)&&connected(b);},"Genuine ICE/DTLS-SRTP did not connect");measured(a,b,"first_call");
    const auto lifetime=a.calls.lifetime();check(a.calls.active()->id.value==first&&b.calls.active()->id.value==first,"Peer call identity mismatch");
    StoredMessage text{Storage::newMessageId(),a.profile.peerId,b.profile.peerId,L"Synthetic text during genuine voice",42,Delivery::Queued,0,a.store.nextLogicalOrder()};
    check(a.store.append(b.profile.peerId,text),"Concurrent text append failed");a.endpoint().pump();
    wait(a,b,[&]{return b.messages==1&&a.store.pending(b.profile.peerId).empty();},"Text/ACK stalled during voice");
    check(a.endpoint().permitsTransferPipeline()&&b.endpoint().permitsTransferPipeline(),"Authenticated transfer pipeline not negotiated");
    const auto file=transfer_interop::content(false);const auto fileHash=digest(file);
    const auto source=(root/"a"/"active-voice-transfer.bin");
    {std::ofstream output(source,std::ios::binary);check(bool(output),"Isolated transfer source open failed");
        output.write(reinterpret_cast<const char*>(file.data()),static_cast<std::streamsize>(file.size()));check(bool(output),"Isolated transfer source write failed");}
    auto beforeFileA=activeDevice(a),beforeFileB=activeDevice(b);const auto transferId=Storage::newMessageId();
    a.transfers.offer(b.profile.peerId,transferId,std::make_unique<transfer::windows::FileSource>(source.wstring()),now());
    wait(a,b,[&]{return a.transfers.completedRecords().size()==1&&b.transfers.completedRecords().size()==1&&b.received->committed;},
        "Real v2 file/ACK transfer stalled during voice",90000);
    const auto sent=a.transfers.completedRecords().front(),arrived=b.transfers.completedRecords().front();
    check(sent.metadata.id==transferId&&arrived.metadata.id==transferId&&sent.done==file.size()&&arrived.done==file.size(),
        "Completed transfer did not confirm all file bytes");
    check(sent.metadata.digest==fileHash&&arrived.metadata.digest==fileHash&&b.received->bytes==file&&digest(b.received->bytes)==fileHash,
        "Completed transfer hash/content mismatch");
    check(connected(a)&&connected(b),"Call lost connectivity during concurrent file transfer");
    auto afterFileA=activeDevice(a),afterFileB=activeDevice(b);
    check(afterFileA.captures>beforeFileA.captures&&afterFileB.captures>beforeFileB.captures&&
        afterFileA.supplied>beforeFileA.supplied&&afterFileB.supplied>beforeFileB.supplied&&
        afterFileA.energy>beforeFileA.energy&&afterFileB.energy>beforeFileB.energy,
        "Remote decoded PCM did not continue while real file transfer completed");
    for(const auto& row:a.transfers.diagnostics(now()))check(row.find("protocol_version=2 effective_chunk_bytes=65536 effective_window=16")!=std::string::npos,
        "File transfer did not negotiate real v2 pipeline");
    check(b.maxTransferFrame>65536&&b.transferFrames>1,"Real multi-frame file transfer was not observed");
    std::cout<<"MEASURE concurrent_file bytes_confirmed="<<sent.done<<" source_sha256_verified=1 transfer_frames="<<b.transferFrames
        <<" max_plain_frame="<<b.maxTransferFrame<<" A_pcm_blocks_during_file="<<(afterFileA.supplied-beforeFileA.supplied)
        <<" B_pcm_blocks_during_file="<<(afterFileB.supplied-beforeFileB.supplied)<<" durable_text_ack=1"<<'\n';
    check(a.voice.app.setMuted(true)&&b.voice.app.setDeafened(true),"Mute/deafen command failed");auto muted=activeDevice(a),deaf=activeDevice(b);run(a,b,12);
    check(activeDevice(a).captures==muted.captures&&activeDevice(b).captures>deaf.captures,"Mute did not stop only the selected synthetic microphone");
    check(activeDevice(b).energy==deaf.energy&&activeDevice(b).supplied>deaf.supplied,"Deafen did not silence PCM while advancing real NetEq");
    auto oldA=activeDevice(a),oldB=activeDevice(b);++a.network;
    wait(a,b,[&]{return connected(a)&&connected(b)&&a.calls.active()->mediaGeneration>=2&&a.calls.active()->mediaGeneration==b.calls.active()->mediaGeneration;},"Real media generation recovery failed");
    check(a.calls.lifetime()==lifetime&&a.calls.active()->id.value==first&&b.calls.active()->id.value==first,"Recovery replaced logical call ownership");
    auto recoveredA=activeDevice(a),recoveredB=activeDevice(b);check(recoveredA.id!=oldA.id&&recoveredB.id!=oldB.id,"Recovery retained an old media device");
    check(recoveredA.muted&&recoveredA.captures==0,"Recovered microphone started before preserved mute");
    check(a.voice.app.setMuted(false)&&b.voice.app.setDeafened(false),"Recovered controls could not be restored");measured(a,b,"recovered_call");
    check(!b.candidateReplay.empty(),"No genuine initial candidate available for stale replay test");
    auto generation=a.calls.active()->mediaGeneration;const auto devices=device::snapshots().size();
    check(a.endpoint().sendVoiceControl(b.candidateReplay),"Could not replay genuine stale candidate over authenticated TLS");run(a,b,3);
    check(connected(a)&&connected(b)&&a.calls.active()->mediaGeneration==generation&&b.calls.active()->mediaGeneration==generation&&device::snapshots().size()==devices,"Stale media-generation candidate affected current media");
    check(b.voice.app.end(first),"End command failed");wait(a,b,[&]{return a.calls.idle()&&b.calls.idle();},"Remote End did not release both calls");
    check(activeDevices()==0,"End leaked a synthetic device");const auto stopped=captures();run(a,b,3);check(captures()==stopped,"Retired device continued capture");
    check(!a.endReplay.empty(),"No genuine old End available for lifetime replay");
    a.endpoint().close(false);b.endpoint().close(false);check(!a.endpoint().sendVoiceControl(a.endReplay),"Retired TLS winner sent signaling");a.peers.clear();b.peers.clear();
    a.open(false);b.open(true);wait(a,b,[&]{return a.endpoint().ready()&&b.endpoint().ready();},"Pinned TLS reconnect failed");
    const auto second=Storage::newMessageId();check(b.start(second),"Reverse-direction repeated call failed");wait(a,b,[&]{return a.calls.state()==calls::State::Incoming;},"Reverse-direction call did not ring");
    check(a.answer(second),"Reverse explicit Answer failed");wait(a,b,[&]{return connected(a)&&connected(b);},"Repeated call did not connect");measured(a,b,"reverse_repeated_call");
    check(b.endpoint().sendVoiceControl(a.endReplay),"Old call End replay could not use current authenticated TLS");run(a,b,3);
    check(connected(a)&&connected(b)&&a.calls.active()->id.value==second&&b.calls.active()->id.value==second,"Old call End affected replacement lifetime");
    check(a.voice.app.end(second),"Reverse End failed");wait(a,b,[&]{return a.calls.idle()&&b.calls.idle();},"Reverse End did not clean up");
    a.friends.replace(0,2,{});a.voice.tick(now());check(!a.start(Storage::newMessageId()),"Revoked friend scope started a call");
    check(activeDevices()==0,"Final teardown retained device ownership");
    std::cout<<"PASS: shared AndroidVoiceApplication/VoiceApplication with real Schannel TLS, friend/pin/winner/capability admission, genuine Opus/ICE/DTLS-SRTP/NetEq, explicit Answer consent, bidirectional PCM, mute/deafen, generation recovery and stale replay, durable text ACK plus real v2 file/hash/confirmed-byte coexistence, reconnect, repeated reverse call and teardown. HOST ONLY; synthetic device, no physical microphone, JNI/Android OS or TURN infrastructure claim.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
