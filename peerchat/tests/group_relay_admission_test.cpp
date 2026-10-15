#include "../portable/reliable_endpoint.hpp"
#include "../portable/relay_requests.hpp"
#include <filesystem>
#include <iostream>
using namespace peerchat;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    check(argc==2||argc==3,"Isolated profile root required");
    auto root=std::filesystem::path(argv[1]);Storage a((root/"a").wstring()),b((root/"b").wstring());
    auto pa=a.profile(),pb=b.profile();
    {
        session::SessionTransportManager manager(pa.peerId,pa.peerId,{1});
        std::vector<session::ReliableAuthorization> scopes;
        for(unsigned n=0;n<65;++n){
            session::ReliableAuthorization scope{session::ReliableAuthorizationScope::GroupControl,Storage::newMessageId(),1,pb.peerId};
            check(manager.demandGroupRoute(scope,2001,1,1),"Unfinished plans beyond first 32 were starved");scopes.push_back(scope);
        }
        auto attempts=manager.takeDueRelayAttempts(1);check(attempts.size()==65,"Scheduler discarded queued scopes beyond live transport capacity");
        auto scope=scopes.front();auto route=session::RouteObservation{{900},"relay:test","test","opaque",false,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};
        check(manager.registerGroupControlAttempt({1},{900},scope,route,2),"Completion fixture registration failed");
        auto future=scope;++future.rosterGeneration;
        check(manager.finishGroupRoute(future).empty()&&manager.attachablePeer({1},{900}).has_value(),"Wrong-epoch completion retired current route");
        auto finished=manager.finishGroupRoute(scope);check(finished.size()==1&&finished.front().value==900&&!manager.attachablePeer({1},{900}),"Completion did not retire matching route");
        check(manager.demandGroupRoute(future,2003,1,3),"Finished plan prevented newer epoch");
        route.revision={901};check(manager.registerGroupControlAttempt({1},{901},future,route,4),"Revocation fixture registration failed");
        manager.invalidateGroupControl({1},future.groupId,0);
        check(!manager.attachablePeer({1},{901}),"Revocation retained transport");
        ++future.rosterGeneration;check(manager.demandGroupRoute(future,2005,1,5),"Revoked plan prevented newer epoch");
    }
    delivery::DeliveryManager ad(a,pa.peerId),bd(b,pb.peerId);
    session::SessionTransportManager as(pa.peerId,pa.peerId,{1}),bs(pb.peerId,pb.peerId,{1});
    reliable::AuthenticatedFriendSnapshot af,bf;af.replace(1,1,{});bf.replace(2,1,{});
    bool authorized=true;std::uint64_t nextA=1,nextB=100,now=1;
    const auto group=Storage::newMessageId();
    session::ReliableAuthorization ab{session::ReliableAuthorizationScope::GroupControl,group,1,pb.peerId};
    session::ReliableAuthorization ba{session::ReliableAuthorizationScope::GroupControl,group,1,pa.peerId};
    auto current=[&](const session::ReliableAuthorization& s){return authorized&&s.groupId==group&&s.rosterGeneration==1;};
    portable::RelayRequests ar(as,af,{1},nextA,{},current),br(bs,bf,{1},nextB,{},current);
    check(ar.authenticatedNetwork(1,1)&&br.authenticatedNetwork(1,1),"Authenticated network rejected");
    unsigned received=0;
    auto admission=[&](const session::ReliableAuthorization& scope,const Blob& key){
        return portable::ReliableEndpoint::GroupAdmission{scope,
            [&,scope,key](const Blob* presented){return current(scope)&&(!presented||*presented==key);},
            [&](const Blob& bytes,const Blob&,std::uint64_t){protocol::Reader r(bytes);check(r.type()==protocol::GroupRosterEvent,"Unexpected control type");check(r.string()=="admission regression","Unexpected control body");r.end();++received;return std::vector<Blob>{};},
            [](const Blob&,std::uint64_t){return std::vector<Blob>{};}};
    };
    // Match the application path: scheduler registers, rendezvous attaches once,
    // endpoint adopts the exact attempt and learns only its incoming role.
    check(as.demandGroupRoute(ab,now+2000,1,now)&&bs.demandGroupRoute(ba,now+2000,1,now),"Group demand rejected");
    auto aq=ar.take(now,7,32),bq=br.take(now,9,32);check(aq.size()==1&&bq.size()==1,"Scheduler omitted group attempts");
    auto ap=ar.attach(aq.front().id,pb.peerId),bp=br.attach(bq.front().id,pa.peerId);
    check(ap.has_value()&&bp.has_value(),"Attempt attachment rejected");
    check(!ar.attach(aq.front().id,pb.peerId)&&!br.attach(bq.front().id,pa.peerId),"Attempt attachment was reusable");
    bp->route.incoming=true;
    check(as.recognizeGroupControlAttempt({1},ap->id,ab,ap->route)&&bs.recognizeGroupControlAttempt({1},bp->id,ba,bp->route),"Exact scheduled group attempt not recognized");
    auto wrongScope=ab;++wrongScope.rosterGeneration;
    check(!as.recognizeGroupControlAttempt({1},ap->id,wrongScope,ap->route),"Changed epoch recognized");
    wrongScope=ab;wrongScope.groupId=Storage::newMessageId();
    check(!as.recognizeGroupControlAttempt({1},ap->id,wrongScope,ap->route),"Changed group recognized");
    auto wrongRoute=ap->route;wrongRoute.endpointUtf8+="-changed";
    check(!as.recognizeGroupControlAttempt({1},ap->id,ab,wrongRoute),"Changed route recognized");
    if(argc==3){check(std::string(argv[2])=="scheduler-only","Unknown mode");std::cout<<"PASS: scheduler group demand/take/one-shot attach/exact admission and altered scope/route rejection. NO TLS RUNTIME\n";return 0;}
    TlsCredentials ac(pa),bc(pb);
    portable::ReliableEndpoint left(pa,a,as,ad,af,{1},ap->id,std::make_unique<TlsSession>(ac,false),pb.peerId,ap->route,++now,{}, {},admission(ab,pb.publicKey));
    portable::ReliableEndpoint right(pb,b,bs,bd,bf,{1},bp->id,std::make_unique<TlsSession>(bc,true),pa.peerId,bp->route,++now,{}, {},admission(ba,pa.publicKey));
    left.receive({},++now);right.receive({},++now);
    auto exchange=[&]{for(unsigned n=0;n<100;++n){auto x=left.takeOutput(),y=right.takeOutput();if(x.empty()&&y.empty())break;if(!x.empty())right.receive(x,++now);if(!y.empty())left.receive(y,++now);}};
    exchange();check(left.ready()&&right.ready(),"Scheduled group peer TLS failed to authenticate");
    check(a.contacts().empty()&&b.contacts().empty(),"Group session created ordinary friend trust");
    check(!left.initialPin(1,now),"Group session allowed friend initial pin");
    protocol::Writer frame(protocol::GroupRosterEvent);frame.string("admission regression");left.sendGroupControl(frame.data);exchange();check(received==1,"Scoped TLS control was not delivered");
    left.sendGroupControl(frame.data);authorized=false;
    check(left.takeOutput().empty()&&left.closed(),"Revoked authorization released queued output");
    check(right.takeOutput().empty()&&right.closed(),"Revoked authorization left endpoint live");
    std::cout<<"PASS: scheduler take/one-shot attach/adopt -> real Schannel group TLS; no friend pin; current-scope output teardown. HOST ONLY\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
