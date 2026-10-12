#pragma once
#include "group.hpp"
#include "protocol.hpp"

namespace peerchat::group_keys {
// Immutable signed creation evidence is not current membership, a history grant,
// or permission to release keys. Current roster and full ledger checks remain.
inline GroupEvent verifiedCreation(const std::string& server,const std::string& legacy,const Blob& wire){
    protocol::check(protocol::id(server,32)&&protocol::id(legacy,32)&&!wire.empty()&&wire.size()<=2048);
    auto event=decodeGroupEvent(wire);
    protocol::check(event.groupId==legacy&&event.kind==GroupEventKind::Create&&event.logicalOrder==1&&
        event.actorAccountId==event.subjectAccountId&&event.actorPeerId==event.subjectPeerId&&event.message.empty());
    GroupLedger root;protocol::check(root.append(event)&&root.state().created);
    auto domain=std::string("PeerChat group roster v1|")+legacy+"|"+event.actorPeerId;
    protocol::check(Storage::publicKeyId(Blob(domain.begin(),domain.end())).substr(0,32)==server);
    return event;
}
}
