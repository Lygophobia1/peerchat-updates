#pragma once
#include "device_identity_endorsement.hpp"
#include "platform/protected_key_registry.hpp"

namespace peerchat::device_identity {
// One profile/server/account registry. Atomic protected record owns proof, root
// consent and watermarks together; no independently published Contact mutation.
class Directory {
    platform::KeyRegistryStore& store_;
    platform::RecordProtector& protector_;
    mailbox::Verify verify_;
    mailbox::Fingerprint fingerprint_;
    std::string context_,server_;
    std::uint64_t account_;
    struct State {
        Bytes root;
        std::uint64_t generation=0,revision=0;
        std::vector<Bytes> proofs;
        bool revoked=false;
        bool reconciled=false;
        std::vector<std::string> disabled;
    };
    State read() {
        Bytes sealed;if(!store_.read(sealed))return {};
        auto plain=protector_.unprotect(sealed,context_);
        mailbox::Reader r(plain,"PeerChat/account-device-directory/v1");
        require(r.text(64)==server_&&r.number(8)==account_);
        State s;s.root=r.field(72);s.generation=r.number(8);s.revision=r.number(8);
        require(s.root.size()==72&&mailbox::account(s.generation)&&mailbox::account(s.revision));
        auto count=r.number(2);require(count<=32);std::vector<std::string> seen;
        while(count--) {
            auto wire=r.field(2048);auto e=endorsement(wire,verify_,fingerprint_);
            require(e.target.server==server_&&e.target.account==account_&&e.authorityKey==s.root&&
                e.authorityGeneration==s.generation&&e.revision<=s.revision&&
                std::find(seen.begin(),seen.end(),e.target.device)==seen.end());
            seen.push_back(e.target.device);s.proofs.push_back(std::move(wire));
        }
        auto revoked=r.number(1);require(revoked<=1);s.revoked=revoked==1;
        auto reconciled=r.number(1);require(reconciled<=1);s.reconciled=reconciled==1;
        auto disabled=r.number(2);require(disabled<=s.proofs.size());
        while(disabled--){auto device=r.text(64);require(std::find(seen.begin(),seen.end(),device)!=seen.end()&&
            std::find(s.disabled.begin(),s.disabled.end(),device)==s.disabled.end());s.disabled.push_back(device);}
        r.end();return s;
    }
    void write(const State& s) {
        mailbox::Writer w("PeerChat/account-device-directory/v1");w.text(server_);w.number(account_,8);
        w.field(s.root);w.number(s.generation,8);w.number(s.revision,8);w.number(s.proofs.size(),2);
        for(const auto& proof:s.proofs)w.field(proof);
        w.number(s.revoked?1:0,1);w.number(s.reconciled?1:0,1);w.number(s.disabled.size(),2);for(const auto& d:s.disabled)w.text(d);
        require(w.bytes.size()<=mailbox::MaxWire);
        store_.replace(protector_.protect(w.bytes,context_));
    }
    void scope(const Scope& s,std::uint64_t generation,std::uint64_t now) {
        current(s,generation,now);require(s.server==server_&&s.account==account_&&fingerprint_(s.authorityKey)==s.authority);
    }
public:
    Directory(platform::KeyRegistryStore& store,platform::RecordProtector& protector,
              std::string profile,std::string server,std::uint64_t account,
              mailbox::Verify verify,mailbox::Fingerprint fingerprint)
        :store_(store),protector_(protector),verify_(std::move(verify)),fingerprint_(std::move(fingerprint)),
         server_(std::move(server)),account_(account) {
        require(!profile.empty()&&profile.size()<=256&&mailbox::id(server_,64)&&mailbox::account(account_));
        context_="PeerChat/account-device-directory/v1/"+profile+"/"+server_+"/"+std::to_string(account_);
    }
    // confirmedFingerprint must originate in explicit out-of-band UI confirmation,
    // not a directory reply. No implicit replacement/recovery operation exists.
    void bootstrap(const Scope& s,const std::string& confirmedFingerprint,std::uint64_t generation,std::uint64_t now) {
        scope(s,generation,now);require(confirmedFingerprint==s.authority);
        store_.locked([&]{auto old=read();require(old.root.empty()||
            (!old.revoked&&old.root==s.authorityKey&&old.generation==s.authorityGeneration&&s.revision>=old.revision));
            if(!old.root.empty())return;
            old.root=s.authorityKey;old.generation=s.authorityGeneration;old.revision=s.revision;write(old);});
    }
    // Complete authenticated directory, not an event hint. targetCurrent checks
    // exact current signed wire as well as device/generation/active authority.
    void reconcile(const Scope& s,const std::vector<Bytes>& proofs,std::uint64_t generation,std::uint64_t now) {
        scope(s,generation,now);require(proofs.size()<=32);
        store_.locked([&]{auto old=read();require(!old.revoked&&old.root==s.authorityKey&&old.generation==s.authorityGeneration&&s.revision>=old.revision);
            State next=old;next.revision=s.revision;std::vector<std::string> seen;
            for(const auto& wire:proofs) {
                auto e=endorsement(wire,verify_,fingerprint_);
                require(e.target.server==server_&&e.target.account==account_&&e.authorityKey==old.root&&
                    e.authorityGeneration==old.generation&&e.revision<=s.revision&&
                    s.targetCurrent(e.target.device,e.targetGeneration,wire)&&
                    std::find(seen.begin(),seen.end(),e.target.device)==seen.end());
                for(const auto& prior:old.proofs) {
                    auto p=endorsement(prior,verify_,fingerprint_);
                    if(p.target.device==e.target.device){
                        require(e.targetGeneration>=p.targetGeneration&&(e.targetGeneration!=p.targetGeneration||wire==prior));
                        if(std::find(old.disabled.begin(),old.disabled.end(),e.target.device)!=old.disabled.end())
                            require(e.targetGeneration>p.targetGeneration);
                    }
                }
                seen.push_back(e.target.device);
                auto existing=std::find_if(next.proofs.begin(),next.proofs.end(),[&](const Bytes& item){
                    return endorsement(item,verify_,fingerprint_).target.device==e.target.device;});
                if(existing==next.proofs.end())next.proofs.push_back(wire);else *existing=wire;
                next.disabled.erase(std::remove(next.disabled.begin(),next.disabled.end(),e.target.device),next.disabled.end());
            }
            // Retain old proof generations when targets disappear or are revoked;
            // current authorization still denies their use. Never lose rollback
            // watermarks merely because a directory omits an offline device.
            require(next.proofs.size()<=32);
            for(const auto& prior:old.proofs){auto device=endorsement(prior,verify_,fingerprint_).target.device;
                if(std::find(seen.begin(),seen.end(),device)==seen.end()&&
                   std::find(next.disabled.begin(),next.disabled.end(),device)==next.disabled.end())next.disabled.push_back(device);}
            // A same-revision equivocation must not replace previously admitted evidence.
            if(next.revision==old.revision&&old.reconciled)require(next.proofs==old.proofs&&next.disabled==old.disabled);
            next.reconciled=true;
            write(next);
        });
    }
    // Persist authenticated authority revocation without destroying old evidence.
    // Automatic bootstrap/reconciliation cannot undo this tombstone.
    void revokeAuthority(const Scope& s,std::uint64_t generation,std::uint64_t now) {
        current(s,generation,now,false);require(!s.authorityActive&&s.server==server_&&s.account==account_&&fingerprint_(s.authorityKey)==s.authority);
        store_.locked([&]{auto state=read();require(state.root==s.authorityKey&&state.generation==s.authorityGeneration&&
            s.revision>state.revision);state.revoked=true;state.revision=s.revision;write(state);});
    }
    Bytes resolve(const Scope& s,const std::string& device,std::uint64_t generation,std::uint64_t now) {
        scope(s,generation,now);Bytes result;
        store_.locked([&]{auto state=read();require(!state.revoked&&state.reconciled&&state.root==s.authorityKey&&state.generation==s.authorityGeneration&&s.revision==state.revision);
            if(device==s.authority){result=state.root;return;}
            for(const auto& wire:state.proofs) {
                auto e=endorsement(wire,verify_,fingerprint_);
                if(e.target.device==device){require(std::find(state.disabled.begin(),state.disabled.end(),device)==state.disabled.end()&&
                    s.targetCurrent(device,e.targetGeneration,wire));result=e.target.key;return;}
            }
        });require(!result.empty());return result;
    }
};
}
