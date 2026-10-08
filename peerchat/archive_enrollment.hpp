#pragma once
#include "archive_authorization.hpp"

namespace peerchat::archive {
// Platform adapters provide this bounded public-metadata projection ONLY from
// the authenticated server RPC. Password/account membership is not archive trust.
struct RegisteredGrant {Blob proof;bool eligible=false;};
struct EnrollmentMetadata {
    std::uint64_t account=0,authenticationGeneration=0,revision=0;
    std::string device,serverEpoch;
    std::vector<Blob> descriptors;
    std::vector<RegisteredGrant> grants;
};
class Enrollment {
    Storage& storage_;const Profile& local_;std::uint64_t account_;
    std::function<std::uint64_t()> clock_;
    std::uint64_t generation_=0,revision_=0,refreshed_=0;
    bool live_=false;
    struct Current {Grant grant;bool eligible=false,verified=false;};
    std::map<std::string,DeviceDescriptor> descriptors_;
    std::map<std::pair<std::string,std::string>,Current> current_;
    Authorization policy_;
    bool fresh()const{
        auto now=clock_();return live_&&now>=refreshed_&&now-refreshed_<30000;
    }
    bool member(std::uint64_t account,const std::string& device)const{
        return fresh()&&account==account_&&descriptors_.count(device)!=0;
    }
    std::optional<CurrentGrant> registered(std::uint64_t account,const std::string& issuer,const std::string& device)const{
        if(!fresh()||account!=account_)return {};
        auto found=current_.find({issuer,device});if(found==current_.end()||!found->second.verified)return {};
        const auto& current=found->second;const auto& g=current.grant;
        return CurrentGrant{g.generation,g.capabilities,g.issuanceId,g.revoked||!current.eligible};
    }
    void acceptApproved(){
        for(const auto& entry:current_){
            const auto& g=entry.second.grant;auto key=policy_.authorityKey(g.issuer);
            if(!entry.second.verified||!key||!policy_.trustsAuthority(*key))continue;
            auto previous=policy_.cachedGrant(g.issuer,g.device);
            if(previous&&previous->generation>g.generation)continue; // Pending local publication, NOT permission to use it.
            policy_.accept(g);
        }
    }
public:
    struct DeviceView {
        std::string peerId;bool local=false,approvedIssuer=false;
        bool mayRead=false,mayServe=false;
    };
    // This profile-worker component owns only archive authorization application
    // state. SessionTransportManager/DeliveryManager remain the media-free route
    // and durable history owners. No network thread, scheduler or second DB.
    Enrollment(Storage& storage,const Profile& local,std::uint64_t account,std::function<std::uint64_t()> clock)
        :storage_(storage),local_(local),account_(account),clock_(std::move(clock)),
        policy_(storage,local,account,[this](auto account,const auto& peer){return member(account,peer);},
            [this](auto account,const auto& issuer,const auto& peer){return registered(account,issuer,peer);}){
        require(bool(clock_)&&account>0);
    }
    Authorization& policy(){return policy_;}
    bool matches(std::uint64_t account,std::uint64_t generation)const{return fresh()&&account==account_&&generation==generation_;}
    void authenticated(std::uint64_t generation){
        require(generation>generation_);generation_=generation;revision_=0;invalidate();
    }
    void invalidate(){live_=false;}
    Blob registration(std::uint64_t utc){
        require(generation_>0);
        auto w=protocol::Writer(LocalFormat);w.string("PeerChat/ArchiveDeviceRegistration/v1");w.number(account_);w.string(local_.peerId);
        const auto digest=digestDevicePayload(w.data);static const char hex[]="0123456789abcdef";std::string context;
        for(auto b:digest){context+=hex[b>>4];context+=hex[b&15];}
        auto saved=storage_.archiveSyncState(context);
        if(saved.empty()){
            saved=encodeDescriptor(describeDevice(local_,account_,utc));storage_.saveArchiveSyncState(context,{},saved);
        }
        const auto d=decodeDescriptor(saved);require(d.account==account_&&d.device==local_.peerId&&d.publicKey==local_.publicKey);
        return saved; // Exact durable retry across launches, not a new identity.
    }
    void refresh(const EnrollmentMetadata& metadata){
        invalidate(); // Any malformed/stale response fails closed.
        require(generation_&&metadata.account==account_&&metadata.device==local_.peerId&&
            metadata.authenticationGeneration==generation_&&metadata.revision>revision_&&
            !metadata.serverEpoch.empty()&&metadata.serverEpoch.size()<=128&&metadata.descriptors.size()<=32&&metadata.grants.size()<=MaxGrants);
        std::map<std::string,DeviceDescriptor> descriptors;
        for(const auto& bytes:metadata.descriptors){
            auto d=decodeDescriptor(bytes);require(d.account==account_&&descriptors.emplace(d.device,std::move(d)).second);
        }
        auto local=descriptors.find(local_.peerId);require(local!=descriptors.end()&&local->second.publicKey==local_.publicKey);
        std::map<std::pair<std::string,std::string>,Current> current;
        for(const auto& registered:metadata.grants){
            auto g=decodeGrant(registered.proof);require(g.account==account_);
            auto issuer=descriptors.find(g.issuer),subject=descriptors.find(g.device);
            auto key=issuer==descriptors.end()?policy_.authorityKey(g.issuer):std::optional<Blob>{issuer->second.publicKey};
            const bool verified=key&&verifyDevicePayload(grantPayload(g),g.signature,*key);
            if(key)require(verified);
            if(subject!=descriptors.end())require(subject->second.publicKey==g.publicKey);
            if(registered.eligible)require(verified&&!g.revoked&&issuer!=descriptors.end()&&subject!=descriptors.end());
            require(current.emplace(std::make_pair(g.issuer,g.device),Current{g,registered.eligible,verified}).second);
        }
        descriptors_=std::move(descriptors);current_=std::move(current);revision_=metadata.revision;refreshed_=clock_();live_=true;
        try{
            acceptApproved();std::vector<Grant> registered;
            for(const auto& entry:current_)if(entry.second.verified&&entry.second.grant.issuer==local_.peerId)registered.push_back(entry.second.grant);
            policy_.reconcilePublications(registered);
        }catch(...){invalidate();throw;}
    }
    void approveIssuer(const std::string& device,bool consent){
        require(fresh());auto descriptor=descriptors_.find(device);require(descriptor!=descriptors_.end());
        policy_.approveAuthority(descriptor->second.publicKey,consent);acceptApproved();
    }
    Grant authorizeDevice(const std::string& device,std::uint64_t capabilities,std::uint64_t utc,bool consent){
        require(fresh());auto descriptor=descriptors_.find(device);require(descriptor!=descriptors_.end());
        return policy_.issue(descriptor->second.publicKey,capabilities,false,utc,consent);
    }
    Grant revokeDevice(const std::string& device,std::uint64_t utc,bool consent){
        require(fresh());auto old=policy_.cachedGrant(local_.peerId,device);require(bool(old));
        return policy_.issue(old->publicKey,0,true,utc,consent);
    }
    void disableIssuer(const std::string& issuer,bool consent){policy_.disableAuthority(issuer,consent);}
    std::vector<Grant> pendingPublications()const{return policy_.pendingPublications();}
    std::vector<DeviceView> devices()const{
        require(fresh());std::vector<DeviceView> result;
        for(const auto& entry:descriptors_){
            const auto& d=entry.second;DeviceView row{d.device,d.device==local_.peerId,policy_.trustsAuthority(d.publicKey),false,false};
            for(const auto& grant:current_)if(grant.second.grant.device==d.device){
                row.mayRead=row.mayRead||policy_.permits(grant.first.first,d.device,ReadHistory);
                row.mayServe=row.mayServe||policy_.permits(grant.first.first,d.device,ServeHistory);
            }
            result.push_back(std::move(row));
        }
        return result;
    }
};
}
