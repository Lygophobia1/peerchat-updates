#pragma once
#include "protocol.hpp"
#include "statistics.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <chrono>
#include <deque>

namespace peerchat::transfer {
// Logical bulk-transfer mechanics only. The caller supplies an authenticated,
// winning reliable route; this class never opens sockets or schedules reconnects.
constexpr size_t ChunkBytes=32768, MaxTransfers=4, MaxRecords=64;
constexpr size_t ExtendedChunkBytes=65536;
struct Options {
    size_t chunkBytes=ChunkBytes,window=1;
    std::uint64_t bitsPerSecond=0; // zero means no individual limit
    bool valid()const{return (chunkBytes==ChunkBytes||chunkBytes==ExtendedChunkBytes)&&(window==1||window==4||window==8||window==16)&&bitsPerSecond<=10000000000ull;}
};
constexpr std::uint64_t MaxFileBytes=256ull*1024*1024, IdleMilliseconds=15*60*1000;
using Digest=std::array<unsigned char,32>;
class Hash {
public:
    virtual ~Hash()=default;
    virtual void update(const unsigned char*,size_t)=0;
    virtual Digest finish()=0;
};
class Source {
public:
    virtual ~Source()=default;
    virtual std::uint64_t size()const=0;
    virtual std::string name()const=0; // UTF-8, basename only.
    virtual Blob read(std::uint64_t offset,size_t limit)=0;
    virtual Blob resumeToken()const{return {};}
};
class Sink {
public:
    virtual ~Sink()=default; // Abandon/delete only the owned temporary file.
    virtual void append(const Blob&)=0;
    virtual void commit()=0; // Flush + no-overwrite atomic rename.
    virtual void flush(){} // Durable prefix before acknowledgement.
    virtual Blob read(std::uint64_t,size_t){throw std::runtime_error("Sink cannot resume");}
    virtual Blob resumeToken()const{return {};}
    virtual void discard(){} // Explicit cancellation/expiry, never normal shutdown.
};
enum class State { Preparing, Offered, AwaitingAcceptance, Transferring, Complete, Cancelled, Failed, Recovering, Suspended };
enum class Kind:std::uint64_t { Offer=1, Resume=2, Chunk=3, Finish=4, Complete=5, Cancel=6 };
inline std::string packetId(const Blob& bytes){protocol::check(bytes.size()<=protocol::MaxPacket);protocol::Reader r(bytes);protocol::check(r.type()==protocol::FileTransfer);auto version=r.number(),kind=r.number();auto id=r.string();protocol::check((version==1||version==2)&&kind>=1&&kind<=6&&protocol::id(id,32));if(version==2)protocol::check(r.number()!=0);return id;}
struct Metadata {
    std::string id,name;
    std::uint64_t size=0;
    Digest digest{};
    bool operator==(const Metadata& b)const{return id==b.id&&name==b.name&&size==b.size&&digest==b.digest;}
};
struct Snapshot { Metadata metadata;std::string peer,error;State state=State::Preparing;bool sending=false;std::uint64_t done=0,created=0;
    bool statisticsKnown=false,etaReliable=false;std::uint64_t statisticsBytes=0,durationMilliseconds=0,currentBitsPerSecond=0;
};
// Persistence adapter only: no sockets, retry loop or independent transfer owner.
struct DurableState:Snapshot {
    Blob location;
    std::uint64_t highWaterSent=0,updated=0,expires=0;
    bool digestReady=false;
};
class StateStore {
public:
    virtual ~StateStore()=default;
    virtual std::uint64_t wallTime()const=0;
    virtual std::vector<DurableState> load()=0;
    virtual void save(const std::vector<DurableState>&)=0;
    virtual std::unique_ptr<Source> source(const DurableState&)=0;
    virtual std::unique_ptr<Sink> sink(const DurableState&)=0;
    // Optional profile-local tuning sidecar; never part of durable ACK authority.
    virtual std::map<std::string,Options> loadOptions(){return {};}
    virtual void saveOptions(const std::map<std::string,Options>&){}
};
struct PersistenceError:std::runtime_error{explicit PersistenceError(const std::string& reason):std::runtime_error("Protected transfer journal unavailable: "+reason){}};
class StatisticsStateStore {
public:
    virtual ~StatisticsStateStore()=default;
    virtual void load(std::vector<DurableState>&)=0;
    virtual void save(const std::vector<DurableState>&)=0;
};
struct CapacityError:std::runtime_error {
    std::string id;
    std::uint64_t version=1,negotiation=0;
    CapacityError(const std::string& message,const std::string& transferId={},std::uint64_t v=1,std::uint64_t n=0):std::runtime_error(message),id(transferId),version(v),negotiation(n){}
    Blob cancellation()const{protocol::check(protocol::id(id,32));protocol::Writer w(protocol::FileTransfer);w.number(version);w.number(static_cast<unsigned>(Kind::Cancel));w.string(id);if(version==2)w.number(negotiation);return w.data;}
};
// Platform finalizers supply only controlled stage names + numeric errno,
// never a path, filename, payload or arbitrary provider exception text.
struct CompletionError:std::runtime_error {
    enum class Stage { FileSync, Rename, DestinationConflict, DirectorySync, PartialCleanup };
    CompletionError(Stage stage,int error):std::runtime_error(std::string("atomic finalize ")+name(stage)+" failed (errno "+std::to_string(error)+")"){}
private:
    static const char* name(Stage value){switch(value){case Stage::FileSync:return "file sync";case Stage::Rename:return "rename";
        case Stage::DestinationConflict:return "destination conflict";case Stage::DirectorySync:return "directory sync";case Stage::PartialCleanup:return "partial cleanup";}return "unknown";}
};
constexpr std::uint64_t RetentionMilliseconds=7ull*24*60*60*1000;
inline bool safeName(const std::string& name){
    if(name.empty()||name.size()>240||name=="."||name==".."||name.back()=='.'||name.back()==' ')return false;
    try{unicode::decode(name);}catch(...){return false;}
    for(auto c:name)if(static_cast<unsigned char>(c)<32||c==127||c=='/'||c=='\\'||c==':'||c=='<'||c=='>'||c=='"'||c=='|'||c=='?'||c=='*')return false;
    auto stem=name.substr(0,name.find('.'));for(auto& c:stem)if(c>='a'&&c<='z')c-=32;
    if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL")return false;
    if(stem.size()==4&&(stem.substr(0,3)=="COM"||stem.substr(0,3)=="LPT")&&stem[3]>='0'&&stem[3]<='9')return false;
    return true;
}
class TransferManager {
    struct Timer {
        std::uint64_t& total;
        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        explicit Timer(std::uint64_t& value):total(value){}
        ~Timer(){total+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());}
    };
    struct Entry:Snapshot {
        std::unique_ptr<Source> source;
        std::unique_ptr<Sink> sink;
        std::unique_ptr<Hash> hash;
        std::uint64_t prepared=0,lastActivity=0,route=0,sentUntil=0,highWaterSent=0;
        bool ready=false,finishSent=false;
        Options requested,effective;unsigned version=1;std::uint64_t negotiation=0;
        std::deque<std::uint64_t> outstanding;
        bool blocked=false;
        bool offerSeen=true;
        double tokens=ExtendedChunkBytes;std::uint64_t tokenAt=0;
        std::optional<Blob> reply;
        Blob location;
        std::uint64_t updated=0,expires=0;
        bool digestReady=false;
        std::uint64_t observedAt=0,routeAt=0,firstPayloadAt=0,lastAckAt=0,payloadBytes=0;
        std::uint64_t readUs=0,hashUs=0,writeUs=0,flushUs=0,commitUs=0,routeChanges=0,backpressure=0,ackWaitTicks=0;
        statistics::RateMeter rateMeter;bool statisticsInitialized=false,statisticsFinished=false;
    };
    std::map<std::string,Entry> entries_;
    std::function<std::unique_ptr<Hash>()> hash_;
    StateStore* store_=nullptr;
    StatisticsStateStore* statistics_=nullptr;
    std::uint64_t persistenceRetryAt_=0;
    std::uint64_t journalUs_=0;
    struct Capability {std::uint64_t route=0;bool pipeline=false;};
    std::map<std::string,Capability> capabilities_;
    std::uint64_t nextNegotiation_=1;
    std::map<std::string,Options> options_;
    std::uint64_t aggregateBits_=0,aggregateAt_=0;double aggregateTokens_=ExtendedChunkBytes;
    size_t turn_=0;bool mediaActive_=false,optionsUnavailable_=false;
    static void refill(double& tokens,std::uint64_t& at,std::uint64_t bits,std::uint64_t now){if(!at){at=now;return;}if(now>=at){tokens=std::min<double>(ExtendedChunkBytes,tokens+double(now-at)*bits/8000.0);at=now;}}
    bool budget(Entry& e,size_t count,std::uint64_t now){refill(aggregateTokens_,aggregateAt_,aggregateBits_,now);refill(e.tokens,e.tokenAt,e.requested.bitsPerSecond,now);return (!aggregateBits_||aggregateTokens_>=count)&&(!e.requested.bitsPerSecond||e.tokens>=count);}
    static bool terminal(State s){return s==State::Complete||s==State::Cancelled||s==State::Failed||s==State::Suspended;}
    void save(){
        if(!store_&&!statistics_)return;
        Timer timer(journalUs_);
        std::vector<DurableState> records;
        for(auto& p:entries_){
            auto& e=p.second;DurableState d;static_cast<Snapshot&>(d)=e;
            if(e.source)e.location=e.source->resumeToken();
            else if(e.sink)e.location=e.sink->resumeToken();
            d.location=e.location;d.highWaterSent=e.highWaterSent;d.digestReady=e.digestReady;
            d.created=e.created;d.updated=e.updated;d.expires=e.expires;records.push_back(std::move(d));
        }
        if(store_)try{store_->save(records);}catch(const std::exception& e){throw PersistenceError(std::string(e.what()).substr(0,128));}catch(...){throw PersistenceError("provider failure");}
        // Optional telemetry storage never gates resume state, payload or ACKs.
        if(statistics_)try{statistics_->save(records);}catch(...){}
    }
    void statistics(Entry& e,std::uint64_t now){
        if(!e.statisticsInitialized){e.rateMeter.baseline(e.statisticsBytes,e.lastActivity);e.statisticsInitialized=true;}
        if(e.statisticsFinished){e.currentBitsPerSecond=0;e.etaReliable=false;return;}
        const bool finished=e.state==State::Complete||e.state==State::Cancelled||e.state==State::Failed;
        auto measured=e.rateMeter.observe(e.done,now,e.state==State::Transferring,finished,e.state==State::Suspended);
        e.statisticsBytes=measured.confirmedBytes;e.currentBitsPerSecond=measured.bitsPerSecond;e.etaReliable=measured.etaReliable;
        if(!measured.sampled)return;
        e.durationMilliseconds=std::min(RetentionMilliseconds,e.durationMilliseconds+std::min(measured.elapsedMilliseconds,RetentionMilliseconds));
        // Existing protected created-time supplies end-to-end duration across
        // resume/restart without persisting live statistics or changing journals.
        if(store_&&e.created){const auto wall=store_->wallTime();if(wall>=e.created&&wall-e.created<=RetentionMilliseconds)e.durationMilliseconds=wall-e.created;else e.statisticsKnown=false;}
        e.statisticsFinished=finished;
    }
    void touch(Entry& e){if(store_){e.updated=store_->wallTime();if(!e.created){e.created=e.updated;e.expires=e.created+RetentionMilliseconds;}}}
    void suspend(Entry& e,const std::string& reason){e.state=State::Suspended;e.error="Retained for restart: "+reason.substr(0,160);e.reply.reset();e.ready=false;}
    static protocol::Writer packet(Kind kind,const std::string& id){protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(static_cast<std::uint64_t>(kind));w.string(id);return w;}
    static protocol::Writer packet(Kind kind,const Entry& e){protocol::Writer w(protocol::FileTransfer);w.number(e.version);w.number(static_cast<std::uint64_t>(kind));w.string(e.metadata.id);if(e.version==2)w.number(e.negotiation);return w;}
    static Blob offsetPacket(Kind kind,const Entry& e){auto w=packet(kind,e);w.number(e.done);return w.data;}
    void room(){
        size_t active=0;for(auto& p:entries_)if(!terminal(p.second.state))++active;
        for(auto& p:entries_)if(p.second.state==State::Suspended)++active;
        if(active>=MaxTransfers)throw CapacityError("File transfer limit reached");
        if(entries_.size()>=MaxRecords){
            auto oldest=entries_.end();
            for(auto i=entries_.begin();i!=entries_.end();++i)if(terminal(i->second.state)&&i->second.state!=State::Suspended&&(oldest==entries_.end()||i->second.lastActivity<oldest->second.lastActivity))oldest=i;
            if(oldest==entries_.end())throw CapacityError("File transfer record limit reached");
            entries_.erase(oldest);
        }
    }
    void fail(Entry& e,const char* error){
        e.state=State::Failed;e.error=error;
        try{if(e.sink)e.sink->discard();}catch(...){e.error="Owned partial cleanup failed; manual attention required";}
        e.sink.reset();e.source.reset();e.hash.reset();
        e.reply=packet(Kind::Cancel,e).data;
    }
public:
    explicit TransferManager(std::function<std::unique_ptr<Hash>()> hash,StateStore* store=nullptr,StatisticsStateStore* statistics=nullptr):hash_(std::move(hash)),store_(store),statistics_(statistics){if(!hash_)throw std::runtime_error("Hash provider unavailable");}
    // Adapter supplies only current authenticated winner capabilities. Never
    // infer support from a transfer packet or a persistent peer preference.
    void capability(const std::string& peer,std::uint64_t route,bool pipeline){if(!protocol::id(peer))throw std::runtime_error("Invalid transfer peer");if(!capabilities_.count(peer)&&capabilities_.size()>=128){auto old=std::find_if(capabilities_.begin(),capabilities_.end(),[&](const auto& cap){return std::none_of(entries_.begin(),entries_.end(),[&](const auto& row){return row.second.peer==cap.first&&(!terminal(row.second.state)||row.second.reply);});});if(old!=capabilities_.end())capabilities_.erase(old);else throw std::runtime_error("Transfer capability bound");}capabilities_[peer]={route,pipeline};}
    void configure(const std::string& id,Options value){auto i=entries_.find(id);if(!value.valid()||(!id.empty()&&(i==entries_.end()||!i->second.sending)))throw std::runtime_error("Invalid transfer settings");auto next=options_;for(auto p=next.begin();p!=next.end();)if(!p->first.empty()&&!entries_.count(p->first))p=next.erase(p);else ++p;next[id]=value;if(next.size()>MaxRecords+1)throw std::runtime_error("Transfer settings bound");if(store_)store_->saveOptions(next);options_=std::move(next);optionsUnavailable_=false;if(!id.empty())i->second.requested=value;else{aggregateBits_=value.bitsPerSecond;aggregateTokens_=0;aggregateAt_=0;}}
    Options options(const std::string& id={})const{if(optionsUnavailable_)throw std::runtime_error("Protected transfer settings unavailable; payload submission paused");auto i=options_.find(id);if(i!=options_.end())return i->second;auto e=entries_.find(id);return e==entries_.end()?Options{}:e->second.requested;}
    void mediaActive(bool value){mediaActive_=value;}
    bool hasTransmissionWork()const{if(optionsUnavailable_)return false;for(const auto& p:entries_){const auto& e=p.second;if(e.sending&&e.state==State::Transferring&&e.ready&&e.route&&!e.blocked&&e.sentUntil<e.metadata.size&&e.outstanding.size()<(mediaActive_?1:e.effective.window))return true;}return false;}
    void restore(std::uint64_t now){
        if(!store_||!entries_.empty())throw std::runtime_error("Invalid transfer restore");
        try{options_=store_->loadOptions();if(options_.size()>MaxRecords+1)throw std::runtime_error("Transfer settings bound");for(const auto& p:options_)if((!p.first.empty()&&!protocol::id(p.first,32))||!p.second.valid())throw std::runtime_error("Invalid transfer settings");aggregateBits_=options().bitsPerSecond;}catch(...){options_.clear();aggregateBits_=0;optionsUnavailable_=true;}
        auto records=store_->load();if(records.size()>MaxRecords)throw std::runtime_error("Transfer journal limit");
        if(statistics_)try{statistics_->load(records);}catch(...){for(auto& d:records)d.statisticsKnown=false;}
        size_t active=0;
        for(auto& d:records){
            if(!protocol::id(d.metadata.id,32)||!protocol::id(d.peer)||!safeName(d.metadata.name)||d.metadata.size>MaxFileBytes||
                d.done>d.metadata.size||d.highWaterSent>d.metadata.size||d.state>State::Suspended||!d.created||d.updated<d.created||
                d.expires<d.created||d.expires-d.created>RetentionMilliseconds||entries_.count(d.metadata.id))throw std::runtime_error("Invalid protected transfer state");
            Entry e;static_cast<Snapshot&>(e)=d;e.location=d.location;e.highWaterSent=d.highWaterSent;
            e.offerSeen=false;
            e.requested=options_.count(d.metadata.id)?options(d.metadata.id):options();if(!options_.count(d.metadata.id))e.requested.bitsPerSecond=0;
            e.created=d.created;e.updated=d.updated;e.expires=d.expires;e.digestReady=d.digestReady;e.lastActivity=now;
            e.statisticsBytes=std::max(e.statisticsBytes,e.done);e.rateMeter.baseline(e.statisticsBytes,now);e.statisticsInitialized=true;e.statisticsFinished=d.state==State::Complete||d.state==State::Cancelled||d.state==State::Failed;
            bool resumable=!terminal(d.state)||d.state==State::Suspended;
            if(resumable&&store_->wallTime()>=e.created&&store_->wallTime()-e.created<=RetentionMilliseconds)e.statisticsKnown=true;
            if(resumable){
                if(++active>MaxTransfers)throw std::runtime_error("Transfer restore limit");
                try{
                    if(d.sending){e.source=store_->source(d);if(!e.source||e.source->size()!=d.metadata.size||e.source->name()!=d.metadata.name)throw std::runtime_error("Source changed");}
                    else if(d.state!=State::AwaitingAcceptance){e.sink=store_->sink(d);if(!e.sink)throw std::runtime_error("Partial missing");}
                    if(store_->wallTime()>=e.expires){fail(e,"Retained transfer expired");e.reply.reset();}
                    else if(e.source||e.sink){e.hash=hash_();if(!e.hash)throw std::runtime_error("Hash unavailable");e.state=State::Recovering;}
                }catch(...){e.state=State::Failed;e.error="Restart validation failed: source/partial missing or changed";e.reply.reset();}
            }
            entries_.emplace(e.metadata.id,std::move(e));
        }
        save();
    }
    std::vector<Snapshot> snapshots(std::uint64_t now=0)const{std::vector<Snapshot> result;for(auto& p:entries_){Snapshot s=p.second;if(s.state!=State::Transferring||(now&&p.second.rateMeter.current(now,true)==0)){s.currentBitsPerSecond=0;s.etaReliable=false;}result.push_back(std::move(s));}return result;}
    // Session-only counters, not journal/wire state. No names, paths or content.
    std::vector<std::string> diagnostics(std::uint64_t now)const{
        std::vector<std::string> rows;
        for(const auto& p:entries_){const auto& e=p.second;
            rows.push_back("transfer="+p.first+" sending="+std::to_string(e.sending)+" state="+std::to_string(static_cast<unsigned>(e.state))+
                " size="+std::to_string(e.metadata.size)+" acknowledged_or_durable="+std::to_string(e.done)+
                " payload_bytes="+std::to_string(e.payloadBytes)+" elapsed_ms="+std::to_string(e.observedAt&&now>=e.observedAt?now-e.observedAt:0)+
                " route_delay_ms="+std::to_string(e.routeAt>=e.observedAt?e.routeAt-e.observedAt:0)+
                " first_payload_delay_ms="+std::to_string(e.firstPayloadAt>=e.observedAt?e.firstPayloadAt-e.observedAt:0)+
                " last_ack_ms="+std::to_string(e.lastAckAt)+" read_us="+std::to_string(e.readUs)+" hash_us="+std::to_string(e.hashUs)+
                " write_us="+std::to_string(e.writeUs)+" flush_us="+std::to_string(e.flushUs)+" commit_us="+std::to_string(e.commitUs)+
                " route_changes="+std::to_string(e.routeChanges)+" backpressure="+std::to_string(e.backpressure)+
                " ack_wait_ticks="+std::to_string(e.ackWaitTicks)+" journal_all_transfers_us="+std::to_string(journalUs_));
            rows.back()+=" protocol_version="+std::to_string(e.version)+" effective_chunk_bytes="+std::to_string(e.effective.chunkBytes)+" effective_window="+std::to_string(e.effective.window)+" outstanding_chunks="+std::to_string(e.outstanding.size())+" aggregate_cap_bps="+std::to_string(aggregateBits_)+" individual_cap_bps="+std::to_string(e.requested.bitsPerSecond)+" settings_unavailable="+std::to_string(optionsUnavailable_);
        }return rows;
    }
    bool hasPendingControl(const std::string& id)const{auto found=entries_.find(id);return found!=entries_.end()&&found->second.reply.has_value();}
    // Local adapter readout only; tokens never travel in a transfer packet.
    std::vector<DurableState> retainedRecords()const{
        std::vector<DurableState> result;
        for(const auto& p:entries_){
            const auto& e=p.second;DurableState d;static_cast<Snapshot&>(d)=e;
            d.location=e.location;d.created=e.created;d.updated=e.updated;d.expires=e.expires;
            d.highWaterSent=e.highWaterSent;d.digestReady=e.digestReady;result.push_back(std::move(d));
        }
        return result;
    }
    std::vector<DurableState> completedRecords()const{
        auto rows=retainedRecords();rows.erase(std::remove_if(rows.begin(),rows.end(),[](const auto& row){return row.state!=State::Complete;}),rows.end());return rows;
    }
    std::vector<std::string> demandedPeers()const{
        std::vector<std::string> result;for(auto& p:entries_)if(!terminal(p.second.state)||p.second.reply)result.push_back(p.second.peer);return result;
    }
    void offer(const std::string& peer,const std::string& id,std::unique_ptr<Source> source,std::uint64_t now,const Digest* expectedDigest=nullptr){
        if(!protocol::id(peer)||!protocol::id(id,32)||!source||!safeName(source->name())||source->size()>MaxFileBytes||entries_.count(id))throw std::runtime_error("Invalid file offer");
        room();Entry e;e.peer=peer;e.metadata.id=id;e.metadata.name=source->name();e.metadata.size=source->size();
        if(expectedDigest){e.metadata.digest=*expectedDigest;e.digestReady=true;}
        e.requested=options();e.requested.bitsPerSecond=0;
        // Snapshot this file's request, so changing defaults does not silently
        // change its framing after restart. The aggregate cap remains global.
        auto tuning=options_;for(auto p=tuning.begin();p!=tuning.end();)if(!p->first.empty()&&!entries_.count(p->first))p=tuning.erase(p);else ++p;
        tuning[id]=e.requested;if(store_)store_->saveOptions(tuning);options_=std::move(tuning);
        e.sending=true;e.statisticsKnown=true;e.lastActivity=now;e.hash=hash_();if(!e.hash)throw std::runtime_error("Hash provider unavailable");e.source=std::move(source);touch(e);entries_.emplace(id,std::move(e));save();
    }
    void accept(const std::string& id,std::unique_ptr<Sink> sink,std::uint64_t now){
        auto i=entries_.find(id);if(i==entries_.end()||i->second.sending||i->second.state!=State::AwaitingAcceptance||!sink)throw std::runtime_error("File offer no longer available");
        auto& e=i->second;e.hash=hash_();if(!e.hash)throw std::runtime_error("Hash provider unavailable");e.sink=std::move(sink);e.state=State::Transferring;e.lastActivity=now;e.reply=offsetPacket(Kind::Resume,e);touch(e);save();
    }
    void cancel(const std::string& id){
        auto i=entries_.find(id);if(i==entries_.end()||(terminal(i->second.state)&&i->second.state!=State::Suspended))return;
        auto& e=i->second;e.state=State::Cancelled;if(e.sink)e.sink->discard();e.sink.reset();e.source.reset();e.hash.reset();e.reply=packet(Kind::Cancel,e).data;touch(e);save();
    }
    bool hasPreparationWork(std::uint64_t now)const{
        if(optionsUnavailable_||now<persistenceRetryAt_)return false;
        return std::any_of(entries_.begin(),entries_.end(),[](const auto& row){return row.second.state==State::Preparing||row.second.state==State::Recovering;});
    }
    void unsupportedPeer(const std::string& peer){
        for(auto& p:entries_)if(p.second.peer==peer&&p.second.sending&&!terminal(p.second.state)){fail(p.second,"Recipient does not support file transfers");p.second.reply.reset();}
    }
    // One bounded hash/read/packet operation per transfer per tick. A route token
    // changes only with SessionTransportManager's winning authenticated session.
    void tick(std::uint64_t now,const std::function<std::uint64_t(const std::string&)>& route,const std::function<bool(const std::string&,const Blob&)>& send){
        tickScoped(now,[&](const Snapshot& e){return route(e.peer);},[&](const Snapshot& e,const Blob& frame){return send(e.peer,frame);});
    }
    // Execution adapters choose the authorized route for this exact transfer.
    // Shared preparation, journal, deadlines and completion ownership are unchanged.
    void tickScoped(std::uint64_t now,const std::function<std::uint64_t(const Snapshot&)>& route,const std::function<bool(const Snapshot&,const Blob&)>& send){
        if(optionsUnavailable_||now<persistenceRetryAt_)return;
        try{tickImplScoped(now,route,send);persistenceRetryAt_=0;}
        catch(const PersistenceError&){persistenceRetryAt_=now+250;throw;}
    }
    void tickImplScoped(std::uint64_t now,const std::function<std::uint64_t(const Snapshot&)>& route,const std::function<bool(const Snapshot&,const Blob&)>& send){
        // Every acknowledgement/offer is gated by durable metadata. A failed
        // journal write propagates to the adapter; never ACK uncommitted progress.
        auto durableSend=[&](const Snapshot& entry,const Blob& frame){save();return send(entry,frame);};
        std::vector<Entry*> ordered;for(auto& p:entries_)ordered.push_back(&p.second);
        if(!ordered.empty()){std::rotate(ordered.begin(),ordered.begin()+(turn_++%ordered.size()),ordered.end());}
        for(auto entry:ordered){
            auto& e=*entry;e.blocked=false;
            if(!e.observedAt)e.observedAt=now;
            if(store_){
                if(e.state!=State::Complete&&e.state!=State::Cancelled&&e.state!=State::Failed&&store_->wallTime()>=e.expires)fail(e,"Retained transfer expired");
            }else if(!terminal(e.state)&&now-e.lastActivity>IdleMilliseconds){fail(e,"File transfer expired");}
            if(terminal(e.state)&&now-e.lastActivity>IdleMilliseconds)e.reply.reset();
            try{
                if(e.state==State::Recovering){
                    auto total=e.sending?e.metadata.size:e.done;
                    if(e.prepared<total){
                        auto n=static_cast<size_t>(std::min<std::uint64_t>(ChunkBytes,total-e.prepared));
                        Blob b;{Timer timer(e.readUs);b=e.sending?e.source->read(e.prepared,n):e.sink->read(e.prepared,n);}
                        if(b.size()!=n)throw std::runtime_error("Restart read failed");
                        {Timer timer(e.hashUs);e.hash->update(b.data(),b.size());}e.prepared+=b.size();e.lastActivity=now;continue;
                    }
                    if(e.sending){
                        auto digest=e.hash->finish();e.hash.reset();
                        if(e.digestReady&&digest!=e.metadata.digest){fail(e,"Source content changed after restart");continue;}
                        e.metadata.digest=digest;e.digestReady=true;e.state=State::Offered;
                    }else if(e.done==e.metadata.size){
                        // A retained full file can finish locally after exact
                        // revalidation, even while its sender is unavailable.
                        if(e.hash->finish()!=e.metadata.digest){fail(e,"File integrity verification failed after restart");continue;}
                        {Timer timer(e.commitUs);e.sink->commit();}
                        e.sink.reset();e.hash.reset();e.state=State::Complete;e.reply.reset();
                    }else{e.state=State::Transferring;if(e.offerSeen)e.reply=offsetPacket(Kind::Resume,e);}
                    e.error.clear();touch(e);
                }
                if(e.state==State::Preparing){
                    if(e.prepared<e.metadata.size){
                        Blob b;{Timer timer(e.readUs);b=e.source->read(e.prepared,static_cast<size_t>(std::min<std::uint64_t>(ChunkBytes,e.metadata.size-e.prepared)));}
                        if(b.empty()||b.size()>ChunkBytes||b.size()>e.metadata.size-e.prepared)throw std::runtime_error("Source read failed");
                        {Timer timer(e.hashUs);e.hash->update(b.data(),b.size());}e.prepared+=b.size();e.lastActivity=now;continue;
                    }
                    auto digest=e.hash->finish();e.hash.reset();
                    if(e.digestReady&&digest!=e.metadata.digest){fail(e,"Source no longer matches verified content");continue;}
                    e.metadata.digest=digest;e.state=State::Offered;e.digestReady=true;touch(e);
                }
                auto current=route(e);if(!current){e.route=0;e.ready=false;continue;}
                if(!e.routeAt)e.routeAt=now;
                // Never send a pending ACK from an old negotiation onto a new
                // route before its authenticated Offer establishes the scope.
                if(!e.sending&&e.version==2){auto cap=capabilities_.find(e.peer);if(cap==capabilities_.end()||cap->second.route!=e.route)continue;}
                if(e.reply){if(durableSend(e,*e.reply))e.reply.reset();continue;}
                if(terminal(e.state)||!e.sending)continue;
                if(e.route!=current){
                    auto cap=capabilities_.find(e.peer);bool extended=cap!=capabilities_.end()&&cap->second.route==current&&cap->second.pipeline;
                    e.version=extended&&(e.requested.chunkBytes!=ChunkBytes||e.requested.window!=1)?2:1;e.effective=e.version==2?e.requested:Options{};
                    e.negotiation=e.version==2?nextNegotiation_++:0;e.outstanding.clear();
                    auto w=packet(Kind::Offer,e);w.string(e.metadata.name);w.number(e.metadata.size);w.bytes(Blob(e.metadata.digest.begin(),e.metadata.digest.end()));
                    if(e.version==2){w.number(e.effective.chunkBytes);w.number(e.effective.window);}
                    if(durableSend(e,w.data)){++e.routeChanges;e.route=current;e.ready=false;e.finishSent=false;e.sentUntil=e.done;}
                    continue;
                }
                if(!e.ready||e.outstanding.size()>=(mediaActive_?1:e.effective.window)){++e.ackWaitTicks;continue;}
                if(e.sentUntil<e.metadata.size){
                    auto planned=static_cast<size_t>(std::min<std::uint64_t>(e.effective.chunkBytes,e.metadata.size-e.sentUntil));
                    if(!budget(e,planned,now)){e.blocked=true;continue;}
                    Blob b;{Timer timer(e.readUs);auto count=std::min<std::uint64_t>(e.effective.chunkBytes,e.metadata.size-e.sentUntil);for(std::uint64_t n=0;n<count;){auto limit=static_cast<size_t>(std::min<std::uint64_t>(ChunkBytes,count-n));auto part=e.source->read(e.sentUntil+n,limit);if(part.size()!=limit)throw std::runtime_error("Source changed or unreadable");b.insert(b.end(),part.begin(),part.end());n+=limit;}}
                    auto w=packet(Kind::Chunk,e);w.number(e.sentUntil);w.bytes(b);
                    e.highWaterSent=std::max(e.highWaterSent,e.sentUntil+b.size());touch(e);
                    if(durableSend(e,w.data)){if(aggregateBits_)aggregateTokens_-=b.size();if(e.requested.bitsPerSecond)e.tokens-=b.size();e.sentUntil+=b.size();e.outstanding.push_back(e.sentUntil);e.payloadBytes+=b.size();if(!e.firstPayloadAt)e.firstPayloadAt=now;}else {++e.backpressure;e.blocked=true;}
                }else if(e.done==e.metadata.size&&!e.finishSent){auto w=packet(Kind::Finish,e);if(durableSend(e,w.data))e.finishSent=true;}
            }catch(const PersistenceError&){throw;} // Retry persistence on the existing next tick; never ACK it early.
            catch(const std::exception& error){if(store_)suspend(e,error.what());else fail(e,"Local file I/O or hash failure");}
        }
        for(auto& item:entries_)statistics(item.second,now);
        save();
    }
    // Caller must verify TLS identity, purpose, negotiated capability and winner
    // BEFORE dispatch. Peer ID is never obtained from the transfer payload.
    void receive(const std::string& peer,const Blob& frame,std::uint64_t now){receiveImpl(peer,frame,now);for(auto& item:entries_)statistics(item.second,now);save();}
    void receiveImpl(const std::string& peer,const Blob& frame,std::uint64_t now){
        protocol::Reader r(frame);protocol::check(r.type()==protocol::FileTransfer);auto version=r.number();protocol::check(version==1||version==2);
        auto kind=static_cast<Kind>(r.number());auto id=r.string();protocol::check(protocol::id(id,32));auto negotiation=version==2?r.number():0;
        auto cap=capabilities_.find(peer);if(version==2)protocol::check(cap!=capabilities_.end()&&cap->second.pipeline&&cap->second.route&&negotiation);
        auto i=entries_.find(id);
        if(kind==Kind::Offer){
            Metadata m;m.id=id;m.name=r.string();m.size=r.number();auto digest=r.bytes();Options options;if(version==2){options.chunkBytes=r.number();options.window=r.number();}r.end();protocol::check(options.valid());
            protocol::check(safeName(m.name)&&m.size<=MaxFileBytes&&digest.size()==32);
            std::copy(digest.begin(),digest.end(),m.digest.begin());
            if(i!=entries_.end()){
                auto& e=i->second;protocol::check(e.peer==peer&&!e.sending&&e.metadata==m);
                e.offerSeen=true;e.version=static_cast<unsigned>(version);e.negotiation=negotiation;e.effective=options;e.route=version==2?cap->second.route:0;
                e.lastActivity=now;
                // A restarted sender must first learn the durable full offset,
                // then send Finish before receiving Complete. Do not bypass
                // its existing completion ordering with an unsolicited ACK.
                if(e.state==State::Complete)e.reply=offsetPacket(Kind::Resume,e);
                else if(e.state==State::Transferring)e.reply=offsetPacket(Kind::Resume,e);
                else if(terminal(e.state))e.reply=packet(Kind::Cancel,e).data;
                return;
            }
            protocol::check(protocol::id(peer));try{room();}catch(const CapacityError& e){throw CapacityError(e.what(),id,version,negotiation);}Entry e;e.metadata=m;e.peer=peer;e.version=static_cast<unsigned>(version);e.negotiation=negotiation;e.effective=options;e.route=version==2?cap->second.route:0;e.state=State::AwaitingAcceptance;e.lastActivity=now;e.digestReady=true;e.statisticsKnown=true;touch(e);entries_.emplace(id,std::move(e));return;
        }
        if(kind==Kind::Cancel&&i==entries_.end()){r.end();return;} // Cancellation can race an unobserved offer.
        protocol::check(i!=entries_.end()&&i->second.peer==peer);auto& e=i->second;
        if(version==2&&(e.version!=version||e.negotiation!=negotiation))return; // superseded authenticated negotiation
        protocol::check(e.version==version||kind==Kind::Cancel);
        if(kind==Kind::Resume){
            auto offset=r.number();r.end();protocol::check(e.sending&&offset<=e.metadata.size&&offset<=e.highWaterSent);
            if(e.state==State::Recovering)return; // No peer event may bypass full source revalidation.
            if(terminal(e.state))return;
            if(e.ready){if(offset<=e.done)return;protocol::check(offset<=e.sentUntil&&std::find(e.outstanding.begin(),e.outstanding.end(),offset)!=e.outstanding.end());}
            else {e.sentUntil=offset;e.outstanding.clear();}
            e.done=offset;while(!e.outstanding.empty()&&e.outstanding.front()<=offset)e.outstanding.pop_front();e.ready=true;e.finishSent=false;e.state=State::Transferring;e.lastActivity=now;e.lastAckAt=now;return;
        }
        if(kind==Kind::Cancel){r.end();if(!terminal(e.state)||e.state==State::Suspended){e.state=State::Cancelled;try{if(e.sink)e.sink->discard();}catch(...){e.error="Owned partial cleanup failed; manual attention required";}e.sink.reset();e.source.reset();e.hash.reset();}e.reply.reset();return;}
        if(kind==Kind::Complete){r.end();protocol::check(e.sending&&e.done==e.metadata.size&&e.finishSent);e.state=State::Complete;e.source.reset();return;}
        protocol::check(!e.sending);
        if(kind==Kind::Chunk){
            auto offset=r.number();auto b=r.bytes();r.end();protocol::check(!b.empty()&&b.size()<=e.effective.chunkBytes&&offset<=e.metadata.size&&b.size()<=e.metadata.size-offset);
            if(e.state==State::Complete){e.reply=packet(Kind::Complete,e).data;return;}
            if(e.state==State::Cancelled||e.state==State::Failed){e.reply=packet(Kind::Cancel,e).data;return;}
            protocol::check(e.state==State::Transferring&&e.sink&&e.hash);
            if(offset<e.done){protocol::check(offset+b.size()<=e.done);e.reply=offsetPacket(Kind::Resume,e);return;}
            protocol::check(offset==e.done);
            try{
                {Timer timer(e.writeUs);for(size_t n=0;n<b.size();n+=ChunkBytes)e.sink->append(Blob(b.begin()+n,b.begin()+std::min(b.size(),n+ChunkBytes)));}
                {Timer timer(e.flushUs);e.sink->flush();}
                {Timer timer(e.hashUs);for(size_t n=0;n<b.size();n+=ChunkBytes)e.hash->update(b.data()+n,std::min(ChunkBytes,b.size()-n));}
                e.payloadBytes+=b.size();if(!e.observedAt)e.observedAt=now;if(!e.firstPayloadAt)e.firstPayloadAt=now;
                e.done+=b.size();e.lastActivity=now;touch(e);e.reply=offsetPacket(Kind::Resume,e);
            }catch(...){if(store_)suspend(e,"receive disk or hash failure");else fail(e,"Receive disk or hash failure");}
            return;
        }
        if(kind==Kind::Finish){
            r.end();if(e.state==State::Complete){e.reply=packet(Kind::Complete,e).data;return;}
            if(e.state==State::Cancelled||e.state==State::Failed){e.reply=packet(Kind::Cancel,e).data;return;}
            protocol::check(e.state==State::Transferring&&e.done==e.metadata.size&&e.hash&&e.sink);
            try{
                if(e.hash->finish()!=e.metadata.digest){fail(e,"File integrity verification failed");return;}
                {Timer timer(e.commitUs);e.sink->commit();}e.sink.reset();e.hash.reset();e.state=State::Complete;e.reply=packet(Kind::Complete,e).data;
            }catch(const CompletionError& error){if(store_)suspend(e,error.what());else fail(e,error.what());}
            catch(...){if(store_)suspend(e,"atomic file completion failed");else fail(e,"Atomic file completion failed");}
            return;
        }
        throw std::runtime_error("Unknown file control");
    }
};
}
