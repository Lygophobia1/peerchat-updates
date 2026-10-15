#pragma once
#include "group.hpp"
#include "protocol.hpp"

namespace peerchat::group_history {
// Local protected evidence, not a peer-supplied roster or a historical key grant.
// Capture only after the existing authenticated roster matches the signed ledger.
struct Epoch {
    std::string server,local;
    std::uint64_t generation=0;
    std::map<std::string,GroupMember> devices;
};
inline std::string evidenceId(const std::string& server,std::uint64_t generation){
    protocol::check(protocol::id(server,32)&&generation>0&&generation<=INT64_MAX);
    protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/GroupEpochContext/v1");w.string(server);w.number(generation);
    auto hash=digestDevicePayload(w.data);std::string id;static const char hex[]="0123456789abcdef";
    for(auto b:hash){id+=hex[b>>4];id+=hex[b&15];}return id;
}
inline Blob encodeEpoch(const Epoch& e){
    protocol::check(protocol::id(e.local,32)&&protocol::id(e.server,32)&&e.generation>0&&e.generation<=INT64_MAX&&!e.devices.empty()&&e.devices.size()<=256);
    protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/GroupEpochEvidence/v1");w.string(e.server);w.string(e.local);w.number(e.generation);w.number(e.devices.size());
    for(const auto& row:e.devices){const auto& m=row.second;
        protocol::check(row.first==m.peerId&&protocol::id(m.peerId)&&m.accountId>0&&m.accountId<=INT64_MAX&&protocol::id(m.membershipId)&&m.publicKey.size()==72&&Storage::publicKeyId(m.publicKey)==m.peerId);
        w.string(m.peerId);w.number(m.accountId);w.bytes(m.publicKey);w.string(m.membershipId);
    }
    protocol::check(w.data.size()<=128*1024);return w.data;
}
inline Epoch decodeEpoch(const Blob& bytes){
    protocol::check(bytes.size()<=128*1024);protocol::Reader r(bytes);protocol::check(r.type()==protocol::GroupEvent&&r.string()=="PeerChat/GroupEpochEvidence/v1");
    Epoch e;e.server=r.string();e.local=r.string();e.generation=r.number();auto count=r.number();protocol::check(count>0&&count<=256);
    for(std::uint64_t n=0;n<count;++n){GroupMember m;m.peerId=r.string();m.accountId=r.number();m.publicKey=r.bytes();m.membershipId=r.string();protocol::check(e.devices.emplace(m.peerId,m).second);}
    r.end();protocol::check(encodeEpoch(e)==bytes);return e;
}
inline void captureEpoch(Storage& store,const std::string& server,std::uint64_t generation,const GroupState& verified){
    protocol::check(verified.created&&protocol::id(verified.groupId,32));Epoch e;e.server=server;e.local=verified.groupId;e.generation=generation;
    for(const auto& row:verified.members)e.devices.emplace(row.second.peerId,row.second);
    for(const auto& row:verified.authorizedDevices)protocol::check(e.devices.emplace(row.first,row.second).second);
    auto id=evidenceId(server,generation);auto saved=store.archiveSyncState(id);auto bytes=encodeEpoch(e);
    if(!saved.empty()){protocol::check(saved==bytes);return;}
    store.saveArchiveSyncState(id,{},bytes);
}
inline std::optional<Epoch> retainedEpoch(Storage& store,const std::string& server,std::uint64_t generation){
    auto saved=store.archiveSyncState(evidenceId(server,generation));if(saved.empty())return {};
    auto e=decodeEpoch(saved);protocol::check(e.server==server&&e.generation==generation);return e;
}
inline bool continuousMember(const Epoch& old,const GroupState& current,std::uint64_t account,const std::string& device){
    auto previous=old.devices.find(device);auto now=current.device(account,device);
    return old.local==current.groupId&&previous!=old.devices.end()&&now&&previous->second.accountId==account&&
        now->publicKey==previous->second.publicKey&&!now->membershipId.empty()&&now->membershipId==previous->second.membershipId;
}
}
