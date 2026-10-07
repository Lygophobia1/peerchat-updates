#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>

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
};
class Sink {
public:
    virtual ~Sink()=default; // Abandon/delete only the owned temporary file.
    virtual void append(const Blob&)=0;
    virtual void commit()=0; // Flush + no-overwrite atomic rename.
};
enum class State { Preparing, Offered, AwaitingAcceptance, Transferring, Complete, Cancelled, Failed };
enum class Kind:std::uint64_t { Offer=1, Resume=2, Chunk=3, Finish=4, Complete=5, Cancel=6 };
struct Metadata {
    std::string id,name;
    std::uint64_t size=0;
    Digest digest{};
    bool operator==(const Metadata& b)const{return id==b.id&&name==b.name&&size==b.size&&digest==b.digest;}
};
struct Snapshot { Metadata metadata;std::string peer,error;State state=State::Preparing;bool sending=false;std::uint64_t done=0; };
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
    struct Entry:Snapshot {
        std::unique_ptr<Source> source;
        std::unique_ptr<Sink> sink;
        std::unique_ptr<Hash> hash;
        std::uint64_t prepared=0,lastActivity=0,route=0,sentUntil=0,highWaterSent=0;
        bool ready=false,finishSent=false;
        std::optional<Blob> reply;
    };
    std::map<std::string,Entry> entries_;
    std::function<std::unique_ptr<Hash>()> hash_;
    static bool terminal(State s){return s==State::Complete||s==State::Cancelled||s==State::Failed;}
    static protocol::Writer packet(Kind kind,const std::string& id){protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(static_cast<std::uint64_t>(kind));w.string(id);return w;}
    static Blob offsetPacket(Kind kind,const Entry& e){auto w=packet(kind,e.metadata.id);w.number(e.done);return w.data;}
    void room(){
        size_t active=0;for(auto& p:entries_)if(!terminal(p.second.state))++active;
        if(active>=MaxTransfers)throw std::runtime_error("File transfer limit reached");
        if(entries_.size()>=MaxRecords){
            auto oldest=entries_.end();
            for(auto i=entries_.begin();i!=entries_.end();++i)if(terminal(i->second.state)&&(oldest==entries_.end()||i->second.lastActivity<oldest->second.lastActivity))oldest=i;
            if(oldest==entries_.end())throw std::runtime_error("File transfer record limit reached");
            entries_.erase(oldest);
        }
    }
    void fail(Entry& e,const char* error){
        e.state=State::Failed;e.error=error;e.sink.reset();e.source.reset();e.hash.reset();
        e.reply=packet(Kind::Cancel,e.metadata.id).data;
    }
public:
    explicit TransferManager(std::function<std::unique_ptr<Hash>()> hash):hash_(std::move(hash)){if(!hash_)throw std::runtime_error("Hash provider unavailable");}
    std::vector<Snapshot> snapshots()const{std::vector<Snapshot> result;for(auto& p:entries_)result.push_back(p.second);return result;}
    std::vector<std::string> demandedPeers()const{
        std::vector<std::string> result;for(auto& p:entries_)if(!terminal(p.second.state)||p.second.reply)result.push_back(p.second.peer);return result;
    }
    void offer(const std::string& peer,const std::string& id,std::unique_ptr<Source> source,std::uint64_t now){
        if(!protocol::id(peer)||!protocol::id(id,32)||!source||!safeName(source->name())||source->size()>MaxFileBytes||entries_.count(id))throw std::runtime_error("Invalid file offer");
        room();Entry e;e.peer=peer;e.metadata.id=id;e.metadata.name=source->name();e.metadata.size=source->size();e.sending=true;e.lastActivity=now;e.hash=hash_();if(!e.hash)throw std::runtime_error("Hash provider unavailable");e.source=std::move(source);entries_.emplace(id,std::move(e));
    }
    void accept(const std::string& id,std::unique_ptr<Sink> sink,std::uint64_t now){
        auto i=entries_.find(id);if(i==entries_.end()||i->second.sending||i->second.state!=State::AwaitingAcceptance||!sink)throw std::runtime_error("File offer no longer available");
        auto& e=i->second;e.hash=hash_();if(!e.hash)throw std::runtime_error("Hash provider unavailable");e.sink=std::move(sink);e.state=State::Transferring;e.lastActivity=now;e.reply=offsetPacket(Kind::Resume,e);
    }
    void cancel(const std::string& id){
        auto i=entries_.find(id);if(i==entries_.end()||terminal(i->second.state))return;
        auto& e=i->second;e.state=State::Cancelled;e.sink.reset();e.source.reset();e.hash.reset();e.reply=packet(Kind::Cancel,id).data;
    }
    void unsupportedPeer(const std::string& peer){
        for(auto& p:entries_)if(p.second.peer==peer&&p.second.sending&&!terminal(p.second.state)){fail(p.second,"Recipient does not support file transfers");p.second.reply.reset();}
    }
    // One bounded hash/read/packet operation per transfer per tick. A route token
    // changes only with SessionTransportManager's winning authenticated session.
    void tick(std::uint64_t now,const std::function<std::uint64_t(const std::string&)>& route,const std::function<bool(const std::string&,const Blob&)>& send){
        for(auto& p:entries_){
            auto& e=p.second;
            if(!terminal(e.state)&&now-e.lastActivity>IdleMilliseconds){fail(e,"File transfer expired");}
            if(terminal(e.state)&&now-e.lastActivity>IdleMilliseconds)e.reply.reset();
            try{
                if(e.state==State::Preparing){
                    if(e.prepared<e.metadata.size){
                        auto b=e.source->read(e.prepared,static_cast<size_t>(std::min<std::uint64_t>(ChunkBytes,e.metadata.size-e.prepared)));
                        if(b.empty()||b.size()>ChunkBytes||b.size()>e.metadata.size-e.prepared)throw std::runtime_error("Source read failed");
                        e.hash->update(b.data(),b.size());e.prepared+=b.size();e.lastActivity=now;continue;
                    }
                    e.metadata.digest=e.hash->finish();e.hash.reset();e.state=State::Offered;
                }
                auto current=route(e.peer);if(!current){e.route=0;e.ready=false;continue;}
                if(e.reply){if(send(e.peer,*e.reply))e.reply.reset();continue;}
                if(terminal(e.state)||!e.sending)continue;
                if(e.route!=current){
                    auto w=packet(Kind::Offer,e.metadata.id);w.string(e.metadata.name);w.number(e.metadata.size);w.bytes(Blob(e.metadata.digest.begin(),e.metadata.digest.end()));
                    if(send(e.peer,w.data)){e.route=current;e.ready=false;e.finishSent=false;e.sentUntil=e.done;}
                    continue;
                }
                if(!e.ready||e.sentUntil!=e.done)continue;
                if(e.done<e.metadata.size){
                    auto b=e.source->read(e.done,static_cast<size_t>(std::min<std::uint64_t>(ChunkBytes,e.metadata.size-e.done)));
                    if(b.empty()||b.size()>ChunkBytes||b.size()>e.metadata.size-e.done)throw std::runtime_error("Source changed or unreadable");
                    auto w=packet(Kind::Chunk,e.metadata.id);w.number(e.done);w.bytes(b);
                    if(send(e.peer,w.data)){e.sentUntil=e.done+b.size();e.highWaterSent=std::max(e.highWaterSent,e.sentUntil);}
                }else if(!e.finishSent){auto w=packet(Kind::Finish,e.metadata.id);if(send(e.peer,w.data))e.finishSent=true;}
            }catch(...){fail(e,"Local file I/O or hash failure");}
        }
    }
    // Caller must verify TLS identity, purpose, negotiated capability and winner
    // BEFORE dispatch. Peer ID is never obtained from the transfer payload.
    void receive(const std::string& peer,const Blob& frame,std::uint64_t now){
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
                if(e.state==State::Complete)e.reply=packet(Kind::Complete,id).data;
                else if(e.state==State::Transferring)e.reply=offsetPacket(Kind::Resume,e);
                else if(terminal(e.state))e.reply=packet(Kind::Cancel,id).data;
                return;
            }
            protocol::check(protocol::id(peer));room();Entry e;e.metadata=m;e.peer=peer;e.state=State::AwaitingAcceptance;e.lastActivity=now;entries_.emplace(id,std::move(e));return;
        }
        if(kind==Kind::Cancel&&i==entries_.end()){r.end();return;} // Cancellation can race an unobserved offer.
        protocol::check(i!=entries_.end()&&i->second.peer==peer);auto& e=i->second;
        if(kind==Kind::Resume){
            auto offset=r.number();r.end();protocol::check(e.sending&&offset<=e.metadata.size&&offset<=e.highWaterSent);
            if(terminal(e.state))return;
            e.done=offset;e.sentUntil=offset;e.ready=true;e.finishSent=false;e.state=State::Transferring;e.lastActivity=now;return;
        }
        if(kind==Kind::Cancel){r.end();if(!terminal(e.state)){e.state=State::Cancelled;e.sink.reset();e.source.reset();e.hash.reset();}e.reply.reset();return;}
        if(kind==Kind::Complete){r.end();protocol::check(e.sending&&e.done==e.metadata.size&&e.finishSent);e.state=State::Complete;e.source.reset();return;}
        protocol::check(!e.sending);
        if(kind==Kind::Chunk){
            auto offset=r.number();auto b=r.bytes();r.end();protocol::check(!b.empty()&&b.size()<=ChunkBytes&&offset<=e.metadata.size&&b.size()<=e.metadata.size-offset);
            if(e.state==State::Complete){e.reply=packet(Kind::Complete,id).data;return;}
            if(e.state==State::Cancelled||e.state==State::Failed){e.reply=packet(Kind::Cancel,id).data;return;}
            protocol::check(e.state==State::Transferring&&e.sink&&e.hash);
            if(offset<e.done){protocol::check(offset+b.size()<=e.done);e.reply=offsetPacket(Kind::Resume,e);return;}
            protocol::check(offset==e.done);
            try{e.sink->append(b);e.hash->update(b.data(),b.size());e.done+=b.size();e.lastActivity=now;e.reply=offsetPacket(Kind::Resume,e);}catch(...){fail(e,"Receive disk or hash failure");}
            return;
        }
        if(kind==Kind::Finish){
            r.end();if(e.state==State::Complete){e.reply=packet(Kind::Complete,id).data;return;}
            if(e.state==State::Cancelled||e.state==State::Failed){e.reply=packet(Kind::Cancel,id).data;return;}
            protocol::check(e.state==State::Transferring&&e.done==e.metadata.size&&e.hash&&e.sink);
            try{
                if(e.hash->finish()!=e.metadata.digest){fail(e,"File integrity verification failed");return;}
                e.sink->commit();e.sink.reset();e.hash.reset();e.state=State::Complete;e.reply=packet(Kind::Complete,id).data;
            }catch(...){fail(e,"Atomic file completion failed");}
            return;
        }
        throw std::runtime_error("Unknown file control");
    }
};
}
