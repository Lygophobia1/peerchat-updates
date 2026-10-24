#pragma once
#include "../storage.hpp"
#include "../transfer_manager.hpp"

namespace peerchat::transfer {
// Isolated protected supplemental records. Legacy peer/DM journals remain
// byte-compatible and never receive the new local-only purpose value.
class GroupCacheJournal final:public StateStore {
 Storage& storage_;StateStore& ordinary_;std::string device_;Blob previous_;bool loaded_=false;
 std::function<std::unique_ptr<Source>(const DurableState&)> source_;
 std::function<std::unique_ptr<Sink>(const DurableState&)> sink_;
 static void valid(const DurableState& d){protocol::check(d.purpose==Purpose::GroupCacheCiphertext&&protocol::id(d.metadata.id,32)&&protocol::id(d.peer)&&d.metadata.name=="ciphertext.peerchat"&&d.metadata.size>=36&&d.metadata.size<=MaxCacheCipherBytes&&d.done<=d.metadata.size&&d.highWaterSent<=d.metadata.size&&d.state<=State::Suspended&&d.created&&d.updated>=d.created&&d.expires>=d.created&&d.expires-d.created<=RetentionMilliseconds&&d.location.size()<=2048&&d.error.size()<=256);}
 static void write(protocol::Writer& w,const DurableState& d){valid(d);w.string(d.metadata.id);w.string(d.peer);w.number(d.metadata.size);w.bytes(Blob(d.metadata.digest.begin(),d.metadata.digest.end()));w.number(static_cast<unsigned>(d.state));w.number(d.sending);w.number(d.digestReady);w.number(d.done);w.number(d.highWaterSent);w.number(d.created);w.number(d.updated);w.number(d.expires);w.bytes(d.location);w.string(d.error);}
 static DurableState read(protocol::Reader& r){DurableState d;d.purpose=Purpose::GroupCacheCiphertext;d.metadata.name="ciphertext.peerchat";d.metadata.id=r.string();d.peer=r.string();d.metadata.size=r.number();auto digest=r.bytes();protocol::check(digest.size()==32);std::copy(digest.begin(),digest.end(),d.metadata.digest.begin());auto state=r.number(),sending=r.number(),ready=r.number();protocol::check(state<=static_cast<unsigned>(State::Suspended)&&sending<=1&&ready<=1);d.state=static_cast<State>(state);d.sending=sending;d.digestReady=ready;d.done=r.number();d.highWaterSent=r.number();d.created=r.number();d.updated=r.number();d.expires=r.number();d.location=r.bytes();d.error=r.string();valid(d);return d;}
public:
 GroupCacheJournal(Storage& storage,std::string device,StateStore& ordinary,decltype(source_) source,decltype(sink_) sink):storage_(storage),ordinary_(ordinary),device_(std::move(device)),source_(std::move(source)),sink_(std::move(sink)){protocol::check(protocol::id(device_));}
 std::uint64_t wallTime()const override{return ordinary_.wallTime();}
 bool supports(Purpose p)const override{return p==Purpose::GroupCacheCiphertext||ordinary_.supports(p);}
 std::map<std::string,Options> loadOptions()override{return ordinary_.loadOptions();}
 void saveOptions(const std::map<std::string,Options>& rows)override{ordinary_.saveOptions(rows);}
 std::optional<std::uint64_t> loadDownloadLimit()override{return ordinary_.loadDownloadLimit();}
 void saveDownloadLimit(std::uint64_t bits)override{ordinary_.saveDownloadLimit(bits);}
 std::vector<DurableState> load()override{
  auto records=ordinary_.load();for(const auto& d:records)protocol::check(d.purpose!=Purpose::GroupCacheCiphertext);
  previous_=storage_.groupAttachmentTransfers(device_);if(!previous_.empty()){
   protocol::check(previous_.size()<=262144);protocol::Reader r(previous_);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/GroupAttachmentTransfers/v1"&&r.string()==device_);auto count=r.number();protocol::check(count<=MaxRecords);
   while(count--){auto d=read(r);protocol::check(std::none_of(records.begin(),records.end(),[&](const auto& x){return x.metadata.id==d.metadata.id;}));records.push_back(std::move(d));}r.end();
  }protocol::check(records.size()<=MaxRecords);loaded_=true;return records;
 }
 void save(const std::vector<DurableState>& rows)override{
  protocol::check(loaded_&&rows.size()<=MaxRecords);std::vector<DurableState> ordinary,direct;for(const auto& d:rows)(d.purpose==Purpose::GroupCacheCiphertext?direct:ordinary).push_back(d);
  protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/GroupAttachmentTransfers/v1");w.string(device_);w.number(direct.size());for(const auto& d:direct)write(w,d);protocol::check(w.data.size()<=262144);
  ordinary_.save(ordinary);if(w.data!=previous_){storage_.saveGroupAttachmentTransfers(device_,previous_,w.data);previous_=std::move(w.data);}
 }
 std::unique_ptr<Source> source(const DurableState& d)override{return d.purpose==Purpose::GroupCacheCiphertext?source_(d):ordinary_.source(d);}
 std::unique_ptr<Sink> sink(const DurableState& d)override{return d.purpose==Purpose::GroupCacheCiphertext?sink_(d):ordinary_.sink(d);}
};
}
