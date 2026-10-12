#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <chrono>

namespace peerchat::transfer {
// Logical bulk-transfer mechanics only. The caller supplies an authenticated,
// winning reliable route; this class never opens sockets or schedules reconnects.
constexpr size_t ChunkBytes=32768, MaxTransfers=4, MaxRecords=64;
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
struct Metadata {
    std::string id,name;
    std::uint64_t size=0;
    Digest digest{};
    bool operator==(const Metadata& b)const{return id==b.id&&name==b.name&&size==b.size&&digest==b.digest;}
};
struct Snapshot { Metadata metadata;std::string peer,error;State state=State::Preparing;bool sending=false;std::uint64_t done=0,created=0; };
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
};
struct PersistenceError:std::runtime_error{explicit PersistenceError(const std::string& reason):std::runtime_error("Protected transfer journal unavailable: "+reason){}};
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
        std::optional<Blob> reply;
        Blob location;
        std::uint64_t updated=0,expires=0;
        bool digestReady=false;
        std::uint64_t observedAt=0,routeAt=0,firstPayloadAt=0,lastAckAt=0,payloadBytes=0;
        std::uint64_t readUs=0,hashUs=0,writeUs=0,flushUs=0,commitUs=0,routeChanges=0,backpressure=0,ackWaitTicks=0;
    };
    std::map<std::string,Entry> entries_;
    std::function<std::unique_ptr<Hash>()> hash_;
    StateStore* store_=nullptr;
    std::uint64_t persistenceRetryAt_=0;
    std::uint64_t journalUs_=0;
    static bool terminal(State s){return s==State::Complete||s==State::Cancelled||s==State::Failed||s==State::Suspended;}
    void save(){
        if(!store_)return;
        Timer timer(journalUs_);
        std::vector<DurableState> records;
        for(auto& p:entries_){
            auto& e=p.second;DurableState d;static_cast<Snapshot&>(d)=e;
            if(e.source)e.location=e.source->resumeToken();
            else if(e.sink)e.location=e.sink->resumeToken();
            d.location=e.location;d.highWaterSent=e.highWaterSent;d.digestReady=e.digestReady;
            d.created=e.created;d.updated=e.updated;d.expires=e.expires;records.push_back(std::move(d));
        }
        try{store_->save(records);}catch(const std::exception& e){throw PersistenceError(std::string(e.what()).substr(0,128));}catch(...){throw PersistenceError("provider failure");}
    }
    void touch(Entry& e){if(store_){e.updated=store_->wallTime();if(!e.created){e.created=e.updated;e.expires=e.created+RetentionMilliseconds;}}}
    void suspend(Entry& e,const std::string& reason){e.state=State::Suspended;e.error="Retained for restart: "+reason.substr(0,160);e.reply.reset();e.ready=false;}
    static protocol::Writer packet(Kind kind,const std::string& id){protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(static_cast<std::uint64_t>(kind));w.string(id);return w;}
    static Blob offsetPacket(Kind kind,const Entry& e){auto w=packet(kind,e.metadata.id);w.number(e.done);return w.data;}
    void room(){
        size_t active=0;for(auto& p:entries_)if(!terminal(p.second.state))++active;
        for(auto& p:entries_)if(p.second.state==State::Suspended)++active;
        if(active>=MaxTransfers)throw std::runtime_error("File transfer limit reached");
        if(entries_.size()>=MaxRecords){
            auto oldest=entries_.end();
            for(auto i=entries_.begin();i!=entries_.end();++i)if(terminal(i->second.state)&&i->second.state!=State::Suspended&&(oldest==entries_.end()||i->second.lastActivity<oldest->second.lastActivity))oldest=i;
            if(oldest==entries_.end())throw std::runtime_error("File transfer record limit reached");
            entries_.erase(oldest);
        }
    }
    void fail(Entry& e,const char* error){
        e.state=State::Failed;e.error=error;
        try{if(e.sink)e.sink->discard();}catch(...){e.error="Owned partial cleanup failed; manual attention required";}
        e.sink.reset();e.source.reset();e.hash.reset();
        e.reply=packet(Kind::Cancel,e.metadata.id).data;
    }
public:
    explicit TransferManager(std::function<std::unique_ptr<Hash>()> hash,StateStore* store=nullptr):hash_(std::move(hash)),store_(store){if(!hash_)throw std::runtime_error("Hash provider unavailable");}
    void restore(std::uint64_t now){
        if(!store_||!entries_.empty())throw std::runtime_error("Invalid transfer restore");
        auto records=store_->load();if(records.size()>MaxRecords)throw std::runtime_error("Transfer journal limit");
        size_t active=0;
        for(auto& d:records){
            if(!protocol::id(d.metadata.id,32)||!protocol::id(d.peer)||!safeName(d.metadata.name)||d.metadata.size>MaxFileBytes||
                d.done>d.metadata.size||d.highWaterSent>d.metadata.size||d.state>State::Suspended||!d.created||d.updated<d.created||
                d.expires<d.created||d.expires-d.created>RetentionMilliseconds||entries_.count(d.metadata.id))throw std::runtime_error("Invalid protected transfer state");
            Entry e;static_cast<Snapshot&>(e)=d;e.location=d.location;e.highWaterSent=d.highWaterSent;
            e.created=d.created;e.updated=d.updated;e.expires=d.expires;e.digestReady=d.digestReady;e.lastActivity=now;
            bool resumable=!terminal(d.state)||d.state==State::Suspended;
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
    std::vector<Snapshot> snapshots()const{std::vector<Snapshot> result;for(auto& p:entries_)result.push_back(p.second);return result;}
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
        e.sending=true;e.lastActivity=now;e.hash=hash_();if(!e.hash)throw std::runtime_error("Hash provider unavailable");e.source=std::move(source);touch(e);entries_.emplace(id,std::move(e));save();
    }
    void accept(const std::string& id,std::unique_ptr<Sink> sink,std::uint64_t now){
        auto i=entries_.find(id);if(i==entries_.end()||i->second.sending||i->second.state!=State::AwaitingAcceptance||!sink)throw std::runtime_error("File offer no longer available");
        auto& e=i->second;e.hash=hash_();if(!e.hash)throw std::runtime_error("Hash provider unavailable");e.sink=std::move(sink);e.state=State::Transferring;e.lastActivity=now;e.reply=offsetPacket(Kind::Resume,e);touch(e);save();
    }
    void cancel(const std::string& id){
        auto i=entries_.find(id);if(i==entries_.end()||(terminal(i->second.state)&&i->second.state!=State::Suspended))return;
        auto& e=i->second;e.state=State::Cancelled;if(e.sink)e.sink->discard();e.sink.reset();e.source.reset();e.hash.reset();e.reply=packet(Kind::Cancel,id).data;touch(e);save();
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
        if(now<persistenceRetryAt_)return;
        try{tickImplScoped(now,route,send);persistenceRetryAt_=0;}
        catch(const PersistenceError&){persistenceRetryAt_=now+250;throw;}
    }
    void tickImplScoped(std::uint64_t now,const std::function<std::uint64_t(const Snapshot&)>& route,const std::function<bool(const Snapshot&,const Blob&)>& send){
        // Every acknowledgement/offer is gated by durable metadata. A failed
        // journal write propagates to the adapter; never ACK uncommitted progress.
        auto durableSend=[&](const Snapshot& entry,const Blob& frame){save();return send(entry,frame);};
        for(auto& p:entries_){
            auto& e=p.second;
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
                    }else{e.state=State::Transferring;e.reply=offsetPacket(Kind::Resume,e);}
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
                if(e.reply){if(durableSend(e,*e.reply))e.reply.reset();continue;}
                if(terminal(e.state)||!e.sending)continue;
                if(e.route!=current){
                    auto w=packet(Kind::Offer,e.metadata.id);w.string(e.metadata.name);w.number(e.metadata.size);w.bytes(Blob(e.metadata.digest.begin(),e.metadata.digest.end()));
                    if(durableSend(e,w.data)){++e.routeChanges;e.route=current;e.ready=false;e.finishSent=false;e.sentUntil=e.done;}
                    continue;
                }
                if(!e.ready||e.sentUntil!=e.done){++e.ackWaitTicks;continue;}
                if(e.done<e.metadata.size){
                    Blob b;{Timer timer(e.readUs);b=e.source->read(e.done,static_cast<size_t>(std::min<std::uint64_t>(ChunkBytes,e.metadata.size-e.done)));}
                    if(b.empty()||b.size()>ChunkBytes||b.size()>e.metadata.size-e.done)throw std::runtime_error("Source changed or unreadable");
                    auto w=packet(Kind::Chunk,e.metadata.id);w.number(e.done);w.bytes(b);
                    e.highWaterSent=std::max(e.highWaterSent,e.done+b.size());touch(e);
                    if(durableSend(e,w.data)){e.sentUntil=e.done+b.size();e.payloadBytes+=b.size();if(!e.firstPayloadAt)e.firstPayloadAt=now;}else ++e.backpressure;
                }else if(!e.finishSent){auto w=packet(Kind::Finish,e.metadata.id);if(durableSend(e,w.data))e.finishSent=true;}
            }catch(const PersistenceError&){throw;} // Retry persistence on the existing next tick; never ACK it early.
            catch(const std::exception& error){if(store_)suspend(e,error.what());else fail(e,"Local file I/O or hash failure");}
        }
        save();
    }
    // Caller must verify TLS identity, purpose, negotiated capability and winner
    // BEFORE dispatch. Peer ID is never obtained from the transfer payload.
    void receive(const std::string& peer,const Blob& frame,std::uint64_t now){receiveImpl(peer,frame,now);save();}
    void receiveImpl(const std::string& peer,const Blob& frame,std::uint64_t now){
        protocol::Reader r(frame);protocol::check(r.type()==protocol::FileTransfer&&r.number()==1);
        auto kind=static_cast<Kind>(r.number());auto id=r.string();protocol::check(protocol::id(id,32));
        auto i=entries_.find(id);
        if(kind==Kind::Offer){
            Metadata m;m.id=id;m.name=r.string();m.size=r.number();auto digest=r.bytes();r.end();
            protocol::check(safeName(m.name)&&m.size<=MaxFileBytes&&digest.size()==32);
            std::copy(digest.begin(),digest.end(),m.digest.begin());
            if(i!=entries_.end()){
                auto& e=i->second;protocol::check(e.peer==peer&&!e.sending&&e.metadata==m);
                e.lastActivity=now;
                // A restarted sender must first learn the durable full offset,
                // then send Finish before receiving Complete. Do not bypass
                // its existing completion ordering with an unsolicited ACK.
                if(e.state==State::Complete)e.reply=offsetPacket(Kind::Resume,e);
                else if(e.state==State::Transferring)e.reply=offsetPacket(Kind::Resume,e);
                else if(terminal(e.state))e.reply=packet(Kind::Cancel,id).data;
                return;
            }
            protocol::check(protocol::id(peer));room();Entry e;e.metadata=m;e.peer=peer;e.state=State::AwaitingAcceptance;e.lastActivity=now;e.digestReady=true;touch(e);entries_.emplace(id,std::move(e));return;
        }
        if(kind==Kind::Cancel&&i==entries_.end()){r.end();return;} // Cancellation can race an unobserved offer.
        protocol::check(i!=entries_.end()&&i->second.peer==peer);auto& e=i->second;
        if(kind==Kind::Resume){
            auto offset=r.number();r.end();protocol::check(e.sending&&offset<=e.metadata.size&&offset<=e.highWaterSent);
            if(e.state==State::Recovering)return; // No peer event may bypass full source revalidation.
            if(terminal(e.state))return;
            e.done=offset;e.sentUntil=offset;e.ready=true;e.finishSent=false;e.state=State::Transferring;e.lastActivity=now;e.lastAckAt=now;return;
        }
        if(kind==Kind::Cancel){r.end();if(!terminal(e.state)||e.state==State::Suspended){e.state=State::Cancelled;try{if(e.sink)e.sink->discard();}catch(...){e.error="Owned partial cleanup failed; manual attention required";}e.sink.reset();e.source.reset();e.hash.reset();}e.reply.reset();return;}
        if(kind==Kind::Complete){r.end();protocol::check(e.sending&&e.done==e.metadata.size&&e.finishSent);e.state=State::Complete;e.source.reset();return;}
        protocol::check(!e.sending);
        if(kind==Kind::Chunk){
            auto offset=r.number();auto b=r.bytes();r.end();protocol::check(!b.empty()&&b.size()<=ChunkBytes&&offset<=e.metadata.size&&b.size()<=e.metadata.size-offset);
            if(e.state==State::Complete){e.reply=packet(Kind::Complete,id).data;return;}
            if(e.state==State::Cancelled||e.state==State::Failed){e.reply=packet(Kind::Cancel,id).data;return;}
            protocol::check(e.state==State::Transferring&&e.sink&&e.hash);
            if(offset<e.done){protocol::check(offset+b.size()<=e.done);e.reply=offsetPacket(Kind::Resume,e);return;}
            protocol::check(offset==e.done);
            try{
                {Timer timer(e.writeUs);e.sink->append(b);}
                {Timer timer(e.flushUs);e.sink->flush();}
                {Timer timer(e.hashUs);e.hash->update(b.data(),b.size());}
                e.payloadBytes+=b.size();if(!e.observedAt)e.observedAt=now;if(!e.firstPayloadAt)e.firstPayloadAt=now;
                e.done+=b.size();e.lastActivity=now;touch(e);e.reply=offsetPacket(Kind::Resume,e);
            }catch(...){if(store_)suspend(e,"receive disk or hash failure");else fail(e,"Receive disk or hash failure");}
            return;
        }
        if(kind==Kind::Finish){
            r.end();if(e.state==State::Complete){e.reply=packet(Kind::Complete,id).data;return;}
            if(e.state==State::Cancelled||e.state==State::Failed){e.reply=packet(Kind::Cancel,id).data;return;}
            protocol::check(e.state==State::Transferring&&e.done==e.metadata.size&&e.hash&&e.sink);
            try{
                if(e.hash->finish()!=e.metadata.digest){fail(e,"File integrity verification failed");return;}
                {Timer timer(e.commitUs);e.sink->commit();}e.sink.reset();e.hash.reset();e.state=State::Complete;e.reply=packet(Kind::Complete,id).data;
            }catch(const CompletionError& error){if(store_)suspend(e,error.what());else fail(e,error.what());}
            catch(...){if(store_)suspend(e,"atomic file completion failed");else fail(e,"Atomic file completion failed");}
            return;
        }
        throw std::runtime_error("Unknown file control");
    }
};
}
