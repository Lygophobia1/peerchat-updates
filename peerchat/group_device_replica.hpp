#pragma once
#include "group_device_reconciliation.hpp"

namespace peerchat::group_devices {
struct AuthenticatedTransition {
    group_keys::RosterSnapshot before,after;
    Blob proof;
};
// Native replica persistence/admission only. No sockets, retry owner or key release.
// Caller supplies authenticated consecutive server snapshots; activation remains
// default OFF until application session verification and revocation gates pass.
class Replica {
    Storage& store_;
    std::string server_,legacy_;
    bool enabled_;
    static void snapshot(protocol::Writer& w,const group_keys::RosterSnapshot& s){
        protocol::check(s.devices.size()<=256);w.string(s.serverGroupId);w.string(s.legacyGroupId);
        w.string(s.distributorPeerId);w.number(s.generation);w.number(s.devices.size());
        for(const auto& d:s.devices){w.number(d.accountId);w.string(d.peerId);w.number(static_cast<unsigned>(d.role));w.number(d.invited);}
    }
    static group_keys::RosterSnapshot snapshot(protocol::Reader& r){
        group_keys::RosterSnapshot s;s.serverGroupId=r.string();s.legacyGroupId=r.string();s.distributorPeerId=r.string();
        s.generation=r.number();auto count=r.number();protocol::check(count<=256);
        for(std::uint64_t i=0;i<count;++i){group_keys::RosterDevice d;d.accountId=r.number();d.peerId=r.string();
            auto role=r.number(),invited=r.number();protocol::check(role>=1&&role<=3&&invited<=1);
            d.role=static_cast<GroupRole>(role);d.invited=invited!=0;s.devices.push_back(std::move(d));}return s;
    }
    static bool sameSnapshot(const group_keys::RosterSnapshot& a,const group_keys::RosterSnapshot& b){
        auto canonical=[](group_keys::RosterSnapshot value){
            std::sort(value.devices.begin(),value.devices.end(),[](const auto& x,const auto& y){return x.peerId<y.peerId;});
            protocol::Writer w(protocol::GroupEvent);snapshot(w,value);return w.data;
        };
        return canonical(a)==canonical(b);
    }
    static Blob receipt(const AuthenticatedTransition& step){
        protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/group-replica-receipt/v1");
        w.bytes(step.proof);snapshot(w,step.before);snapshot(w,step.after);
        protocol::check(w.data.size()<=65536);return w.data;
    }
    void root(const GroupLedger& ledger)const{
        protocol::check(ledger.state().created&&ledger.state().groupId==legacy_);
        auto event=std::find_if(ledger.events().begin(),ledger.events().end(),[&](const auto& e){return e.kind==GroupEventKind::Create&&ledger.accepted(e.eventId);});
        protocol::check(event!=ledger.events().end());
        auto value=std::string("PeerChat group roster v1|")+legacy_+"|"+event->actorPeerId;
        protocol::check(Storage::publicKeyId(Blob(value.begin(),value.end())).substr(0,32)==server_);
    }
    Reconciliation admit(const group_keys::RosterSnapshot& before,const group_keys::RosterSnapshot& after,const Blob& proof,GroupLedger& ledger)const{
        if(!enabled_)return Reconciliation::LedgerRejected;
        if(before.serverGroupId!=server_||after.serverGroupId!=server_||before.legacyGroupId!=legacy_||after.legacyGroupId!=legacy_)return Reconciliation::WrongGroup;
        root(ledger);SignedApproval parsed;try{parsed=readApproval(proof);}catch(...){return Reconciliation::InvalidProof;}auto candidate=ledger;
        candidate.admittedDeviceEvents_.insert(parsed.event.eventId);
        auto result=reconcileApproval(before,after,proof,candidate);
        if(result==Reconciliation::Accepted){ledger=std::move(candidate);}return result;
    }
public:
    // Existing application roster owner supplies the authenticated snapshot and
    // local ledger. No second session/retry owner and no implicit activation.
    static void applyAuthenticated(Storage& store,group_keys::GroupKeyManager& keys,
                                   group_keys::RosterSnapshot incoming,GroupLedger* ledger,bool enabled=false,
                                   const std::vector<AuthenticatedTransition>& recovery={}){
        const std::string server=incoming.serverGroupId;
        auto old=keys.roster(server);group_keys::RosterSnapshot before;if(old)before=*old;
        try{
            incoming=keys.checked(std::move(incoming));
            auto exactRoot=[&](const GroupLedger& verified){
                if(incoming.creationProof.empty())return;
                auto creation=std::find_if(verified.events().begin(),verified.events().end(),[&](const auto& event){return event.kind==GroupEventKind::Create&&verified.accepted(event.eventId);});
                protocol::check(creation!=verified.events().end()&&encodeGroupEvent(*creation)==incoming.creationProof);
            };
            if(ledger&&ledger->state().created)exactRoot(*ledger);
            auto memberships=[](const group_keys::RosterSnapshot& snapshot){
                std::map<std::uint64_t,std::set<std::string>> result;
                for(const auto& d:snapshot.devices)if(!d.invited)result[d.accountId].insert(d.peerId);
                return result;
            };
            auto afterMembers=memberships(incoming);bool deviceChange=false;
            if(old){
                auto previous=memberships(before);
                for(const auto& account:afterMembers){auto found=previous.find(account.first);
                    if(found!=previous.end()?found->second!=account.second:account.second.size()>1)deviceChange=true;
                }
            }
            if(!recovery.empty()){
                protocol::check(enabled&&old&&ledger&&sameSnapshot(before,recovery.front().before)&&
                    sameSnapshot(incoming,recovery.back().after));
                Replica replica(store,server,incoming.legacyGroupId,true);
                protocol::check(replica.recoverAuthenticated(recovery,*ledger)==Reconciliation::Accepted);
            }else if(deviceChange){
                protocol::check(enabled&&ledger&&incoming.generation==before.generation+1);
                auto proof=std::find_if(incoming.deviceApprovals.begin(),incoming.deviceApprovals.end(),[&](const auto& row){return row.appliedGeneration==incoming.generation;});
                protocol::check(proof!=incoming.deviceApprovals.end());
                Replica replica(store,server,incoming.legacyGroupId,true);
                protocol::check(replica.accept(before,incoming,proof->proof,*ledger)==Reconciliation::Accepted);
            }else if(!old&&std::any_of(afterMembers.begin(),afterMembers.end(),[](const auto& account){return account.second.size()>1;})){
                // Reopening may use verified protected receipts, never a naked
                // current roster or a truncated public proof window as a ledger.
                protocol::check(enabled&&ledger);
                auto restored=restoreStored(store,incoming.legacyGroupId);
                exactRoot(restored);
                protocol::check(group_keys::GroupKeyManager::matchesSignedLedger(incoming,restored.state()));
                *ledger=std::move(restored);
            }
            keys.apply(std::move(incoming));
        }catch(...){
            // A failed current snapshot cannot leave old key authority usable.
            keys.revoke(server);throw;
        }
    }
    // New sibling bootstrap is membership metadata ONLY. No historical message,
    // attachment, history grant or old content key is imported by this boundary.
    static void bootstrapAuthenticated(Storage& store,group_keys::GroupKeyManager& keys,
                                       group_keys::RosterSnapshot incoming,const std::vector<GroupEvent>& events,
                                       const std::vector<AuthenticatedTransition>& evidence,GroupLedger& live,bool enabled=false){
        const auto server=incoming.serverGroupId;
        try{
            protocol::check(enabled&&!keys.roster(server)&&!live.state().created&&
                store.groupEvents(incoming.legacyGroupId).empty()&&!events.empty()&&events.size()<=256&&evidence.size()<=32);
            incoming=keys.checked(std::move(incoming));
            protocol::check(!incoming.creationProof.empty()&&events.front().kind==GroupEventKind::Create&&
                encodeGroupEvent(events.front())==incoming.creationProof);
            Replica replica(store,server,incoming.legacyGroupId,true);GroupLedger candidate;
            std::vector<GroupEvent> metadata;std::vector<std::pair<GroupEvent,Blob>> receipts;
            std::size_t proofIndex=0,bytes=0;
            for(std::size_t i=0;i<events.size();++i){
                const auto& event=events[i];auto wire=encodeGroupEvent(event);bytes+=wire.size();
                protocol::check(wire.size()<=4096&&bytes<=256*1024&&event.groupId==incoming.legacyGroupId&&event.message.empty()&&
                    (!i||events[i-1].logicalOrder<event.logicalOrder||
                        (events[i-1].logicalOrder==event.logicalOrder&&events[i-1].eventId<event.eventId)));
                if(event.kind==GroupEventKind::AuthorizeDevice||event.kind==GroupEventKind::RevokeDevice){
                    protocol::check(proofIndex<evidence.size());const auto& step=evidence[proofIndex++];
                    protocol::check(encodeGroupEvent(readApproval(step.proof).event)==wire&&
                        replica.admit(step.before,step.after,step.proof,candidate)==Reconciliation::Accepted);
                    for(const auto* state:{&step.before,&step.after})protocol::check(state->creationProof.empty()||state->creationProof==incoming.creationProof);
                    receipts.push_back({event,receipt(step)});
                }else{
                    protocol::check(event.kind>=GroupEventKind::Create&&event.kind<=GroupEventKind::SetRole&&candidate.append(event));
                    metadata.push_back(event);
                }
            }
            protocol::check(proofIndex==evidence.size()&&group_keys::GroupKeyManager::matchesSignedLedger(incoming,candidate.state())&&
                (evidence.empty()?incoming.generation==1:sameSnapshot(incoming,evidence.back().after)));
            replica.root(candidate);store.commitGroupReplicaReceipts(receipts,metadata);
            keys.apply(std::move(incoming));live=std::move(candidate);
        }catch(...){keys.revoke(server);throw;}
    }
    // Existing groups keep their accepted-event replay behavior. Device events
    // require individually protected receipts; recovery does not enable live
    // enrollment or release a key without a fresh authenticated roster.
    static GroupLedger restoreStored(Storage& store,const std::string& legacy){
        auto events=store.groupEvents(legacy);GroupLedger ordinary;bool hasDeviceEvents=false;
        for(const auto& event:events){
            if(event.kind==GroupEventKind::AuthorizeDevice||event.kind==GroupEventKind::RevokeDevice)hasDeviceEvents=true;
            else ordinary.append(event);
        }
        if(!hasDeviceEvents)return ordinary;
        auto creation=std::find_if(ordinary.events().begin(),ordinary.events().end(),[&](const auto& e){return e.kind==GroupEventKind::Create&&ordinary.accepted(e.eventId);});
        protocol::check(creation!=ordinary.events().end());
        auto value=std::string("PeerChat group roster v1|")+legacy+"|"+creation->actorPeerId;
        auto server=Storage::publicKeyId(Blob(value.begin(),value.end())).substr(0,32);
        return Replica(store,server,legacy,true).restore();
    }
    Replica(Storage& store,std::string server,std::string legacy,bool enabled=false):store_(store),server_(std::move(server)),legacy_(std::move(legacy)),enabled_(enabled){
        protocol::check(protocol::id(server_,32)&&protocol::id(legacy_,32));
    }
    Reconciliation accept(const group_keys::RosterSnapshot& before,const group_keys::RosterSnapshot& after,const Blob& proof,GroupLedger& ledger){
        auto candidate=ledger;auto result=admit(before,after,proof,candidate);if(result!=Reconciliation::Accepted)return result;
        auto event=readApproval(proof).event;
        store_.commitGroupReplicaReceipt(event,receipt({before,after,proof}));ledger=std::move(candidate);return result;
    }
    // Caller must obtain every snapshot through its current authenticated server
    // session. A public/truncated proof list is NOT an authenticated epoch chain.
    // Verify the entire page before one atomic protected commit or live mutation.
    Reconciliation recoverAuthenticated(const std::vector<AuthenticatedTransition>& steps,GroupLedger& ledger){
        if(!enabled_)return Reconciliation::LedgerRejected;
        if(steps.empty()||steps.size()>32)return Reconciliation::InvalidProof;
        auto candidate=ledger;root(candidate);
        std::vector<std::pair<GroupEvent,Blob>> receipts;
        for(std::size_t i=0;i<steps.size();++i){
            const auto& step=steps[i];
            if(i&&!sameSnapshot(steps[i-1].after,step.before))return Reconciliation::RosterMismatch;
            auto result=admit(step.before,step.after,step.proof,candidate);
            if(result!=Reconciliation::Accepted)return result;
            for(const auto* state:{&step.before,&step.after})if(!state->creationProof.empty()){
                auto creation=std::find_if(candidate.events().begin(),candidate.events().end(),[&](const auto& event){return event.kind==GroupEventKind::Create&&candidate.accepted(event.eventId);});
                if(creation==candidate.events().end()||encodeGroupEvent(*creation)!=state->creationProof)return Reconciliation::RosterMismatch;
            }
            receipts.push_back({readApproval(step.proof).event,receipt(step)});
        }
        store_.commitGroupReplicaReceipts(receipts);ledger=std::move(candidate);
        return Reconciliation::Accepted;
    }
    GroupLedger restore(){
        GroupLedger ledger;
        for(const auto& event:store_.groupEvents(legacy_)){
            if(event.kind!=GroupEventKind::AuthorizeDevice&&event.kind!=GroupEventKind::RevokeDevice){protocol::check(ledger.append(event));continue;}
            auto saved=store_.groupReplicaReceipt(legacy_,event.eventId);protocol::check(!saved.empty()&&saved.size()<=65536);
            protocol::Reader r(saved);protocol::check(r.type()==protocol::GroupEvent&&r.string()=="PeerChat/group-replica-receipt/v1");
            auto proof=r.bytes();auto before=snapshot(r),after=snapshot(r);r.end();
            protocol::check(encodeGroupEvent(readApproval(proof).event)==encodeGroupEvent(event));
            protocol::check(admit(before,after,proof,ledger)==Reconciliation::Accepted);
        }
        if(ledger.state().created){root(ledger);}return ledger;
    }
};
}
