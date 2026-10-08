#pragma once
#include "archive_enrollment.hpp"

namespace peerchat::archive {
constexpr size_t MaxApplicationBytes=262144;
inline protocol::Writer applicationHeader(unsigned operation){
    protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveApplication/v1");w.number(1);w.number(operation);return w;
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
