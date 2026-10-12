#include "../storage.cpp"
#include <fstream>
#include <iostream>

namespace {
struct ObservedRecords final : peerchat::platform::RecordProtector {
    peerchat::platform::WindowsRecordProtector dpapi;
    int encrypted=0,decrypted=0;bool unavailable=false,failReplicaReceipt=false;
    peerchat::Blob protect(const peerchat::Blob& value,const std::string& context) override{
        if(unavailable)throw std::runtime_error("Test record key missing");
        if(failReplicaReceipt&&context.rfind("PeerChat.v1/group-replica/",0)==0)throw std::runtime_error("Test receipt protection failure");
        ++encrypted;return dpapi.protect(value,context);
    }
    peerchat::Blob unprotect(const peerchat::Blob& value,const std::string& context) override{
        if(unavailable)throw std::runtime_error("Test record key missing");
        ++decrypted;return dpapi.unprotect(value,context);
    }
};
template<class F> void rejectedRecord(F call){bool rejected=false;try{call();}catch(const std::exception&){rejected=true;}peerchat::storage_detail::require(rejected,"Bad protected record accepted");}
}
int main(){using namespace peerchat;using namespace storage_detail;try{
    auto root=std::filesystem::current_path()/L"build"/L"android-foundation"/L"profiles"/wide(Storage::newMessageId());
    auto records=std::make_shared<ObservedRecords>();const std::string remote(64,'b');
    Profile profile;StoredMessage message;
    {auto db=std::make_shared<WindowsSqlDatabase>(root.wstring());Storage store(db,records);
        profile=store.profile();message={Storage::newMessageId(),profile.peerId,remote,L"Protected record synthetic secret",42,Delivery::Queued,0,1};
        require(store.append(remote,message),"Provider message insert failed");}
    require(records->encrypted>=2,"Production Storage bypassed record provider");
    for(int i=0;i<32;++i){auto db=std::make_shared<WindowsSqlDatabase>(root.wstring());Storage store(db,records);
        require(store.profile().peerId==profile.peerId&&store.history(remote).size()==1&&store.pending(remote).size()==1,"Provider restart lost records");}
    require(records->decrypted>=96,"Reads bypassed record provider");
    GroupEvent replicaEvent;replicaEvent.groupId=Storage::newMessageId();replicaEvent.logicalOrder=2;
    replicaEvent.kind=GroupEventKind::AuthorizeDevice;replicaEvent.actorAccountId=replicaEvent.subjectAccountId=1;
    replicaEvent.actorPeerId=profile.peerId;replicaEvent.subjectPeerId=remote;replicaEvent.value=L"PeerChat/group-device-authorization/v1";
    signGroupEvent(replicaEvent,profile);
    {auto db=std::make_shared<WindowsSqlDatabase>(root.wstring());Storage store(db,records);
        records->failReplicaReceipt=true;rejectedRecord([&]{store.commitGroupReplicaReceipt(replicaEvent,Blob{1});});records->failReplicaReceipt=false;
        require(store.groupEvents(replicaEvent.groupId).empty()&&store.groupReplicaReceipt(replicaEvent.groupId,replicaEvent.eventId).empty(),"Receipt failure left partial replica state");}
    {auto db=std::make_shared<WindowsSqlDatabase>(root.wstring());Storage store(db,records);
        require(store.groupEvents(replicaEvent.groupId).empty(),"Partial replica event survived restart");
        store.commitGroupReplicaReceipt(replicaEvent,Blob{1});store.commitGroupReplicaReceipt(replicaEvent,Blob{1});
        rejectedRecord([&]{store.commitGroupReplicaReceipt(replicaEvent,Blob{2});});
        require(store.groupEvents(replicaEvent.groupId).size()==1&&store.groupReplicaReceipt(replicaEvent.groupId,replicaEvent.eventId)==Blob{1},"Replica duplicate changed durable evidence");}
    {auto db=std::make_shared<WindowsSqlDatabase>(root.wstring());Storage store(db,records);records->unavailable=true;
        rejectedRecord([&]{store.profile();});rejectedRecord([&]{store.history(remote);});rejectedRecord([&]{store.append(remote,message);});records->unavailable=false;
        require(store.profile().peerId==profile.peerId&&store.pending(remote).size()==1,"Provider failure changed identity/outbox");}
    {auto db=std::make_shared<WindowsSqlDatabase>(root.wstring());rejectedRecord([&]{Storage store(db,std::shared_ptr<platform::RecordProtector>{});});}
    Blob original;
    {WindowsSqlDatabase db(root.wstring());auto query=db.prepare("SELECT b FROM messages WHERE id=?");query->bindText(1,message.id);require(query->row(),"Missing tamper fixture");original=query->bytes(0);}
    std::vector<Blob> damaged{Blob(original.size(),0),Blob(original.begin(),original.begin()+original.size()/2),original};damaged.back()[0]^=255;
    for(const auto& blob:damaged){
        {WindowsSqlDatabase db(root.wstring());auto update=db.prepare("UPDATE messages SET b=? WHERE id=?");update->bindBlob(1,blob);update->bindText(2,message.id);update->row();}
        auto db=std::make_shared<WindowsSqlDatabase>(root.wstring());Storage store(db,records);rejectedRecord([&]{store.history(remote);});
    }
    {WindowsSqlDatabase db(root.wstring());auto update=db.prepare("UPDATE messages SET b=? WHERE id=?");update->bindBlob(1,original);update->bindText(2,message.id);update->row();}
    {Storage store(root.wstring());require(store.history(remote).size()==1,"DPAPI/default cross-read changed");}
    for(const auto& file:std::filesystem::directory_iterator(root)){if(!file.is_regular_file())continue;std::ifstream input(file.path(),std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});require(bytes.find("Protected record synthetic secret")==std::string::npos,"Plaintext leaked");}
    std::cout<<"PASS: production per-instance record protection, 32 restarts, no fallback on missing provider/key, corruption/truncation/tamper, DPAPI cross-read and plaintext scan\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
