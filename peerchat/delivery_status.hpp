#pragma once
#include "storage.hpp"
#include "protocol.hpp"
#include <map>

namespace peerchat::delivery_status {
// Read-only presentation of the existing protected target manifest. No key,
// lease, signature or server assertion is exported as UI delivery authority.
struct Target { std::string device; DeliveryTargetStatus status; };
struct Snapshot { bool selectionUnresolved=false; std::vector<Target> targets; };
using Snapshots=std::map<std::string,Snapshot>;
inline Snapshots read(Storage& store,const std::string& local,const std::vector<StoredMessage>& messages){
    Snapshots result;
    for(const auto& message:messages){
        if(message.sender!=local)continue; // History import is not a live receipt.
        auto manifest=store.deliveryTargets(message.id);if(!manifest)continue;
        Snapshot snapshot;snapshot.selectionUnresolved=manifest->selectionUnresolved;
        for(const auto& target:manifest->targets)snapshot.targets.push_back({target.device,target.status});
        result.emplace(message.id,std::move(snapshot));
    }
    return result;
}
// Additive local JNI page extension, never a network message or storage format.
inline void append(protocol::Writer& record,const Snapshots& snapshots){
    record.string("PeerChat/DeliveryTargetsSnapshot/v1");record.number(snapshots.size());
    for(const auto& item:snapshots){record.string(item.first);record.number(item.second.selectionUnresolved?1:0);record.number(item.second.targets.size());
        for(const auto& target:item.second.targets){record.string(target.device);record.number(static_cast<unsigned>(target.status));}}
}
}
