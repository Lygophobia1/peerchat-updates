#pragma once
#include "archive_authorization.hpp"
#include "session_transport_types.hpp"

namespace peerchat::archive {
// A value admission context, not a transport/session owner. Both participants
// explicitly trust the issuer and current signed grants before creating it.
inline session::ReliableAuthorization sessionAuthorization(const Authorization& policy,
        const std::string& issuer,const std::string& source,const std::string& reader){
    require(source!=reader&&(policy.localDevice()==source||policy.localDevice()==reader));
    require(policy.permits(issuer,source,ServeHistory)&&policy.permits(issuer,reader,ReadHistory));
    const auto serving=policy.grant(issuer,source),reading=policy.grant(issuer,reader);
    session::ReliableAuthorization authorization;
    authorization.scope=session::ReliableAuthorizationScope::ArchiveControl;
    authorization.expectedRemoteDeviceId=policy.localDevice()==source?reader:source;
    authorization.archive={policy.account(),serving.generation,reading.generation,
        issuer,source,reader,serving.issuanceId,reading.issuanceId};
    require(authorization.valid());return authorization;
}
// Re-evaluate before each frame, not just TLS establishment. A revoked grant,
// changed generation, stale server snapshot or disabled local issuer fails closed.
inline bool currentSessionAuthorization(const Authorization& policy,
        const session::ReliableAuthorization& expected,const Blob* tlsPublicKey=nullptr){
    try{
        if(expected.scope!=session::ReliableAuthorizationScope::ArchiveControl||!expected.valid())return false;
        const auto current=sessionAuthorization(policy,expected.archive.issuer,expected.archive.source,expected.archive.reader);
        const auto& a=current.archive;const auto& b=expected.archive;
        if(current.expectedRemoteDeviceId!=expected.expectedRemoteDeviceId||a.account!=b.account||
           a.sourceGeneration!=b.sourceGeneration||a.readerGeneration!=b.readerGeneration||
           a.sourceGrantId!=b.sourceGrantId||a.readerGrantId!=b.readerGrantId)return false;
        return !tlsPublicKey||policy.grant(a.issuer,current.expectedRemoteDeviceId).publicKey==*tlsPublicKey;
    }catch(const std::exception&){return false;}
}
inline protocol::Writer sessionBinding(const session::ReliableAuthorization& authorization){
    require(authorization.scope==session::ReliableAuthorizationScope::ArchiveControl&&authorization.valid());
    const auto& s=authorization.archive;
    protocol::Writer w(protocol::ArchiveControl);
    w.string("PeerChat/ArchiveSession/v1");w.number(1);w.number(1); // Bind, not history.
    w.number(s.account);w.string(s.issuer);w.string(s.source);w.string(s.reader);
    w.number(s.sourceGeneration);w.number(s.readerGeneration);
    w.string(s.sourceGrantId);w.string(s.readerGrantId);return w;
}
inline void verifySessionBinding(protocol::Reader& reader,const session::ReliableAuthorization& authorization){
    require(reader.type()==protocol::ArchiveControl);
    const auto expected=sessionBinding(authorization);
    require(reader.data==expected.data);reader.cursor=reader.data.size();reader.end();
}
}
