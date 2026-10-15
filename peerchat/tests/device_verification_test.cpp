#include "../portable/reliable_endpoint.hpp"
#include <filesystem>
#include <iostream>

namespace {
using namespace peerchat;
using portable::ReliableEndpoint;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
Contact contact(const Profile& p){return {p.peerId,p.name,p.publicKey,{},true};}
bool sameContacts(const std::vector<Contact>& a,const std::vector<Contact>& b){
    if(a.size()!=b.size())return false;
    for(size_t n=0;n<a.size();++n)if(a[n].peerId!=b[n].peerId||a[n].name!=b[n].name||a[n].publicKey!=b[n].publicKey||a[n].verified!=b[n].verified||a[n].endpoints.size()!=b[n].endpoints.size())return false;
    return true;
}
struct Pair {
    Storage &a,&b;Profile alice,bob;TlsCredentials ac,bc;
    delivery::DeliveryManager ad,bd;
    session::SessionTransportManager as,bs;
    reliable::AuthenticatedFriendSnapshot af,bf;
    std::uint64_t now=100,began=100;unsigned received=0,delivered=0;
    std::unique_ptr<ReliableEndpoint> left,right;
    static session::RouteObservation route(unsigned id,bool incoming){return {{id},"relay:verification-fixture","authenticated fixture","isolated relay",incoming,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};}
    Pair(Storage& first,Storage& second,const std::string& historical):a(first),b(second),alice(a.profile()),bob(b.profile()),ac(alice),bc(bob),ad(a,alice.peerId),bd(b,bob.peerId),as(alice.peerId,alice.peerId,{1}),bs(bob.peerId,bob.peerId,{1}){
        af.replace(10,7,{{20,bob.peerId,{historical,bob.peerId}}});bf.replace(20,7,{{10,alice.peerId,{alice.peerId}}});
        left=std::make_unique<ReliableEndpoint>(alice,a,as,ad,af,session::SessionEpoch{1},session::TransportInstanceId{1},std::make_unique<TlsSession>(ac,false),bob.peerId,route(1,false),began);
        right=std::make_unique<ReliableEndpoint>(bob,b,bs,bd,bf,session::SessionEpoch{1},session::TransportInstanceId{2},std::make_unique<TlsSession>(bc,true),alice.peerId,route(2,true),began);
    }
    void drain(ReliableEndpoint& endpoint,bool remote){while(auto event=endpoint.poll()){
        if(remote&&event->kind==ReliableEndpoint::EventKind::TrustRequired)require(endpoint.initialPin(7,++now),"Remote fixture initial pin rejected");
        if(event->kind==ReliableEndpoint::EventKind::Message)++received;
        if(event->kind==ReliableEndpoint::EventKind::Delivered)++delivered;
    }}
    void exchange(){for(unsigned n=0;n<100;++n){drain(*left,false);drain(*right,true);auto forward=left->takeOutput(),back=right->takeOutput();if(forward.empty()&&back.empty())break;if(!forward.empty())right->receive(forward,++now);if(!back.empty())left->receive(back,++now);}drain(*left,false);drain(*right,true);}
    void start(){left->receive({},++now);right->receive({},++now);exchange();}
    void challenge(const std::string& historical){auto pins=left->verificationPins(7,++now);require(pins&&*pins==std::vector<std::string>{historical},"Challenge did not expose exact historical pin");require(!left->ready(),"New identity trusted without explicit acceptance");}
};
}
int main(){try{
    auto id=Storage::newMessageId();auto root=std::filesystem::current_path()/L"build"/L"tests"/L"device-verification"/std::wstring(id.begin(),id.end());
    Profile alice,bob,old;std::string queued,newId;
    {
        Storage a((root/L"alice").wstring()),b((root/L"bob-new").wstring()),oldStore((root/L"bob-old").wstring());
        alice=a.profile();bob=b.profile();old=oldStore.profile();a.saveContact(contact(old));
        queued=Storage::newMessageId();auto oldContact=contact(old);reliable::queueText(a,alice,&oldContact,old.peerId,queued,L"queued before verification",1);
        const auto unchanged=a.contacts();
        {Pair p(a,b,old.peerId);require(!p.left->verificationPins(7,p.now),"Pre-TLS endpoint exposed challenge");p.start();p.challenge(old.peerId);
            reliable::InitialPinDecision reason;require(!p.left->initialPin(7,++p.now,&reason)&&reason==reliable::InitialPinDecision::HistoricalPin,"Historical pin silently rotated");
            require(!p.left->verifyPresented(6,bob.peerId,true,++p.now),"Stale generation accepted");
            require(!p.left->verifyPresented(7,old.peerId,true,++p.now),"Wrong fingerprint accepted");
            require(p.left->verifyPresented(7,bob.peerId,false,++p.now)&&p.left->closed(),"Explicit rejection failed to close attempt");
            require(!p.left->verifyPresented(7,bob.peerId,true,++p.now),"Rejected attempt later accepted");}
        require(sameContacts(unchanged,a.contacts()),"Rejection or invalid response changed contacts");
        {Pair p(a,b,old.peerId);p.start();p.challenge(old.peerId);require(!p.left->verificationPins(7,p.began+20000)&&!p.left->verifyPresented(7,bob.peerId,true,p.began+20000),"Expired attempt accepted at deadline");}
        {Pair p(a,b,old.peerId);p.start();p.challenge(old.peerId);p.af.replace(10,8,{{20,bob.peerId,{old.peerId,bob.peerId}}});require(!p.left->verifyPresented(7,bob.peerId,true,++p.now),"Response from replaced authentication accepted");require(!p.left->verificationPins(8,++p.now)&&!p.left->verifyPresented(8,bob.peerId,true,++p.now),"Old endpoint issued new challenge or approval after authentication replacement");}
        {Pair p(a,b,old.peerId);p.start();p.challenge(old.peerId);p.af.replace(10,8,{});require(!p.left->verificationPins(8,++p.now)&&!p.left->verifyPresented(7,bob.peerId,true,++p.now),"Revoked friendship accepted");}
        {Pair p(a,b,old.peerId);p.start();p.challenge(old.peerId);p.as.closeTransport({1},{1},"fixture replaced attempt",false);require(!p.left->verifyPresented(7,bob.peerId,true,++p.now),"Detached/replaced attempt accepted");}
        require(sameContacts(unchanged,a.contacts()),"Stale/expired/revoked/replaced response changed contacts");
        {
            Storage revoked((root/L"revoked-alice").wstring());revoked.saveContact(contact(old));Pair p(revoked,b,old.peerId);p.start();p.challenge(old.peerId);
            require(p.left->verifyPresented(7,bob.peerId,true,++p.now)&&!p.left->outputEmpty(),"Revocation fixture did not buffer Accepted");
            auto pins=revoked.contacts();auto current=std::find_if(pins.begin(),pins.end(),[&](const auto& c){return c.peerId==bob.peerId;});require(current!=pins.end(),"Revocation fixture current pin missing");
            auto pending=reliable::queueText(revoked,p.alice,&*current,bob.peerId,Storage::newMessageId(),L"pending survives revocation",3);
            p.af.replace(10,8,{});require(p.left->takeOutput().empty()&&p.left->closed(),"Revoked friendship emitted already-buffered DM bytes");
            require(revoked.message(pending.id)->recipient==bob.peerId&&revoked.pending(bob.peerId).size()==1,"Revocation discarded or retargeted durable pending message");
        }
        {
            Pair p(a,b,old.peerId);p.start();p.challenge(old.peerId);
            require(reliable::queueRecipient(bob.peerId,a.contacts(),p.af)==old.peerId,"Unverified device redirected new queue away from prior pin");
            require(p.left->verifyPresented(7,bob.peerId,true,++p.now),"Explicit acceptance rejected");p.exchange();require(p.left->ready()&&p.right->ready(),"Explicit acceptance did not activate existing endpoint");
            require(!p.left->verifyPresented(7,bob.peerId,true,++p.now),"Repeated acceptance remained actionable");
            require(reliable::queueRecipient(bob.peerId,a.contacts(),p.af)==bob.peerId,"New messages did not select newly verified current device");
            require(a.message(queued)->recipient==old.peerId&&a.pending(old.peerId).size()==1&&!b.message(queued),"Verification migrated an old queued recipient");
            auto contacts=a.contacts();auto current=std::find_if(contacts.begin(),contacts.end(),[&](const auto& c){return c.peerId==bob.peerId;});require(current!=contacts.end()&&current->verified&&current->publicKey==bob.publicKey,"Acceptance did not persist exact TLS key");
            newId=Storage::newMessageId();reliable::queueText(a,alice,&*current,bob.peerId,newId,L"new verified device only",2);p.left->pump();auto bytes=p.left->takeOutput();require(!bytes.empty(),"Verified current send missing");p.right->receive(bytes,++p.now);p.drain(*p.right,true);require(!p.right->takeOutput().empty(),"No recipient ACK to drop");
            require(a.pending(bob.peerId).size()==1&&b.message(newId)&&p.received==1,"Disconnect-before-ACK durable fixture failed");p.left->close(true);p.right->close(true);
        }
    }
    {
        Storage a((root/L"alice").wstring()),b((root/L"bob-new").wstring());require(a.profile().peerId==alice.peerId&&b.profile().peerId==bob.peerId,"Profile reopen changed identity");
        auto contacts=a.contacts();require(contacts.size()==2,"Reopen lost historical/current pins");for(const auto& c:contacts)require(c.verified&&Storage::publicKeyId(c.publicKey)==c.peerId,"Reopen corrupted trusted pin");
        Pair p(a,b,old.peerId);p.start();require(p.left->ready()&&p.right->ready(),"Reopened verified pins required new approval");require(a.pending(bob.peerId).empty()&&p.received==0&&p.delivered==1,"Reconnect failed exact retry/ACK/dedupe");
        auto history=b.history(alice.peerId,100);require(history.size()==1&&history[0].id==newId&&history[0].text==L"new verified device only","Retry altered message ID/payload or duplicated history");
        require(a.message(queued)->recipient==old.peerId&&a.pending(old.peerId).size()==1&&!b.message(queued),"Reconnect retargeted old queued message");
    }
    std::cout<<"PASS: real Schannel explicit device verification accept/reject, stale auth/fingerprint/deadline/retired attempt/revoked friend, persistent pins/profile reopen, immutable old queued recipient, new verified recipient and exact reconnect retry/ACK/dedupe. HOST ONLY\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
