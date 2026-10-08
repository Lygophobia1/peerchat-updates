#pragma once
#include "group.hpp"
#include "protocol.hpp"
#include <functional>
#include <map>

namespace peerchat::archive {
// Deliberately separate from Friend and GroupControl permissions. No socket,
// retry owner, plaintext server archive, or new permanent signing identity.
constexpr std::uint64_t ReadHistory=1,ServeHistory=2,AllCapabilities=3;
constexpr size_t MaxAuthorities=8,MaxGrants=64;
constexpr auto LocalFormat=static_cast<protocol::Type>(31);
inline void require(bool ok){if(!ok)throw std::runtime_error("Archive authorization/provenance rejected");}
struct Grant {
    std::uint64_t account=0,generation=0,capabilities=0,issued=0;
    std::string issuer,device,issuanceId;
    Blob publicKey,signature;
    bool revoked=false;
};
// Supplied from current authenticated authorization metadata, not persisted
// grants or ordinary account membership. Missing/stale metadata denies access.
struct CurrentGrant {
    std::uint64_t generation=0,capabilities=0;
    std::string issuanceId;
    bool revoked=true;
};
inline Blob grantPayload(const Grant& g){
    require(g.account>0&&g.generation>0&&g.generation<INT64_MAX&&g.issued>0&&
        protocol::id(g.issuer)&&protocol::id(g.device)&&protocol::id(g.issuanceId,32)&&
        g.publicKey.size()==72&&Storage::publicKeyId(g.publicKey)==g.device&&
        (g.revoked?g.capabilities==0:g.capabilities>0&&g.capabilities<=AllCapabilities));
    protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveAuthorization/v1");w.number(1);
    w.number(g.account);w.string(g.issuer);w.string(g.device);w.bytes(g.publicKey);
    w.number(g.capabilities);w.number(g.generation);w.number(g.issued);w.string(g.issuanceId);w.number(g.revoked);
    return w.data;
}
inline Blob encodeGrant(const Grant& g){auto payload=grantPayload(g);require(g.signature.size()==64);protocol::Writer w(LocalFormat);w.bytes(payload);w.bytes(g.signature);return w.data;}
inline Grant decodeGrant(const Blob& bytes){
    require(bytes.size()<=2048);protocol::Reader wire(bytes);require(wire.type()==LocalFormat);auto payload=wire.bytes(),signature=wire.bytes();wire.end();
    protocol::Reader r(payload);require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchiveAuthorization/v1"&&r.number()==1);
    Grant g;g.account=r.number();g.issuer=r.string();g.device=r.string();g.publicKey=r.bytes();g.capabilities=r.number();
    g.generation=r.number();g.issued=r.number();g.issuanceId=r.string();auto revoked=r.number();require(revoked<=1);g.revoked=revoked!=0;
    r.end();g.signature=std::move(signature);require(g.signature.size()==64&&grantPayload(g)==payload);return g;
}
class Authorization {
    struct Authority {Blob publicKey;bool enabled=true;};
    Storage& storage_;const Profile& local_;std::uint64_t account_;
    // Current server-authorized account/device membership is necessary, NEVER sufficient.
    std::function<bool(std::uint64_t,const std::string&)> currentDevice_;
    std::function<std::optional<CurrentGrant>(std::uint64_t,const std::string&,const std::string&)> currentGrant_;
    std::map<std::string,Authority> authorities_;
    std::map<std::pair<std::string,std::string>,Grant> grants_;
    Blob saved_;
    void persist(){
        protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveLocalPolicy/v1");w.number(account_);w.string(local_.peerId);
        w.number(authorities_.size());for(auto& item:authorities_){w.string(item.first);w.bytes(item.second.publicKey);w.number(item.second.enabled);}
        w.number(grants_.size());for(auto& item:grants_)w.bytes(encodeGrant(item.second));
        storage_.saveArchiveAuthorizationState(saved_,w.data);saved_=std::move(w.data);
    }
    void validateGrant(const Grant& g)const{
        require(g.account==account_);auto a=authorities_.find(g.issuer);
        require(a!=authorities_.end()&&verifyDevicePayload(grantPayload(g),g.signature,a->second.publicKey));
    }
    void usable()const{require(storage_.archiveAuthorizationState()==saved_);} // Fail closed after another owner revokes/changes policy.
public:
    Authorization(Storage& storage,const Profile& local,std::uint64_t account,
        std::function<bool(std::uint64_t,const std::string&)> current,
        std::function<std::optional<CurrentGrant>(std::uint64_t,const std::string&,const std::string&)> grants)
        :storage_(storage),local_(local),account_(account),currentDevice_(std::move(current)),currentGrant_(std::move(grants)){
        require(account>0&&protocol::id(local.peerId)&&Storage::publicKeyId(local.publicKey)==local.peerId&&bool(currentDevice_)&&bool(currentGrant_));
        saved_=storage_.archiveAuthorizationState();if(saved_.empty())return;
        protocol::Reader r(saved_);require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchiveLocalPolicy/v1"&&r.number()==account&&r.string()==local.peerId);
        auto count=r.number();require(count<=MaxAuthorities);
        for(std::uint64_t n=0;n<count;++n){auto id=r.string();auto key=r.bytes();auto enabled=r.number();
            require(enabled<=1&&protocol::id(id)&&key.size()==72&&Storage::publicKeyId(key)==id&&authorities_.emplace(id,Authority{key,enabled!=0}).second);}
        count=r.number();require(count<=MaxGrants);
        for(std::uint64_t n=0;n<count;++n){auto g=decodeGrant(r.bytes());validateGrant(g);require(grants_.emplace(std::make_pair(g.issuer,g.device),g).second);}
        r.end();
    }
    // Interactive, explicit trust decision only. A server list/login/friendship
    // MUST NOT call this implicitly. Existing ordinary trust is not enrollment.
    void approveAuthority(const Blob& key,bool explicitConsent){
        usable();require(explicitConsent&&key.size()==72);auto id=Storage::publicKeyId(key);require(currentDevice_(account_,id));
        auto found=authorities_.find(id);
        if(found!=authorities_.end()){require(found->second.enabled&&found->second.publicKey==key);return;}
        require(authorities_.size()<MaxAuthorities);authorities_.emplace(id,Authority{key,true});
        try{persist();}catch(...){authorities_.erase(id);throw;}
    }
    void disableAuthority(const std::string& issuer,bool explicitConsent){
        usable();require(explicitConsent);auto a=authorities_.find(issuer);require(a!=authorities_.end());
        auto previous=a->second.enabled;a->second.enabled=false;try{persist();}catch(...){a->second.enabled=previous;throw;}
        // No silent re-enable: stale grants must not resurrect an authority.
    }
    bool accept(const Grant& g){
        usable();validateGrant(g);require(authorities_.at(g.issuer).enabled);
        auto key=std::make_pair(g.issuer,g.device);auto old=grants_.find(key);
        if(old!=grants_.end()){
            if(g.generation==old->second.generation){require(grantPayload(g)==grantPayload(old->second));return false;}
            require(g.generation>old->second.generation);
        }
        require(old!=grants_.end()||grants_.size()<MaxGrants);
        auto previous=old==grants_.end()?std::optional<Grant>{}:std::optional<Grant>{old->second};
        grants_[key]=g;
        try{persist();}catch(...){if(previous)grants_[key]=*previous;else grants_.erase(key);throw;}
        return true;
    }
    Grant issue(const Blob& subject,std::uint64_t capabilities,bool revoke,std::uint64_t now,bool explicitConsent){
        usable();require(explicitConsent&&subject.size()==72&&currentDevice_(account_,local_.peerId));auto id=Storage::publicKeyId(subject);
        auto root=authorities_.find(local_.peerId);require(root!=authorities_.end()&&root->second.enabled&&root->second.publicKey==local_.publicKey);
        // Device removal can still be revoked locally; new grants require live membership.
        require(revoke||currentDevice_(account_,id));auto old=grants_.find({local_.peerId,id});
        Grant g;g.account=account_;g.issuer=local_.peerId;g.device=id;g.publicKey=subject;g.capabilities=revoke?0:capabilities;g.revoked=revoke;
        g.generation=old==grants_.end()?1:old->second.generation+1;g.issued=now;g.issuanceId=Storage::newMessageId();
        g.signature=signDevicePayload(grantPayload(g),local_);accept(g);return g;
    }
    bool permits(const std::string& issuer,const std::string& device,std::uint64_t capability)const{
        usable();auto a=authorities_.find(issuer);auto g=grants_.find({issuer,device});
        const auto current=currentGrant_(account_,issuer,device);
        return capability>0&&capability<=AllCapabilities&&a!=authorities_.end()&&a->second.enabled&&
            g!=grants_.end()&&!g->second.revoked&&(g->second.capabilities&capability)==capability&&
            current&&!current->revoked&&current->generation==g->second.generation&&
            current->capabilities==g->second.capabilities&&current->issuanceId==g->second.issuanceId&&
            currentDevice_(account_,issuer)&&currentDevice_(account_,device);
    }
    Grant grant(const std::string& issuer,const std::string& device)const{usable();auto g=grants_.find({issuer,device});require(g!=grants_.end());return g->second;}
};
}
