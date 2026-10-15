#include "../node.hpp"
#include "../portable/group_application.hpp"
#include "../portable/reliable_endpoint.hpp"
#include <wincrypt.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace peerchat;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static Blob unbase64(const std::wstring& text){DWORD size=0;check(CryptStringToBinaryW(text.c_str(),static_cast<DWORD>(text.size()),CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr),"base64 size");Blob bytes(size);check(CryptStringToBinaryW(text.c_str(),static_cast<DWORD>(text.size()),CRYPT_STRING_BASE64,bytes.data(),&size,nullptr,nullptr),"base64 bytes");bytes.resize(size);return bytes;}
struct AndroidSide {
    Storage store;Profile profile;delivery::DeliveryManager deliveries;
    portable::GroupApplication app;session::SessionTransportManager sessions;
    reliable::AuthenticatedFriendSnapshot friends;TlsCredentials credentials;
    std::unique_ptr<portable::ReliableEndpoint> endpoint;
    AndroidSide(const std::filesystem::path& path,std::uint64_t account):store(path.wstring()),profile(store.profile()),
        deliveries(store,profile.peerId),app(store,profile,deliveries,account,true),sessions(profile.peerId,profile.peerId,{1}),credentials(profile){friends.replace(account,1,{});}
    void connect(const group_keys::RosterSnapshot& row,std::uint64_t token,const std::string& target={},bool server=true){
        const auto remote=target.empty()?row.distributorPeerId:target;
        session::ReliableAuthorization scope{session::ReliableAuthorizationScope::GroupControl,row.serverGroupId,row.generation,remote};
        auto state=std::make_shared<std::shared_ptr<portable::GroupApplication::Control>>();
        auto control=[this,scope,token,state](const Blob& key)->portable::GroupApplication::Control&{
            if(!*state)*state=std::make_shared<portable::GroupApplication::Control>(app,scope,session::TransportInstanceId{token},key);
            return **state;};
        portable::ReliableEndpoint::GroupAdmission admission{scope,[this,scope](const Blob* key){return app.current(scope,key);},
            [control](const Blob& bytes,const Blob& key,std::uint64_t now){return control(key).receive(bytes,now);},
            [control](const Blob& key,std::uint64_t now){return control(key).outgoing(now);},
            [state]{return *state&&(*state)->complete();},
            [control](const Blob& key,std::uint64_t now,std::uint64_t caps){return control(key).historyOutgoing(now,caps);}};
        session::RouteObservation route{{token},"relay:windows-android","authenticated roster","native interop",false,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};
        endpoint=std::make_unique<portable::ReliableEndpoint>(profile,store,sessions,deliveries,friends,session::SessionEpoch{1},session::TransportInstanceId{token},
            std::make_unique<TlsSession>(credentials,server),remote,route,1,std::nullopt,portable::ReliableEndpoint::TransferReceiver{},std::move(admission));
        endpoint->receive({},2);
    }
};
int main(int argc,char** argv){Node windows;try{
    check(argc==2,"Isolated profile root required");const auto root=std::filesystem::path(argv[1]);
    AndroidSide b(root/"android-b",2),c(root/"android-c",3);std::array<AndroidSide*,2> android{{&b,&c}};
    windows.start((root/"windows").wstring(),0,L"Isolated Windows owner",false);
    std::string owner;std::array<std::uint64_t,2> attempts{};std::array<bool,2> attached{};
    std::set<std::string> acked;unsigned receipts=0,duplicates=0,windowsMessages=0;std::uint64_t tick=3,rosterGeneration=3;
    bool bOffline=false;Blob windowsWire;const std::wstring text=L"Native interop \U0001F44B \U0001F680 e\u0301 \u65e5\u672c\u8a9e \u041f\u0440\u0438\u0432\u0435\u0442\nsecond line";
    auto collect=[&]{for(const auto& event:windows.poll()){
        if(event.kind==NodeEvent::Kind::Ready){check(event.port==0,"Ordinary listener opened");owner=event.peerId;windows.setNetworkContext(1,L"isolated relay");windows.replaceAuthenticatedFriends(1,1,{});windows.setGroupAccount(1,L"Owner");}
        else if(event.kind==NodeEvent::Kind::RelayRequested){check(event.authorizationRosterGeneration==rosterGeneration,"Stale relay authority");bool known=false;for(size_t i=0;i<2;i++)if(event.peerId==android[i]->profile.peerId){attempts[i]=event.attemptId;known=true;}check(known,"Unknown group peer requested");}
        else if(event.kind==NodeEvent::Kind::ConnectionLifecycle&&event.text==L"Group key offer acknowledged by member")acked.insert(event.peerId);
        else if(event.kind==NodeEvent::Kind::GroupCiphertext){windowsWire=unbase64(event.detail);for(auto* peer:android)if(peer->app.generation()&&!(bOffline&&peer==&b))check(peer->app.receive(windowsWire)==group_messages::Result::Accepted,"Android rejected Windows PCGE");}
        else if(event.kind==NodeEvent::Kind::GroupCiphertextAck){if(event.text==L"accepted")++receipts;else if(event.text==L"duplicate_message")++duplicates;else throw std::runtime_error("Windows rejected Android PCGE");}
        else if(event.kind==NodeEvent::Kind::GroupMessage){check(event.groupEvents.size()==1&&event.groupEvents[0].message==text,"Windows Unicode display mismatch");++windowsMessages;}
        else if(event.kind==NodeEvent::Kind::TrustRequired)throw std::runtime_error("Group transport requested friendship trust");
        else if(event.kind==NodeEvent::Kind::Error){std::wcerr<<event.text<<L'\n';throw std::runtime_error("Windows native group error");}
    }};
    auto pump=[&]{collect();unsigned char buffer[65536];for(size_t i=0;i<2;i++)if(attached[i]){
        for(;;){int n=windows.takeRelayOutput(attempts[i],buffer,sizeof(buffer),0);if(n<=0)break;android[i]->endpoint->receive(Blob(buffer,buffer+n),++tick);}
        auto bytes=android[i]->endpoint->takeOutput();if(!bytes.empty())check(windows.relayReceive(attempts[i],bytes.data(),bytes.size()),"Windows rejected relay bytes");
    }};
    auto wait=[&](auto done,const char* why){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(25);while(std::chrono::steady_clock::now()<until){pump();if(done())return;std::this_thread::sleep_for(std::chrono::milliseconds(3));}throw std::runtime_error(why);};
    wait([&]{return !owner.empty();},"Windows initialization timed out");
    const auto legacy=windows.createGroup(L"Windows Android \U0001F680");std::wstring proof;
    wait([&]{proof=windows.groupCreationProof(legacy);return !proof.empty();},"Create proof missing");
    auto domain=std::string("PeerChat group roster v1|")+legacy+"|"+owner;
    group_keys::RosterSnapshot roster;roster.legacyGroupId=legacy;roster.serverGroupId=Storage::publicKeyId(Blob(domain.begin(),domain.end())).substr(0,32);
    roster.distributorPeerId=owner;roster.generation=3;roster.creationProof=unbase64(proof);
    roster.devices={{1,owner,GroupRole::Owner,false},{2,b.profile.peerId,GroupRole::Member,false},{3,c.profile.peerId,GroupRole::Member,false}};
    check(windows.setAuthenticatedGroupRoster(1,1,roster),"Windows roster rejected");
    for(size_t i=0;i<2;i++){android[i]->app.authenticated(1,{roster});check(windows.beginGroupControl(roster.serverGroupId,i+2,android[i]->profile.peerId,1),"Windows control request rejected");}
    wait([&]{return attempts[0]&&attempts[1];},"Windows did not request both scoped relays");
    for(size_t i=0;i<2;i++){android[i]->connect(roster,101+i);check(windows.relayReady(attempts[i],false),"Windows scoped relay attach failed");attached[i]=true;}
    wait([&]{return acked.size()==2;},"Windows/Android native key handoff did not converge");
    for(auto* peer:android){check(peer->endpoint->ready(),"Android endpoint not ready");check(peer->store.groupKey(legacy,3).size()==32,"Android content key missing");check(!peer->endpoint->initialPin(1,++tick),"Group endpoint allowed friend pin");}
    check(windows.sendGroup(legacy,text),"Windows composer failed");wait([&]{return !windowsWire.empty();},"Windows PCGE missing");
    Blob androidWire;
    for(size_t i=0;i<2;i++){
        auto sent=android[i]->app.send(roster.serverGroupId,text,3000+i);check(sent.result==group_messages::Result::Accepted,"Android native encrypted send failed");androidWire=sent.wire;
        check(windows.receiveGroupCiphertext(sent.wire),"Windows ingress rejected");check(android[1-i]->app.receive(sent.wire)==group_messages::Result::Accepted,"Other Android rejected PCGE");
        wait([&]{return receipts==i+1;},"Windows PCGE authentication/receipt timed out");
    }
    check(windows.receiveGroupCiphertext(androidWire),"Windows duplicate ingress failed");wait([&]{return duplicates==1;},"Windows durable duplicate receipt missing");
    check(windowsMessages==3,"Windows did not project exactly three messages");
    check(b.app.receive(windowsWire)==group_messages::Result::DuplicateMessage,"Android duplicate not rejected");
    auto tamper=windowsWire;tamper.back()^=1;check(b.app.receive(tamper)!=group_messages::Result::Accepted,"Android accepted modified Windows ciphertext");
    for(auto* peer:android){auto history=peer->store.authenticatedGroupHistory(legacy);check(history.size()==3,"Android history missing message");for(const auto& row:history)check(group_text::decode(row.plaintext).text==text,"Android Unicode history mismatch");check(peer->store.contacts().empty(),"Android added friend trust");}
    // Deliberately do not deliver these through live fanout. The existing scoped
    // peer TLS route must import the missing PCGE records in BOTH directions.
    attached[0]=false;bOffline=true;windows.relayClosed(attempts[0],L"Isolated group history interruption");b.endpoint->close(false);b.endpoint.reset();attempts[0]=0;
    for(unsigned n=0;n<7;++n){windowsWire.clear();check(windows.sendGroup(legacy,text),"Offline Windows group send");wait([&]{return !windowsWire.empty();},"Offline group message was not created");}
    for(unsigned n=0;n<5;++n)check(b.app.send(roster.serverGroupId,text,4000+n).result==group_messages::Result::Accepted,"Offline Android durable creation");
    check(b.store.groupArchiveWatermark()==8,"Offline messages leaked through live fanout");
    check(windows.beginGroupControl(roster.serverGroupId,2,b.profile.peerId,1),"History reconnect rejected");wait([&]{return attempts[0]!=0;},"History relay request missing");b.connect(roster,104);check(windows.relayReady(attempts[0],false),"History relay attach failed");attached[0]=true;
    wait([&]{return b.store.groupArchiveWatermark()==15;},"Windows-to-Android nonempty TLS history import failed");
    // A selected Windows history read is the real Node projection, not a direct
    // test import or fabricated receiver acknowledgment.
    windows.loadGroupHistory(legacy,INT64_MAX);
    for(unsigned n=0;n<50;++n){pump();std::this_thread::sleep_for(std::chrono::milliseconds(3));}
    bOffline=false;
    attached={};b.endpoint.reset();c.endpoint.reset();windows.stop();
    // Original source really is stopped. Two already authorized members must
    // reconcile through the same group-scoped native TLS adapter without it.
    b.connect(roster,201,c.profile.peerId,false);c.connect(roster,202,b.profile.peerId,true);
    auto fallbackUntil=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    while(std::chrono::steady_clock::now()<fallbackUntil&&c.store.groupArchiveWatermark()!=15){
        auto left=b.endpoint->takeOutput(),right=c.endpoint->takeOutput();
        if(!left.empty())c.endpoint->receive(left,++tick);
        if(!right.empty())b.endpoint->receive(right,++tick);
        b.endpoint->pump();c.endpoint->pump();std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    check(c.store.groupArchiveWatermark()==15,"Fallback group source did not recover history while original source was offline");
    b.endpoint.reset();c.endpoint.reset();owner.clear();windows.start((root/"windows").wstring(),0,L"Isolated Windows owner",false);
    wait([&]{return !owner.empty();},"Windows source restart timed out");check(windows.setAuthenticatedGroupRoster(1,1,roster),"Reopened source roster rejected");
    // Recreate the adapter using durable protected membership/history. Neither
    // disk persistence nor a successful earlier TLS channel grants live authority.
    portable::GroupApplication reopened(b.store,b.profile,b.deliveries,2);
    check(reopened.receive(windowsWire)==group_messages::Result::RosterUnavailable,"Restart retained live authority");
    reopened.authenticated(2,{roster});check(reopened.receive(windowsWire)==group_messages::Result::DuplicateMessage,"Restart lost durable dedupe");
    // Actual signed Windows removal rotates keys only to the remaining adapter.
    attached={};b.endpoint.reset();c.endpoint.reset();attempts={};acked.clear();rosterGeneration=4;
    roster.generation=4;roster.devices.pop_back();b.app.authenticated(2,{roster});c.app.authenticated(2,{});
    check(windows.setAuthenticatedGroupRoster(1,1,roster)&&windows.removeGroupMember(legacy,3),"Windows removal rejected");
    check(windows.beginGroupControl(roster.serverGroupId,2,b.profile.peerId,1),"Rotated Windows control request rejected");
    wait([&]{return attempts[0]!=0;},"Rotated relay request missing");b.connect(roster,103);check(windows.relayReady(attempts[0],false),"Rotated relay attach failed");attached[0]=true;
    wait([&]{return acked.count(b.profile.peerId)>0;},"Rotated key not acknowledged by Android");
    check(b.store.groupKey(legacy,4).size()==32&&b.store.groupKey(legacy,4)!=b.store.groupKey(legacy,3),"Android rotation failed");
    check(c.store.groupKey(legacy,4).empty(),"Removed Android obtained future key");check(c.app.receive(windowsWire)==group_messages::Result::RosterUnavailable,"Revoked Android retained ingress authority");
    windows.stop();Storage ownerStore((root/"windows").wstring());check(ownerStore.contacts().empty(),"Windows added friend trust");
    check(ownerStore.authenticatedGroupHistory(legacy).size()==15,"Android-to-Windows protected history import/reopen failed");
    check(ownerStore.groupKey(legacy,4)==b.store.groupKey(legacy,4),"Windows and Android rotated keys differ");
    check(!ownerStore.hasGroupKeyReceipt(legacy,4,c.profile.peerId),"Removed device has future key receipt");
    std::cout<<"PASS: Windows Node and Android native adapters over real Schannel TLS; signed key ACK, Unicode PCGE, 7 missed Windows and 5 missed Android messages recovered without live fanout, member-source fallback with Windows stopped, durable history/reopen, removal rotation and no friend auto-trust. HOST ONLY\n";return 0;
}catch(const std::exception& e){windows.stop();std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
