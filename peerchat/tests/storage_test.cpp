// Includes the implementation to exercise the stored-ciphertext tamper boundary.
// Compile this file alone, not together with storage.cpp.
#include "storage.cpp"
#include "../delivery_manager.hpp"
#include <fstream>
#include <iostream>
#include <set>

int main() {
    using namespace peerchat;
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/wide(Storage::newMessageId());
    try {
        std::vector<Profile> identities;
        for(int i=0;i<6;i++) {
            Storage db((root/std::to_wstring(i)).wstring());auto p=db.profile();
            p.name=L"Private nickname "+std::to_wstring(i);p.listenPort=static_cast<std::uint16_t>(18000+i);db.saveProfile(p);identities.push_back(p);
        }
        std::set<std::string> ids;for(const auto& p:identities)ids.insert(p.peerId);
        require(ids.size()==6,"Installations reused an identity");
        const auto& a=identities[0];const auto& b=identities[1];
        std::vector<StoredMessage> sent;
        {
            Storage db((root/L"0").wstring());
            db.saveContact({b.peerId,L"Private contact name",b.publicKey,{{L"friend.example.test",54321},{L"192.0.2.1",12345}},true});
            for(int i=0;i<12;i++) {
                StoredMessage m{Storage::newMessageId(),a.peerId,b.peerId,L"Secret history text \u2603 https://example.test/",static_cast<std::uint64_t>(1700000000000+i),Delivery::Queued,0};
                require(db.append(b.peerId,m),"First insert failed");require(!db.append(b.peerId,m),"Duplicate was inserted");sent.push_back(m);
            }
            delivery::DeliveryManager deliveries(db,a.peerId);
            require(deliveries.authorizeTransport({101},b.peerId),"Authenticated transport was not authorized");
            require(deliveries.authorizeTransport({202},identities[2].peerId),"Second destination transport was not authorized");
            require(deliveries.submittedMessage(b.peerId,sent[0].id,{101}),"Message submission was not recorded above the socket");
            require(!deliveries.acknowledgeMessage(identities[2].peerId,{202},sent[0].id),"Another destination acknowledged a message it did not own");
            require(deliveries.pendingMessages(b.peerId).size()==12,"Wrong-destination ACK changed the durable outbox");
            require(!deliveries.acknowledgeMessage(b.peerId,{101},Storage::newMessageId()),"Unsent message ID was acknowledged");
            require(deliveries.beginDraining({101}),"Authorized transport did not enter draining state");
            require(!deliveries.submittedMessage(b.peerId,sent[1].id,{101}),"Draining transport accepted a new application send");
            require(deliveries.acknowledgeMessage(b.peerId,{101},sent[0].id),"Authorized draining transport ACK was rejected");
            db.acknowledge(sent[0].id,Delivery::Sent);
            auto changed=sent[0];changed.text=L"conflicting payload";bool rejected=false;
            try{db.append(b.peerId,changed);}catch(const std::runtime_error&){rejected=true;}
            require(rejected,"Conflicting duplicate accepted");
            auto latest=db.history(b.peerId,5);require(latest.size()==5&&latest.front().id==sent[7].id,"Newest history page incorrect");
            auto older=db.history(b.peerId,5,latest.front().sequence);require(older.size()==5&&older.front().id==sent[2].id,"History cursor incorrect");
            auto all=db.history(b.peerId);require(all.size()==12&&all[0].delivery==Delivery::Delivered,"Delivery acknowledgement downgraded");
        }
        {
            Storage reopened((root/L"0").wstring());auto p=reopened.profile();
            require(p.peerId==a.peerId&&p.privateKey==a.privateKey&&p.name==a.name&&p.listenPort==a.listenPort,"Identity/profile did not survive restart");
            auto contacts=reopened.contacts();require(contacts.size()==1&&contacts[0].verified&&contacts[0].endpoints[0].host==L"friend.example.test"&&contacts[0].endpoints[0].port==54321,"Contact did not survive restart");
            auto history=reopened.history(b.peerId);require(history.size()==12&&history[0].text==sent[0].text,"History did not survive restart");
            delivery::DeliveryManager restarted(reopened,a.peerId);
            require(restarted.authorizeTransport({303},b.peerId),"Restart transport was not authorized");
            auto pending=restarted.pendingMessages(b.peerId,100);require(!pending.empty(),"Durable outbox did not survive restart");
            require(restarted.submittedMessage(b.peerId,sent[1].id,{303}),"Restarted outbox message was not eligible to resend");
            restarted.retireTransport({303});
            require(restarted.authorizeTransport({304},b.peerId)&&restarted.submittedMessage(b.peerId,sent[1].id,{304}),"Retired transport prevented resend on its replacement");
            require(restarted.acknowledgeMessage(b.peerId,{304},sent[1].id),"Replacement transport could not complete durable delivery");
        }
        // Independent recipient owns an independent local copy of the same ID.
        {Storage receiver((root/L"1").wstring());delivery::DeliveryManager incoming(receiver,b.peerId);auto received=sent[0];received.delivery=Delivery::Delivered;require(incoming.acceptIncomingMessage(a.peerId,received),"Independent recipient copy failed");require(!incoming.acceptIncomingMessage(a.peerId,received),"Duplicate incoming message escaped durable suppression");}
        // Scan database and surviving sidecars for UTF-8 and UTF-16 message text.
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
            if(!entry.is_regular_file())continue;
            std::ifstream file(entry.path(),std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});
            require(bytes.find("Secret history text")==std::string::npos&&bytes.find("Private nickname")==std::string::npos&&bytes.find("friend.example.test")==std::string::npos,"Plaintext leaked to disk");
            auto text=sent[0].text;std::string utf16(reinterpret_cast<const char*>(text.data()),text.size()*sizeof(wchar_t));
            require(bytes.find(utf16)==std::string::npos,"UTF-16 plaintext leaked to disk");
        }
        {
            Sqlite api;sqlite3* raw{};auto path=(root/L"0"/L"peerchat.sqlite").wstring();
            require(api.open16(path.c_str(),&raw)==0,"Tamper test open failed");
            int rc=api.exec(raw,"UPDATE messages SET b=zeroblob(length(b)) WHERE rowid=1",nullptr,nullptr,nullptr);api.close(raw);require(rc==0,"Tamper test update failed");
            Storage db((root/L"0").wstring());bool rejected=false,diagnosed=false;try{db.history(b.peerId);}catch(const std::runtime_error& error){rejected=true;diagnosed=std::string(error.what()).find("DPAPI Win32 error")!=std::string::npos;}require(rejected,"Altered ciphertext was accepted");require(diagnosed,"DPAPI failure did not retain its Win32 error diagnostic");
        }
        std::cout<<"PASS: six persistent identities, encrypted history, destination-scoped delivery/ACK, draining ACK, restart resend, dedup, pagination, disk scan, and tamper rejection.\n";
        // Test-created directory is retained for inspection; contains encrypted synthetic data only.
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
