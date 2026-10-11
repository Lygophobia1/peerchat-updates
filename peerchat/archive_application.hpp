#pragma once
#include "archive_enrollment.hpp"
#include "archive_execution.hpp"

namespace peerchat::archive {
constexpr size_t MaxApplicationBytes=262144;
inline protocol::Writer applicationHeader(unsigned operation){
    protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveApplication/v1");w.number(1);w.number(operation);return w;
}
inline void reconcileInitialSync(Enrollment& enrollment,Execution& execution,std::uint64_t now=0){
    auto restoredSource=execution.blockedRestoreSource();
    if(!restoredSource.empty()){
        std::string issuer;
        try{issuer=enrollment.sourceIssuer(restoredSource);}catch(const std::exception&){return;}
        execution.resumeAuthorized(issuer,restoredSource);return;
    }
    if(!execution.needsAutomaticSync(now))return;
    auto devices=enrollment.devices();
    // Fair bounded reconciliation across sources; every source retains its own
    // durable conversation cursors. Missing availability is never called Online.
    auto last=std::find_if(devices.begin(),devices.end(),[&](const auto& d){return d.peerId==execution.lastAutomaticSource();});
    if(last!=devices.end())std::rotate(devices.begin(),std::next(last),devices.end());
    for(const auto& device:devices){
        if(device.local||!device.mayServe||!enrollment.automaticSync(device.peerId)||!enrollment.automaticSync(enrollment.policy().localDevice()))continue;
        std::string issuer;
        // Capability bits alone are insufficient: source and reader must have
        // current verified grants from the SAME explicitly trusted authority.
        try{issuer=enrollment.sourceIssuer(device.peerId);}catch(const std::exception&){continue;}
        // Do not swallow a corrupt durable cursor as an unavailable source.
        execution.startAutomatic(issuer,device.peerId,now);return;
    }
}
inline Blob executeSyncOperation(Enrollment& enrollment,Execution& execution,unsigned operation,const Blob& bytes,
    std::uint64_t now,std::uint64_t network){
    require(((operation>=12&&operation<=15)||(operation>=19&&operation<=23))&&bytes.size()<=MaxApplicationBytes);
    protocol::Reader r(bytes);require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchiveApplication/v1"&&r.number()==1&&r.number()==operation);
    auto account=r.number(),generation=r.number();
    if(operation==22){r.end();auto result=applicationHeader(operation);auto details=execution.diagnostics()+enrollment.authorizationDiagnostics(account,generation);if(details.size()>49152)details.resize(49152);result.string(details);return result.data;}
    require(enrollment.matches(account,generation));
    auto result=applicationHeader(operation);
    if(operation==19){auto peer=r.string();auto write=r.number();require(write<=1);if(write){auto enabled=r.number();require(enabled<=1);r.end();enrollment.automaticSync(peer,enabled!=0);}else r.end();result.number(enrollment.automaticSync(peer));return result.data;}
    if(operation==20){r.end();auto peers=execution.providingDevices(now);result.number(peers.size());for(const auto& peer:peers)result.string(peer);return result.data;}
    if(operation==21){r.end();auto devices=enrollment.devices();result.number(devices.size());for(const auto& d:devices){result.string(d.peerId);result.number(d.grantedRead);result.number(d.grantedServe);}return result.data;}
    if(operation==12){auto source=r.string();auto rescan=r.number();require(rescan<=1);r.end();execution.start(enrollment.sourceIssuer(source),source,rescan!=0);}
    if(operation==14){r.end();execution.stop();}
    if(operation==15){auto binding=r.bytes();require(binding.size()<=1024);r.end();
        auto scope=decodeSessionBinding(binding,enrollment.policy().localDevice());require(scope.archive.account==account);execution.incoming(scope,now,network);}
    if(operation==13||operation==23){r.end();reconcileInitialSync(enrollment,execution,now);execution.tick(now,network);auto snapshot=execution.snapshot();result.string(execution.state());result.number(snapshot.processed);
        result.number(snapshot.conversations);result.number(snapshot.complete);result.number(execution.running());
        result.string(execution.scope()?execution.scope()->archive.source:std::string{});result.number(snapshot.attachments);
        if(operation==23){result.number(execution.received());result.number(execution.historyRevision());result.number(execution.lastSuccessful());result.number(execution.scanGeneration());result.number(enrollment.policy().historySyncEnabled());}}
    return result.data;
}
inline Blob executeEnrollmentOperation(Enrollment& enrollment,unsigned operation,const Blob& bytes,std::uint64_t utc){
    require(operation>=1&&operation<=11&&bytes.size()<=MaxApplicationBytes);
    protocol::Reader r(bytes);require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchiveApplication/v1"&&r.number()==1&&r.number()==operation);
    auto result=applicationHeader(operation);
    if(operation==1){r.end();return enrollment.registration(utc);}
    if(operation==2){
        EnrollmentMetadata m;m.account=r.number();m.authenticationGeneration=r.number();m.revision=r.number();m.device=r.string();m.serverEpoch=r.string();
        auto count=r.number();require(count<=32);for(std::uint64_t n=0;n<count;++n){auto proof=r.bytes();require(proof.size()<=1024);m.descriptors.push_back(std::move(proof));}
        count=r.number();require(count<=MaxGrants);for(std::uint64_t n=0;n<count;++n){
            auto proof=r.bytes();auto eligible=r.number();require(proof.size()<=2048&&eligible<=1);m.grants.push_back({std::move(proof),eligible!=0});
        }
        r.end();enrollment.refresh(m);return result.data;
    }
    if(operation==3){
        r.end();auto devices=enrollment.devices();result.number(devices.size());
        for(const auto& d:devices){result.string(d.peerId);result.number(d.local);result.number(d.approvedIssuer);result.number(d.mayRead);result.number(d.mayServe);}
        // Local application projection extension; peer/server wire is unchanged.
        result.string("PeerChat/ArchiveDeviceStatus/v1");
        size_t revoked=0;for(const auto& d:devices)if(d.revoked)++revoked;
        result.number(revoked);for(const auto& d:devices)if(d.revoked)result.string(d.peerId);
        size_t authorized=0;for(const auto& d:devices)if(d.authorized)++authorized;
        result.number(authorized);for(const auto& d:devices)if(d.authorized)result.string(d.peerId);
        return result.data;
    }
    if(operation==8){
        r.end();auto pending=enrollment.pendingPublications();result.number(pending.size());
        for(const auto& grant:pending)result.bytes(encodeGrant(grant));
        return result.data;
    }
    if(operation==9){r.end();enrollment.invalidate();return result.data;}
    if(operation==11){
        auto account=r.number(),generation=r.number(),write=r.number();require(enrollment.matches(account,generation)&&write<=1);
        auto enabled=write?r.number():0;require(enabled<=1);r.end();
        if(write)enrollment.policy().setHistorySyncEnabled(enabled!=0);
        result.number(enrollment.policy().historySyncEnabled());return result.data;
    }
    auto action=operation;
    if(operation==10){auto account=r.number(),generation=r.number(),selected=r.number();require(selected>=4&&selected<=7&&enrollment.matches(account,generation));action=static_cast<unsigned>(selected);}
    auto device=r.string();require(protocol::id(device));
    auto capability=action==5?r.number():0;auto consent=r.number();require(consent<=1);r.end();
    // Complete structural parsing before any user decision can mutate policy.
    if(action==4){enrollment.approveIssuer(device,consent!=0);return result.data;}
    if(action==5)return encodeGrant(enrollment.authorizeDevice(device,capability,utc,consent!=0));
    if(action==6)return encodeGrant(enrollment.revokeDevice(device,utc,consent!=0));
    require(action==7);enrollment.disableIssuer(device,consent!=0);return result.data;
}
}
