#pragma once
#include "../storage.hpp"
#include "../transfer_manager.hpp"
#include <chrono>

namespace peerchat::transfer {
// SQLite/protected-record adapter only. TransferManager owns all persistence
// policy; platform callbacks reopen exactly the recorded source/partial.
class ProtectedStorageJournal final:public StateStore {
    Storage& storage_;std::string device_;Blob previous_;
    std::function<std::unique_ptr<Source>(const DurableState&)> source_;
    std::function<std::unique_ptr<Sink>(const DurableState&)> sink_;
public:
    ProtectedStorageJournal(Storage& storage,std::string device,decltype(source_) source,decltype(sink_) sink)
        :storage_(storage),device_(std::move(device)),source_(std::move(source)),sink_(std::move(sink)){}
    std::uint64_t wallTime()const override{return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
    std::vector<DurableState> load()override{
        previous_=storage_.transferJournal(device_);if(previous_.empty())return {};
        protocol::Reader r(previous_);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/ProtectedTransferJournal/v1"&&r.string()==device_&&r.number()==ChunkBytes);
        auto count=r.number();protocol::check(count<=MaxRecords);std::vector<DurableState> result;
        for(std::uint64_t n=0;n<count;++n){DurableState d;d.metadata.id=r.string();d.peer=r.string();d.metadata.name=r.string();d.metadata.size=r.number();
            auto hash=r.bytes();protocol::check(hash.size()==32);std::copy(hash.begin(),hash.end(),d.metadata.digest.begin());
            auto state=r.number(),sending=r.number(),ready=r.number();protocol::check(state<=static_cast<unsigned>(State::Suspended)&&sending<=1&&ready<=1);
            d.state=static_cast<State>(state);d.sending=sending!=0;d.digestReady=ready!=0;
            d.done=r.number();d.highWaterSent=r.number();d.created=r.number();d.updated=r.number();d.expires=r.number();d.location=r.bytes();d.error=r.string();
            protocol::check(d.location.size()<=2048&&d.error.size()<=256);result.push_back(std::move(d));
        }r.end();return result;
    }
    void save(const std::vector<DurableState>& records)override{
        protocol::check(records.size()<=MaxRecords);protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/ProtectedTransferJournal/v1");w.string(device_);w.number(ChunkBytes);w.number(records.size());
        for(const auto& d:records){protocol::check(d.location.size()<=2048&&d.error.size()<=256);w.string(d.metadata.id);w.string(d.peer);w.string(d.metadata.name);w.number(d.metadata.size);w.bytes(Blob(d.metadata.digest.begin(),d.metadata.digest.end()));
            w.number(static_cast<unsigned>(d.state));w.number(d.sending);w.number(d.digestReady);w.number(d.done);w.number(d.highWaterSent);w.number(d.created);w.number(d.updated);w.number(d.expires);w.bytes(d.location);w.string(d.error);
        }
        if(w.data==previous_)return;
        storage_.saveTransferJournal(device_,previous_,w.data);previous_=std::move(w.data);
    }
    std::unique_ptr<Source> source(const DurableState& d)override{return source_(d);}
    std::unique_ptr<Sink> sink(const DurableState& d)override{return sink_(d);}
};
}
