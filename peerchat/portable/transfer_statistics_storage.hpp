#pragma once
#include "../storage.hpp"
#include "../transfer_manager.hpp"
#include "../group.hpp"

namespace peerchat::transfer {
// Optional, protected local telemetry. Existing resume journal/wire stay intact.
class ProtectedStatisticsStore final:public StatisticsStateStore {
    Storage& storage_;std::string device_;std::map<std::string,Blob> previous_;struct CompletedValue {
        std::string peer;bool sending;std::uint64_t size,bytes,duration;
        explicit CompletedValue(const DurableState& d):peer(d.peer),sending(d.sending),size(d.metadata.size),bytes(d.statisticsBytes),duration(d.durationMilliseconds){}
        bool matches(const DurableState& d)const{return peer==d.peer&&sending==d.sending&&size==d.metadata.size&&bytes==d.statisticsBytes&&duration==d.durationMilliseconds;}
    };
    std::map<std::string,CompletedValue> completed_;std::uint64_t retryAt_=0,encodingCalls_=0;
    std::string key(const std::string& id)const{
        protocol::Writer w(static_cast<protocol::Type>(31));w.string("PeerChat/TransferStatisticsContext/v1");w.string(device_);w.string(id);
        auto digest=digestDevicePayload(w.data);static const char hex[]="0123456789abcdef";std::string result;for(auto b:digest){result+=hex[b>>4];result+=hex[b&15];}return result;
    }
    Blob encode(const DurableState& d){
        ++encodingCalls_;
        protocol::check(d.statisticsBytes<=d.metadata.size&&d.durationMilliseconds<=RetentionMilliseconds);
        protocol::Writer w(static_cast<protocol::Type>(31));w.string("PeerChat/CompletedTransferStatistics/v1");w.string(device_);w.string(d.metadata.id);w.string(d.peer);w.number(d.sending);w.number(d.metadata.size);w.number(d.statisticsBytes);w.number(d.durationMilliseconds);w.number(static_cast<unsigned>(State::Complete));return w.data;
    }
public:
    ProtectedStatisticsStore(Storage& storage,std::string device):storage_(storage),device_(std::move(device)){}
    std::uint64_t encodingCalls()const{return encodingCalls_;}
    void load(std::vector<DurableState>& records)override{
        for(auto& d:records)try{
            completed_.erase(d.metadata.id);previous_.erase(d.metadata.id);
            auto bytes=storage_.archiveSyncState(key(d.metadata.id));previous_[d.metadata.id]=bytes;if(bytes.empty())continue;
            protocol::Reader r(bytes);protocol::check(r.type()==static_cast<protocol::Type>(31)&&r.string()=="PeerChat/CompletedTransferStatistics/v1"&&r.string()==device_&&r.string()==d.metadata.id&&r.string()==d.peer&&r.number()==static_cast<unsigned>(d.sending)&&r.number()==d.metadata.size);
            const auto counted=r.number(),duration=r.number();protocol::check(r.number()==static_cast<unsigned>(State::Complete));r.end();protocol::check(d.state==State::Complete&&counted==d.metadata.size&&duration<=RetentionMilliseconds);d.statisticsBytes=counted;d.durationMilliseconds=duration;d.statisticsKnown=true;completed_.insert_or_assign(d.metadata.id,CompletedValue(d));
        }catch(...){d.statisticsKnown=false;d.currentBitsPerSecond=0;}
    }
    void save(const std::vector<DurableState>& records)override{
        const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());if(now<retryAt_)return;
        try{
        for(const auto& d:records)if(d.statisticsKnown&&d.state==State::Complete){
            // Completed summaries are immutable until one encoded field changes.
            // Cache only validated/committed values; live progress is not stored.
            auto completed=completed_.find(d.metadata.id);if(completed!=completed_.end()&&completed->second.matches(d))continue;
            auto bytes=encode(d);auto found=previous_.find(d.metadata.id);if(found==previous_.end())found=previous_.emplace(d.metadata.id,storage_.archiveSyncState(key(d.metadata.id))).first;
            if(found->second==bytes){completed_.insert_or_assign(d.metadata.id,CompletedValue(d));continue;}
            storage_.saveArchiveSyncState(key(d.metadata.id),found->second,bytes);found->second=std::move(bytes);completed_.insert_or_assign(d.metadata.id,CompletedValue(d));
        }
        // Runtime cache is bounded; persistent records retain completed results.
        while(previous_.size()>MaxRecords){completed_.erase(previous_.begin()->first);previous_.erase(previous_.begin());}
        }catch(...){retryAt_=now+1000;throw;}
    }
};
}
