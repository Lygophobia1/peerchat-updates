#include "../portable/reliable_endpoint.hpp"
#include "../archive_sync.hpp"
#include <filesystem>
#include <iostream>
#include <set>
using namespace peerchat;
using portable::ReliableEndpoint;
static void check(bool ok){if(!ok)throw std::runtime_error("Archive TLS regression");}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
int main(){try{
    const auto id=Storage::newMessageId();
    const auto root=std::filesystem::current_path()/L"build"/L"tests"/std::wstring(id.begin(),id.end());
    Storage a((root/L"archive").wstring()),b((root/L"reader").wstring());
    auto source=a.profile(),reader=b.profile();
    std::set<std::string> members{source.peerId,reader.peerId};
    std::map<std::string,archive::CurrentGrant> grants;
    bool fresh=true;
    auto current=[&](std::uint64_t account,const std::string& peer){return account==8&&members.count(peer)!=0;};
    auto published=[&](std::uint64_t account,const std::string& issuer,const std::string& peer)->std::optional<archive::CurrentGrant>{
        if(account!=8||issuer!=source.peerId||!fresh||!grants.count(peer))return {};
        return grants.at(peer);
    };
    auto publish=[&](const archive::Grant& grant){grants[grant.device]={grant.generation,grant.capabilities,grant.issuanceId,grant.revoked};};
    archive::Authorization serving(a,source,8,current,published),reading(b,reader,8,current,published);
    serving.approveAuthority(source.publicKey,true);reading.approveAuthority(source.publicKey,true);
    auto serve=serving.issue(source.publicKey,archive::ServeHistory,false,100,true);
    auto read=serving.issue(reader.publicKey,archive::ReadHistory,false,100,true);
    publish(serve);publish(read);reading.accept(serve);reading.accept(read);
    TlsCredentials ac(source),bc(reader);
    delivery::DeliveryManager ad(a,source.peerId),bd(b,reader.peerId);
    session::SessionTransportManager as("archive",source.peerId,{1}),bs("reader",reader.peerId,{1});
    reliable::AuthenticatedFriendSnapshot af,bf; // Deliberately no friendship.
    auto route=[](std::uint64_t token){return session::RouteObservation{{token},"relay:test","server","opaque TLS",false,0,session::TransportKind::Relay,session::RouteClass::ServerRelay};};
    std::uint64_t now=1;unsigned ready=0;
    auto drain=[&](ReliableEndpoint& endpoint){while(auto event=endpoint.poll()){
        check(event->kind!=ReliableEndpoint::EventKind::TrustRequired&&event->kind!=ReliableEndpoint::EventKind::Message);
        if(event->kind==ReliableEndpoint::EventKind::Ready)++ready;
    }};
    auto exchange=[&](ReliableEndpoint& left,ReliableEndpoint& right){for(unsigned n=0;n<100;++n){
        drain(left);drain(right);auto f=left.takeOutput(),r=right.takeOutput();
        if(f.empty()&&r.empty())break;
        if(!f.empty())right.receive(f,++now);
        if(!r.empty())left.receive(r,++now);
    }drain(left);drain(right);};
    auto admissionA=[&]{return ReliableEndpoint::ArchiveAdmission{&serving,archive::sessionAuthorization(serving,source.peerId,source.peerId,reader.peerId)};};
    auto admissionB=[&]{return ReliableEndpoint::ArchiveAdmission{&reading,archive::sessionAuthorization(reading,source.peerId,source.peerId,reader.peerId)};};
    for(unsigned cycle=0;cycle<10;++cycle){
        auto token=std::uint64_t(cycle*2+1);
        ReliableEndpoint left(source,a,as,ad,af,{1},{token},std::make_unique<TlsSession>(ac,false),reader.peerId,route(token),++now,admissionA());
        ReliableEndpoint right(reader,b,bs,bd,bf,{1},{token+1},std::make_unique<TlsSession>(bc,true),source.peerId,route(token+1),++now,admissionB());
        left.receive({},++now);right.receive({},++now);exchange(left,right);
        check(left.ready()&&right.ready()&&!left.initialPin(0,now));
        check(a.contacts().empty()&&b.contacts().empty()&&!as.snapshot(reader.peerId)->connected);
    }
    check(ready==20);
    // Real peer TLS carries bounded history without friendship, contact pins,
    // a second session owner, or a DM delivery authorization.
    Storage friendStore((root/L"friend").wstring());auto contact=friendStore.profile();
    auto directory=[&](const std::string& peer)->std::optional<std::uint64_t>{
        if(peer==contact.peerId)return 9;
        if(members.count(peer))return 8;
        return {};
    };
    for(unsigned n=0;n<25;++n){
        StoredMessage m{Storage::newMessageId(),contact.peerId,source.peerId,L"TLS archive test",200,Delivery::Delivered,0,n+1};
        check(a.append(contact.peerId,m));
    }
    {
        auto scopedA=admissionA(),scopedB=admissionB();
        archive::SyncReader history(b,reader,reading,bd,scopedB.scope,directory);
        scopedA.control=[&](const Blob& bytes){return archive::serveSyncControl(a,source,serving,scopedA.scope,bytes,directory,300);};
        scopedB.control=[&](const Blob& bytes){history.accept(bytes);return Blob{};};
        ReliableEndpoint sender(source,a,as,ad,af,{1},{90},std::make_unique<TlsSession>(ac,false),reader.peerId,route(90),++now,scopedA);
        ReliableEndpoint receiver(reader,b,bs,bd,bf,{1},{91},std::make_unique<TlsSession>(bc,true),source.peerId,route(91),++now,scopedB);
        sender.receive({},++now);receiver.receive({},++now);exchange(sender,receiver);
        unsigned pages=0;
        do{receiver.sendArchiveControl(history.nextRequest());exchange(receiver,sender);++pages;}while(!history.snapshot().complete);
        check(pages==5&&history.snapshot().conversations==1&&history.snapshot().processed==25&&b.history(contact.peerId,100).size()==25&&b.pending(contact.peerId).empty());
        check(a.contacts().empty()&&b.contacts().empty());
        protocol::Writer forbidden(protocol::Text);rejects([&]{receiver.sendArchiveControl(forbidden.data);});
    }
    // A scoped transport must never drain the ordinary DM outbox or accept Text.
    StoredMessage dm{Storage::newMessageId(),source.peerId,reader.peerId,L"test-only DM",200,Delivery::Queued,0,a.nextLogicalOrder()};
    check(a.append(reader.peerId,dm));
    auto attack=std::make_unique<TlsSession>(ac,false);auto raw=attack.get();
    ReliableEndpoint left(source,a,as,ad,af,{1},{101},std::move(attack),reader.peerId,route(101),++now,admissionA());
    ReliableEndpoint right(reader,b,bs,bd,bf,{1},{102},std::make_unique<TlsSession>(bc,true),source.peerId,route(102),++now,admissionB());
    left.receive({},++now);right.receive({},++now);exchange(left,right);check(left.ready()&&right.ready());
    left.pump();check(left.takeOutput().empty()&&a.pending(reader.peerId).size()==1);
    auto text=reliable::textPacket(dm,true);raw->send(text.frame());auto forbidden=raw->takeOutput();
    rejects([&]{right.receive(forbidden,++now);});check(right.closed()&&b.history(source.peerId,100).empty());
    left.close(false);
    // Current-generation disagreement is rejected inside TLS, not promoted to friendship.
    auto altered=admissionB();altered.scope.archive.readerGeneration++;
    rejects([&]{ReliableEndpoint invalid(reader,b,bs,bd,bf,{1},{103},std::make_unique<TlsSession>(bc,true),source.peerId,route(103),++now,altered);});
    auto bound=admissionA();auto frame=archive::sessionBinding(bound.scope);
    auto wrong=bound.scope;wrong.archive.readerGeneration++;auto wrongFrame=archive::sessionBinding(wrong);
    rejects([&]{protocol::Reader parsed(wrongFrame.data);archive::verifySessionBinding(parsed,bound.scope);});
    // An already-open session cannot release queued bytes after revocation/freshness loss.
    ReliableEndpoint pending(source,a,as,ad,af,{1},{104},std::make_unique<TlsSession>(ac,false),reader.peerId,route(104),++now,bound);
    pending.receive({},++now);fresh=false;check(pending.takeOutput().empty()&&pending.closed());
    fresh=true;publish(serving.issue(reader.publicKey,0,true,210,true));
    check(!archive::currentSessionAuthorization(reading,altered.scope));
    check(a.profile().peerId==source.peerId&&b.profile().peerId==reader.peerId);
    std::cout<<"PASS: 12 real mutual peer-TLS archive handshakes, manifest discovery and 25 records over four bounded TLS history pages, exact scope, no friend pin/DM/outbox authority, stale admission and revocation. HOST ONLY\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
