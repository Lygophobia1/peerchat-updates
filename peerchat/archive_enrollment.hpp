#pragma once
#include "archive_authorization.hpp"
#include <sstream>

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
        bool mayRead=false,mayServe=false,revoked=false,authorized=false,grantedRead=false,grantedServe=false;
        bool mayAuthorize=false,pairAuthorized=false,localGrantPresent=false,localGrantRead=false,localGrantServe=false;
        bool maySetUp=false;std::string approvalIssuer{};
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
    // Public identity references only. No proof, key, signature or history bytes.
    std::string authorizationDiagnostics(std::uint64_t account,std::uint64_t generation)const{
        require(account==account_&&generation==generation_);
        std::ostringstream out;
        out<<"scope=archive_history account="<<account_<<" local_device="<<local_.peerId
           <<"\nauthentication_generation="<<generation_<<" metadata_revision="<<revision_
           <<" metadata_fresh="<<fresh()<<" refreshed_ms="<<refreshed_<<" observed_ms="<<clock_()<<"\n";
        std::map<std::string,bool> issuers;
        for(const auto& id:policy_.authorityReferences())issuers[id]=true;
        for(const auto& row:current_)issuers[row.first.first]=true;
        std::map<std::pair<std::string,std::string>,bool> rows;
        for(const auto& g:policy_.grantReferences())rows[{g.issuer,g.device}]=true;
        for(const auto& row:current_)rows[row.first]=true;
        for(const auto& issuer:issuers){
            auto key=policy_.authorityKey(issuer.first);
            out<<"authorizer="<<issuer.first<<" trust="<<(!key?"unknown":policy_.trustsAuthority(*key)?"trusted":"disabled")
               <<" device_mapping="<<descriptors_.count(issuer.first)<<"\n";
        }
        for(const auto& row:rows){
            auto local=policy_.cachedGrant(row.first.first,row.first.second);auto server=current_.find(row.first);
            out<<"grant authorizer="<<row.first.first<<" device="<<row.first.second<<" device_mapping="<<descriptors_.count(row.first.second);
            if(local)out<<" local_id="<<local->issuanceId<<" local_generation="<<local->generation<<" local_permissions="<<local->capabilities<<" local_revoked="<<local->revoked;
            else out<<" local=missing";
            if(server!=current_.end()){const auto& s=server->second;out<<" server_id="<<s.grant.issuanceId<<" server_generation="<<s.grant.generation<<" server_permissions="<<s.grant.capabilities<<" server_revoked="<<s.grant.revoked<<" signature_verified="<<s.verified<<" server_eligible="<<s.eligible;}
            else out<<" server=missing";
            out<<" receive="<<policy_.admissionReason(row.first.first,row.first.second,ReadHistory)<<" provide="<<policy_.admissionReason(row.first.first,row.first.second,ServeHistory)<<"\n";
        }
        for(const auto& device:descriptors_)if(device.first!=local_.peerId){
            bool admitted=false,sourceAny=false,readerAny=false;
            for(const auto& issuer:issuers){
                bool source=policy_.permits(issuer.first,device.first,ServeHistory),reader=policy_.permits(issuer.first,local_.peerId,ReadHistory);
                sourceAny|=source;readerAny|=reader;admitted|=source&&reader;
                out<<"source="<<device.first<<" reader="<<local_.peerId<<" authorizer="<<issuer.first
                   <<" source_provide="<<policy_.admissionReason(issuer.first,device.first,ServeHistory)
                   <<" reader_receive="<<policy_.admissionReason(issuer.first,local_.peerId,ReadHistory)<<"\n";
            }
            out<<"source_eligibility="<<device.first<<" result="<<(!fresh()?"metadata_stale":admitted?"allowed":sourceAny&&readerAny?"incompatible_authorizers":"required_grant_or_trust_missing")<<"\n";
        }
        auto result=out.str();if(result.size()>49152)result=result.substr(0,49100)+"\n[bounded report truncated]\n";return result;
    }
    std::string automaticContext(const std::string& peer)const{
        protocol::Writer w(LocalFormat);w.string("PeerChat/HistoryAutomatic/v1");w.number(account_);w.string(local_.peerId);w.string(peer);
        std::string context;static const char hex[]="0123456789abcdef";for(auto byte:digestDevicePayload(w.data)){context+=hex[byte>>4];context+=hex[byte&15];}return context;
    }
    // Local scheduling preference only: never grants access or changes grants.
    bool automaticSync(const std::string& peer)const{
        require(fresh()&&member(account_,peer));
        auto context=automaticContext(peer);
        auto bytes=storage_.archiveSyncState(context);if(bytes.empty())return true;
        protocol::Reader r(bytes);require(r.type()==LocalFormat&&r.string()==context);auto value=r.number();r.end();require(value<=1);return value!=0;
    }
    void automaticSync(const std::string& peer,bool enabled){
        (void)automaticSync(peer);
        auto context=automaticContext(peer);
        auto previous=storage_.archiveSyncState(context);protocol::Writer w(LocalFormat);w.string(context);w.number(enabled);
        storage_.saveArchiveSyncState(context,previous,w.data);
    }
    std::string sourceIssuer(const std::string& source)const{
        require(fresh()&&protocol::id(source)&&source!=local_.peerId);
        for(const auto& entry:current_)if(entry.first.second==source&&
            policy_.permits(entry.first.first,source,ServeHistory)&&policy_.permits(entry.first.first,local_.peerId,ReadHistory))return entry.first.first;
        std::string reason="History Sync admission denied";
        for(const auto& issuer:policy_.authorityReferences()){
            reason+="; "+issuer.substr(0,12)+": source="+policy_.admissionReason(issuer,source,ServeHistory)
                +", reader="+policy_.admissionReason(issuer,local_.peerId,ReadHistory);
        }
        if(policy_.authorityReferences().empty())reason+="; no trusted authorizer recorded";
        throw std::runtime_error(reason);
    }
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
        // Retain only fully verified authenticated metadata. This directory is
        // historical evidence, never a source of friend/DM authorization.
        for(const auto& entry:descriptors){
            auto prior=storage_.historicalDeviceAccount(entry.first);require(!prior||*prior==account_);
        }
        for(const auto& entry:descriptors)storage_.rememberAuthenticatedDeviceAccount(entry.first,account_);
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
            // Presentation-only eligibility; issuing/using a grant still invokes
            // the existing native consent and admission checks. OFF does not
            // erase approval, and separate self-grants are not pair approval.
            auto valid=[&](const std::string& issuer,const std::string& subject)->std::uint64_t{
                auto server=current_.find({issuer,subject});auto cached=policy_.cachedGrant(issuer,subject);auto key=policy_.authorityKey(issuer);
                if(server==current_.end()||!server->second.verified||!server->second.eligible||server->second.grant.revoked||!cached||!key||!policy_.trustsAuthority(*key)||!member(account_,issuer)||!member(account_,subject))return 0;
                const auto& g=server->second.grant;if(cached->revoked||cached->generation!=g.generation||cached->issuanceId!=g.issuanceId||cached->capabilities!=g.capabilities)return 0;
                return g.capabilities;
            };
            row.mayAuthorize=member(account_,local_.peerId)&&policy_.trustsAuthority(local_.publicKey);
            // First-use is not recovery from disabled trust/revocation. Never
            // offer bootstrap over any retained authority or server grant.
            row.maySetUp=row.local&&member(account_,local_.peerId)&&current_.empty()&&policy_.authorityReferences().empty()&&policy_.grantReferences().empty()&&policy_.pendingPublications().empty();
            if(row.local)for(const auto& item:current_){
                const auto& g=item.second.grant;if(g.device!=local_.peerId||!item.second.verified||!item.second.eligible||g.revoked||!g.capabilities||policy_.authorityKey(g.issuer))continue;
                auto self=current_.find({g.issuer,g.issuer});if(self==current_.end()||!self->second.verified||!self->second.eligible||self->second.grant.revoked)continue;
                auto own=self->second.grant.capabilities;if(((own&ServeHistory)&&(g.capabilities&ReadHistory))||((own&ReadHistory)&&(g.capabilities&ServeHistory))){row.approvalIssuer=g.issuer;break;}
            } // Explicit confirmation may trust this exact registered issuer; never automatic.
            auto own=valid(local_.peerId,d.device);row.localGrantPresent=own!=0;row.localGrantRead=(own&ReadHistory)!=0;row.localGrantServe=(own&ServeHistory)!=0;
            for(const auto& issuer:policy_.authorityReferences()){
                auto remote=valid(issuer,d.device),local=valid(issuer,local_.peerId);
                row.pairAuthorized=row.pairAuthorized||((remote&ServeHistory)&&(local&ReadHistory))||((remote&ReadHistory)&&(local&ServeHistory));
            }
            for(const auto& grant:current_)if(grant.second.grant.device==d.device){
                row.mayRead=row.mayRead||policy_.permits(grant.first.first,d.device,ReadHistory);
                row.mayServe=row.mayServe||policy_.permits(grant.first.first,d.device,ServeHistory);
                auto authority=policy_.authorityKey(grant.first.first);
                row.revoked=row.revoked||(grant.second.verified&&grant.second.grant.revoked&&authority&&policy_.trustsAuthority(*authority));
                // Saved authorization is independent of the participation OFF
                // switch. OFF suspends use, not the signed current grant.
                row.authorized=row.authorized||(grant.second.verified&&grant.second.eligible&&!grant.second.grant.revoked&&grant.second.grant.capabilities&&authority&&policy_.trustsAuthority(*authority));
                auto cached=policy_.cachedGrant(grant.first.first,d.device);
                if(grant.second.verified&&grant.second.eligible&&!grant.second.grant.revoked&&authority&&policy_.trustsAuthority(*authority)&&cached&&cached->generation==grant.second.grant.generation&&cached->issuanceId==grant.second.grant.issuanceId){row.grantedRead=row.grantedRead||(grant.second.grant.capabilities&ReadHistory);row.grantedServe=row.grantedServe||(grant.second.grant.capabilities&ServeHistory);}
            }
            result.push_back(std::move(row));
        }
        return result;
    }
};
}
