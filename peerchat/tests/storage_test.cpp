// Includes the implementation to exercise the stored-ciphertext tamper boundary.
// Compile this file alone, not together with storage.cpp.
#include "storage.cpp"
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
            db.acknowledge(sent[0].id,Delivery::Delivered);db.acknowledge(sent[0].id,Delivery::Sent);
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
        }
        // Independent recipient owns an independent local copy of the same ID.
        {Storage receiver((root/L"1").wstring());require(receiver.append(a.peerId,sent[0]),"Independent recipient copy failed");}
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
        std::cout<<"PASS: six persistent identities, profiles, contacts/endpoints, encrypted history, restart, pagination, IDs/dedup, acknowledgements, independent copies, disk scan, tamper rejection.\n";
        // Test-created directory is retained for inspection; contains encrypted synthetic data only.
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
