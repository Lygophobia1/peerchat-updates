#pragma once
#include "mailbox_protocol.hpp"
#include "platform/identity_provider.hpp"
#include <mutex>

namespace peerchat::device_identity {
using mailbox::Bytes;
using mailbox::require;

// Public, one-hop identity evidence only. Never a history/group/cache grant.
// No JNI/general-purpose signer is exposed by this protocol component.
struct Possession {
    std::string server, device;
    std::uint64_t account=0;
    Bytes key, signature;
};
inline Bytes possessionPayload(const Possession& p) {
    require(mailbox::id(p.server,64)&&mailbox::account(p.account)&&
        mailbox::id(p.device,64)&&p.key.size()==72);
    mailbox::Writer w("PeerChat/account-device-possession/v1");
    w.text(p.server);w.number(p.account,8);w.text(p.device);w.field(p.key);return w.bytes;
}
inline Bytes encode(const Possession& p) {
    auto payload=possessionPayload(p);require(p.signature.size()==64);
    mailbox::Writer w("PeerChat/account-device-possession-signed/v1");
    w.field(payload);w.field(p.signature);return w.bytes;
}
inline Possession possession(const Bytes& wire,const mailbox::Verify& verify,const mailbox::Fingerprint& fingerprint) {
    require(wire.size()<=1024);
    mailbox::Reader r(wire,"PeerChat/account-device-possession-signed/v1");
    auto payload=r.field(768);auto signature=r.field(64);r.end();
    mailbox::Reader p(payload,"PeerChat/account-device-possession/v1");
    Possession out;out.server=p.text(64);out.account=p.number(8);out.device=p.text(64);
    out.key=p.field(72);out.signature=signature;p.end();
    require(possessionPayload(out)==payload&&encode(out)==wire&&
        fingerprint(out.key)==out.device&&verify(payload,signature,out.key));return out;
}
struct Endorsement {
    Possession target;
    std::string authority, issuance, session;
    Bytes authorityKey, signature;
    std::uint64_t authorityGeneration=0, targetGeneration=0, revision=0, issued=0, expires=0;
};
inline Bytes payload(const Endorsement& e) {
    require(mailbox::id(e.authority,64)&&e.authority!=e.target.device&&e.authorityKey.size()==72&&
        mailbox::id(e.issuance,64)&&mailbox::id(e.session,64)&&
        mailbox::account(e.authorityGeneration)&&mailbox::account(e.targetGeneration)&&
        mailbox::account(e.revision)&&mailbox::account(e.issued)&&mailbox::account(e.expires)&&
        e.expires>e.issued&&e.expires-e.issued<=60000);
    mailbox::Writer w("PeerChat/account-device-endorsement/v1");
    w.field(encode(e.target));w.text(e.authority);w.field(e.authorityKey);
    w.number(e.authorityGeneration,8);w.number(e.targetGeneration,8);w.number(e.revision,8);
    w.text(e.issuance);w.text(e.session);w.number(e.issued,8);w.number(e.expires,8);
    return w.bytes;
}
inline Bytes encode(const Endorsement& e) {
    require(e.signature.size()==64);mailbox::Writer w("PeerChat/account-device-endorsement-signed/v1");
    w.field(payload(e));w.field(e.signature);return w.bytes;
}
inline Endorsement unsignedEndorsement(const Bytes& input,const mailbox::Verify& verify,const mailbox::Fingerprint& fingerprint) {
    require(input.size()<=1792);
    mailbox::Reader p(input,"PeerChat/account-device-endorsement/v1");Endorsement e;
    e.target=possession(p.field(1024),verify,fingerprint);e.authority=p.text(64);e.authorityKey=p.field(72);
    e.authorityGeneration=p.number(8);e.targetGeneration=p.number(8);e.revision=p.number(8);
    e.issuance=p.text(64);e.session=p.text(64);e.issued=p.number(8);e.expires=p.number(8);
    p.end();require(payload(e)==input&&fingerprint(e.authorityKey)==e.authority);return e;
}
inline Endorsement endorsement(const Bytes& wire,const mailbox::Verify& verify,const mailbox::Fingerprint& fingerprint) {
    require(wire.size()<=2048);mailbox::Reader r(wire,"PeerChat/account-device-endorsement-signed/v1");
    auto input=r.field(1792);auto signature=r.field(64);r.end();
    auto e=unsignedEndorsement(input,verify,fingerprint);e.signature=signature;
    require(encode(e)==wire&&verify(input,signature,e.authorityKey));return e;
}
struct AuthoritySelection {
    Possession identity;
    std::uint64_t generation=0,revision=0,issued=0,expires=0;
    std::string issuance,session;
    Bytes signature;
};
inline Bytes payload(const AuthoritySelection& e) {
    require(e.generation==1&&mailbox::account(e.revision)&&mailbox::account(e.issued)&&
        mailbox::account(e.expires)&&e.expires>e.issued&&e.expires-e.issued<=60000&&
        mailbox::id(e.issuance,64)&&mailbox::id(e.session,64));
    mailbox::Writer w("PeerChat/account-device-authority/v1");w.field(encode(e.identity));
    w.number(e.generation,8);w.number(e.revision,8);w.text(e.issuance);w.text(e.session);
    w.number(e.issued,8);w.number(e.expires,8);return w.bytes;
}
inline Bytes encode(const AuthoritySelection& e) {
    require(e.signature.size()==64);mailbox::Writer w("PeerChat/account-device-authority-signed/v1");
    w.field(payload(e));w.field(e.signature);return w.bytes;
}
inline AuthoritySelection unsignedAuthority(const Bytes& input,const mailbox::Verify& verify,const mailbox::Fingerprint& fingerprint) {
    require(input.size()<=1792);mailbox::Reader r(input,"PeerChat/account-device-authority/v1");AuthoritySelection e;
    e.identity=possession(r.field(1024),verify,fingerprint);e.generation=r.number(8);e.revision=r.number(8);
    e.issuance=r.text(64);e.session=r.text(64);e.issued=r.number(8);e.expires=r.number(8);r.end();
    require(payload(e)==input);return e;
}
inline AuthoritySelection authority(const Bytes& wire,const mailbox::Verify& verify,const mailbox::Fingerprint& fingerprint) {
    require(wire.size()<=2048);mailbox::Reader r(wire,"PeerChat/account-device-authority-signed/v1");
    auto input=r.field(1792);auto sig=r.field(64);r.end();auto e=unsignedAuthority(input,verify,fingerprint);e.signature=sig;
    require(encode(e)==wire&&verify(input,sig,e.identity.key));return e;
}

// Supplied by an authenticated adapter, not decoded from untrusted endorsement
// bytes. Directory membership is necessary but never sufficient to bootstrap trust.
struct Scope {
    std::string server, authority, session;
    Bytes authorityKey;
    std::uint64_t account=0, authorityGeneration=0, revision=0, localGeneration=0, validUntil=0;
    bool authorityActive=false;
    std::function<bool(const std::string&,std::uint64_t,const Bytes&)> targetCurrent;
};
inline void current(const Scope& s,std::uint64_t localGeneration,std::uint64_t now,bool requireActive=true) {
    require(mailbox::id(s.server,64)&&mailbox::account(s.account)&&mailbox::id(s.authority,64)&&
        s.authorityKey.size()==72&&mailbox::account(s.authorityGeneration)&&mailbox::account(s.revision)&&
        mailbox::id(s.session,64)&&s.localGeneration!=0&&s.localGeneration==localGeneration&&
        now<s.validUntil&&(!requireActive||s.authorityActive)&&bool(s.targetCurrent));
}
// Explicit approval is represented by an immutable selection captured by the UI.
// A request/challenge cannot supply or change these expected values itself.
struct Approval {
    std::string server, authority, target, issuance;
    Bytes targetKey;
    std::uint64_t account=0, authorityGeneration=0, targetGeneration=0, revision=0, localGeneration=0;
    std::string session;
    std::uint64_t issued=0,expires=0;
};
class ScopedSigner {
    platform::IdentityProvider& identity_;
    mailbox::Verify verify_;
    mailbox::Fingerprint fingerprint_;
    std::vector<std::string> consumed_;
    std::mutex gate_;
public:
    ScopedSigner(platform::IdentityProvider& identity,mailbox::Verify verify,mailbox::Fingerprint fingerprint)
        :identity_(identity),verify_(std::move(verify)),fingerprint_(std::move(fingerprint)){}
    Bytes sign(Endorsement request,const Approval& approved,const Scope& s,std::uint64_t generation,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(gate_);
        current(s,generation,now);
        auto proof=possession(encode(request.target),verify_,fingerprint_);
        require(request.signature.empty()&&proof.server==s.server&&proof.account==s.account&&
            request.authority==s.authority&&request.authorityKey==s.authorityKey&&
            identity_.publicKey()==s.authorityKey&&fingerprint_(s.authorityKey)==s.authority&&
            request.authorityGeneration==s.authorityGeneration&&request.revision==s.revision&&
            request.session==s.session&&request.issued<=now&&now<request.expires&&
            approved.server==s.server&&approved.account==s.account&&approved.authority==s.authority&&
            approved.authorityGeneration==s.authorityGeneration&&approved.revision==s.revision&&
            approved.localGeneration==generation&&approved.target==proof.device&&approved.targetKey==proof.key&&
            approved.targetGeneration==request.targetGeneration&&approved.issuance==request.issuance&&
            approved.session==request.session&&approved.issued==request.issued&&approved.expires==request.expires&&
            s.targetCurrent(proof.device,request.targetGeneration,{})&&consumed_.size()<1024&&
            std::find(consumed_.begin(),consumed_.end(),request.issuance)==consumed_.end());
        auto input=payload(request);
        consumed_.push_back(request.issuance); // failure cannot turn an approval into a replayable signer
        request.signature=identity_.sign(input);
        require(verify_(input,request.signature,s.authorityKey));return encode(request);
    }
};
}
