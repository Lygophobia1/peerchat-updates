#pragma once
#include "group.hpp"
#include "protocol.hpp"
#include <functional>
#include <map>
#include <algorithm>

namespace peerchat::archive {
// Deliberately separate from Friend and GroupControl permissions. No socket,
// retry owner, plaintext server archive, or new permanent signing identity.
constexpr std::uint64_t ReadHistory=1,ServeHistory=2,AllCapabilities=3;
constexpr size_t MaxAuthorities=8,MaxGrants=64;
constexpr size_t MaxPendingGrants=32;
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
// Public directory metadata only. Publishing an existing device identity does
// not approve an authority or confer any archive capability.
struct DeviceDescriptor {
    std::uint64_t account=0,issued=0;
    std::string device;
    Blob publicKey,signature;
};
inline Blob descriptorPayload(const DeviceDescriptor& d){
    require(d.account>0&&d.account<INT64_MAX&&d.issued>0&&d.issued<INT64_MAX&&
        protocol::id(d.device)&&d.publicKey.size()==72&&Storage::publicKeyId(d.publicKey)==d.device);
    protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveDeviceDescriptor/v1");w.number(1);
    w.number(d.account);w.string(d.device);w.bytes(d.publicKey);w.number(d.issued);return w.data;
}
inline Blob encodeDescriptor(const DeviceDescriptor& d){
    require(d.signature.size()==64);protocol::Writer w(LocalFormat);w.bytes(descriptorPayload(d));w.bytes(d.signature);return w.data;
}
inline DeviceDescriptor describeDevice(const Profile& device,std::uint64_t account,std::uint64_t now){
    DeviceDescriptor d;d.account=account;d.issued=now;d.device=device.peerId;d.publicKey=device.publicKey;
    d.signature=signDevicePayload(descriptorPayload(d),device);return d;
}
inline DeviceDescriptor decodeDescriptor(const Blob& bytes){
    require(bytes.size()<=1024);protocol::Reader r(bytes);require(r.type()==LocalFormat);
    auto payload=r.bytes(),signature=r.bytes();r.end();protocol::Reader p(payload);
    require(p.type()==LocalFormat&&p.string()=="PeerChat/ArchiveDeviceDescriptor/v1"&&p.number()==1);
    DeviceDescriptor d;d.account=p.number();d.device=p.string();d.publicKey=p.bytes();d.issued=p.number();p.end();d.signature=std::move(signature);
    require(descriptorPayload(d)==payload&&d.signature.size()==64&&verifyDevicePayload(payload,d.signature,d.publicKey));return d;
}
inline Blob grantPayload(const Grant& g){
    require(g.account>0&&g.account<INT64_MAX&&g.generation>0&&g.generation<INT64_MAX&&g.issued>0&&g.issued<INT64_MAX&&
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
    std::vector<Grant> pending_; // Signed public metadata, atomically durable with local policy.
    Blob saved_;
    std::string syncPreferenceContext()const{
        protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveSyncPreference/v1");w.number(account_);w.string(local_.peerId);
        auto digest=digestDevicePayload(w.data);static const char hex[]="0123456789abcdef";std::string result;
        for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
    }
    void persist(){
        protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveLocalPolicy/v2");w.number(account_);w.string(local_.peerId);
        w.number(authorities_.size());for(auto& item:authorities_){w.string(item.first);w.bytes(item.second.publicKey);w.number(item.second.enabled);}
        w.number(grants_.size());for(auto& item:grants_)w.bytes(encodeGrant(item.second));
        w.number(pending_.size());for(const auto& grant:pending_)w.bytes(encodeGrant(grant));
        storage_.saveArchiveAuthorizationState(saved_,w.data);saved_=std::move(w.data);
    }
    void validateGrant(const Grant& g)const{
        require(g.account==account_);auto a=authorities_.find(g.issuer);
        require(a!=authorities_.end()&&verifyDevicePayload(grantPayload(g),g.signature,a->second.publicKey));
    }
    void usable()const{require(storage_.archiveAuthorizationState()==saved_);} // Fail closed after another owner revokes/changes policy.
public:
    // Participation policy, NOT a grant. Stored in the existing protected local
    // record store. Every admission re-reads it so OFF also stops an older owner.
    bool historySyncEnabled()const{
        auto bytes=storage_.archiveSyncState(syncPreferenceContext());if(bytes.empty())return true;
        protocol::Reader r(bytes);require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchiveSyncPreference/v1"&&r.number()==account_&&r.string()==local_.peerId);
        auto value=r.number();require(value<=1);r.end();return value!=0;
    }
    void setHistorySyncEnabled(bool enabled){
        auto context=syncPreferenceContext();auto previous=storage_.archiveSyncState(context);
        (void)historySyncEnabled(); // Corrupt/missing protected material is not silently replaced.
        protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveSyncPreference/v1");w.number(account_);w.string(local_.peerId);w.number(enabled);
        storage_.saveArchiveSyncState(context,previous,w.data);
        // Grants, local history and stable per-conversation cursors are untouched.
    }
    std::uint64_t account()const noexcept{return account_;}
    const std::string& localDevice()const noexcept{return local_.peerId;}
    Authorization(Storage& storage,const Profile& local,std::uint64_t account,
        std::function<bool(std::uint64_t,const std::string&)> current,
        std::function<std::optional<CurrentGrant>(std::uint64_t,const std::string&,const std::string&)> grants)
        :storage_(storage),local_(local),account_(account),currentDevice_(std::move(current)),currentGrant_(std::move(grants)){
        require(account>0&&protocol::id(local.peerId)&&Storage::publicKeyId(local.publicKey)==local.peerId&&bool(currentDevice_)&&bool(currentGrant_));
        saved_=storage_.archiveAuthorizationState();if(saved_.empty())return;
        protocol::Reader r(saved_);require(r.type()==LocalFormat);auto version=r.string();
        require((version=="PeerChat/ArchiveLocalPolicy/v1"||version=="PeerChat/ArchiveLocalPolicy/v2")&&r.number()==account&&r.string()==local.peerId);
        auto count=r.number();require(count<=MaxAuthorities);
        for(std::uint64_t n=0;n<count;++n){auto id=r.string();auto key=r.bytes();auto enabled=r.number();
            require(enabled<=1&&protocol::id(id)&&key.size()==72&&Storage::publicKeyId(key)==id&&authorities_.emplace(id,Authority{key,enabled!=0}).second);}
        count=r.number();require(count<=MaxGrants);
        for(std::uint64_t n=0;n<count;++n){auto g=decodeGrant(r.bytes());validateGrant(g);require(grants_.emplace(std::make_pair(g.issuer,g.device),g).second);}
        if(version=="PeerChat/ArchiveLocalPolicy/v2"){
            count=r.number();require(count<=MaxPendingGrants);
            std::map<std::string,std::uint64_t> generations;
            for(std::uint64_t n=0;n<count;++n){
                auto g=decodeGrant(r.bytes());validateGrant(g);auto latest=grants_.find({g.issuer,g.device});
                require(g.issuer==local_.peerId&&latest!=grants_.end()&&g.generation<=latest->second.generation&&
                    (!generations.count(g.device)||g.generation==generations.at(g.device)+1));
                generations[g.device]=g.generation;pending_.push_back(std::move(g));
            }
        }
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
        require(pending_.size()<MaxPendingGrants);
        g.signature=signDevicePayload(grantPayload(g),local_);
        // One protected CAS commit contains BOTH policy and publication intent.
        // A failed RPC/restart cannot lose an intermediate revoke/re-enroll
        // generation or regenerate different proof bytes for the same operation.
        pending_.push_back(g);
        try{accept(g);}catch(...){pending_.pop_back();throw;}
        return g;
    }
    const char* admissionReason(const std::string& issuer,const std::string& device,std::uint64_t capability)const{
        usable();auto a=authorities_.find(issuer);auto g=grants_.find({issuer,device});
        const auto current=currentGrant_(account_,issuer,device);
        if(!historySyncEnabled())return "history_sync_off";
        if(!capability||capability>AllCapabilities)return "invalid_capability";
        if(a==authorities_.end())return "authorizer_not_trusted";
        if(!a->second.enabled)return "authorizer_disabled";
        if(g==grants_.end())return "local_grant_missing";
        if(g->second.revoked)return "local_grant_revoked";
        if((g->second.capabilities&capability)!=capability)return "local_permission_missing";
        if(!current)return "current_verified_grant_unavailable";
        if(current->revoked)return "server_grant_revoked_or_ineligible";
        if(current->generation!=g->second.generation)return "grant_generation_mismatch";
        if(current->capabilities!=g->second.capabilities)return "grant_capability_mismatch";
        if(current->issuanceId!=g->second.issuanceId)return "grant_id_mismatch";
        if(!currentDevice_(account_,issuer))return "authorizer_device_mapping_unavailable";
        if(!currentDevice_(account_,device))return "subject_device_mapping_unavailable";
        return "allowed";
    }
    bool permits(const std::string& issuer,const std::string& device,std::uint64_t capability)const{
        return std::string(admissionReason(issuer,device,capability))=="allowed";
    }
    std::vector<std::string> authorityReferences()const{
        usable();std::vector<std::string> result;for(const auto& row:authorities_)result.push_back(row.first);return result;
    }
    std::vector<Grant> grantReferences()const{
        usable();std::vector<Grant> result;for(const auto& row:grants_)result.push_back(row.second);return result;
    }
    Grant grant(const std::string& issuer,const std::string& device)const{usable();auto g=grants_.find({issuer,device});require(g!=grants_.end());return g->second;}
    std::vector<Grant> pendingPublications()const{usable();return pending_;}
    // Call after refreshing authenticated server metadata. Only a locally
    // verified exact signed generation confirms publication; login/HTTP success
    // alone is not enough. Higher matching generations retire older intents.
    size_t reconcilePublications(const std::vector<Grant>& registered={}){
        usable();auto previous=pending_;
        std::map<std::string,std::uint64_t> confirmed;
        for(const auto& g:pending_){
            auto current=currentGrant_(account_,g.issuer,g.device);
            if(current&&current->generation==g.generation&&current->capabilities==g.capabilities&&
                current->issuanceId==g.issuanceId&&current->revoked==g.revoked)confirmed[g.device]=g.generation;
        }
        for(const auto& entry:grants_){
            const auto& g=entry.second;if(g.issuer!=local_.peerId)continue;
            auto current=currentGrant_(account_,g.issuer,g.device);
            if(current&&current->generation==g.generation&&current->capabilities==g.capabilities&&
                current->issuanceId==g.issuanceId&&current->revoked==g.revoked)confirmed[g.device]=g.generation;
        }
        // An exact server-registered signed record confirms publication even
        // when device-removal tombstones make it ineligible for authorization.
        // Otherwise an already-stored disabled generation could indefinitely
        // block publishing its explicit re-enrollment successor.
        for(const auto& g:registered){
            require(g.issuer==local_.peerId);validateGrant(g);bool known=false;
            auto latest=grants_.find({g.issuer,g.device});
            if(latest!=grants_.end()&&encodeGrant(latest->second)==encodeGrant(g))known=true;
            for(const auto& pending:pending_)if(encodeGrant(pending)==encodeGrant(g)){known=true;break;}
            if(known)confirmed[g.device]=std::max(confirmed[g.device],g.generation);
        }
        pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](const Grant& g){
            return confirmed.count(g.device)&&g.generation<=confirmed.at(g.device);
        }),pending_.end());
        auto removed=previous.size()-pending_.size();
        if(removed)try{persist();}catch(...){pending_=std::move(previous);throw;}
        return removed;
    }
    bool trustsAuthority(const Blob& key)const{
        usable();if(key.size()!=72)return false;auto found=authorities_.find(Storage::publicKeyId(key));
        return found!=authorities_.end()&&found->second.enabled&&found->second.publicKey==key;
    }
    std::optional<Blob> authorityKey(const std::string& issuer)const{
        usable();auto found=authorities_.find(issuer);if(found==authorities_.end())return {};
        return found->second.publicKey; // Public verification key, never private material.
    }
    std::optional<Grant> cachedGrant(const std::string& issuer,const std::string& device)const{
        usable();auto found=grants_.find({issuer,device});if(found==grants_.end())return {};return found->second;
    }
};
}
