#include "../windows_mailbox.hpp"
#include "../reliable_peer_protocol.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace peerchat;
namespace fs=std::filesystem;
static void check(bool value){if(!value)throw std::runtime_error("Eligibility application integration failed");}
static Blob read(const fs::path& path){std::ifstream file(path,std::ios::binary);check(bool(file));return Blob(std::istreambuf_iterator<char>(file),{});}
static void write(const fs::path& path,const Blob& value){std::ofstream file(path,std::ios::binary);check(bool(file));file.write(reinterpret_cast<const char*>(value.data()),static_cast<std::streamsize>(value.size()));check(bool(file));}
static std::string text(const fs::path& path){auto bytes=read(path);return std::string(bytes.begin(),bytes.end());}
static void text(const fs::path& path,const std::string& value){write(path,Blob(value.begin(),value.end()));}
static std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
static reliable::AuthenticatedFriendSnapshot friends(std::uint64_t account,const std::string& primary,const std::vector<std::string>& peers){
    reliable::AuthenticatedFriendSnapshot value;value.replace(account,1,{{account==7?8u:7u,primary,peers}});return value;
}
static session::SessionTransportManager routes(const std::string& device){session::SessionTransportManager value(device,device,{1});value.setDemandDriven(true);return value;}
static void setup(const fs::path& root){
    Storage sender((root/L"sender").wstring()),first((root/L"recipient-1").wstring()),second((root/L"recipient-2").wstring());
    const auto s=sender.profile(),a=first.profile(),b=second.profile();
    write(root/L"sender-public.bin",s.publicKey);write(root/L"recipient-1-public.bin",a.publicKey);write(root/L"recipient-2-public.bin",b.publicKey);
    text(root/L"sender-device.txt",s.peerId);text(root/L"recipient-1-device.txt",a.peerId);text(root/L"recipient-2-device.txt",b.peerId);
    sender.saveContact({a.peerId,L"Pinned recipient 1",a.publicKey,{},true});
    sender.saveContact({b.peerId,L"Pinned recipient 2",b.publicKey,{},true});
    first.saveContact({s.peerId,L"Pinned sender",s.publicKey,{},true});
    second.saveContact({s.peerId,L"Pinned sender",s.publicKey,{},true});
    for(int number=1;number<=2;++number){
        Storage& store=number==1?first:second;const auto& profile=number==1?a:b;
        delivery::DeliveryManager owner(store,profile.peerId);auto known=friends(8,s.peerId,{s.peerId});auto sessions=routes(profile.peerId);
        mailbox::WindowsMailbox adapter((root/(number==1?L"mailbox-1":L"mailbox-2")).wstring(),profile);
        write(root/(number==1?L"binding-1.bin":L"binding-2.bin"),adapter.execute(0,{},"",true,store,owner,known,sessions));
    }
}
static void send(const fs::path& root){
    Storage store((root/L"sender").wstring());auto profile=store.profile();const auto a=text(root/L"recipient-1-device.txt"),b=text(root/L"recipient-2-device.txt");
    const auto unknown=text(root/L"unknown-device.txt");auto known=friends(7,a,{a,b,unknown});
    delivery::DeliveryManager owner(store,profile.peerId);auto sessions=routes(profile.peerId);
    mailbox::WindowsMailbox adapter((root/L"mailbox-sender").wstring(),profile);
    const auto contacts=store.contacts();auto canonical=std::find_if(contacts.begin(),contacts.end(),[&](const Contact& item){return item.peerId==a;});check(canonical!=contacts.end());
    auto unresolved=reliable::queueText(store,profile,&*canonical,a,Storage::newMessageId(),L"No prior snapshot remains unresolved",now(),&known,&owner);
    text(root/L"unresolved-id.txt",unresolved.id);check(store.deliveryTargets(unresolved.id)->selectionUnresolved);
    check(adapter.execute(11,read(root/L"snapshot.bin"),"",false,store,owner,known,sessions)==Blob{1});
    auto message=reliable::queueText(store,profile,&*canonical,a,Storage::newMessageId(),L"Two independent devices \u2603",now(),&known,&owner);
    text(root/L"message-id.txt",message.id);auto target=store.deliveryTargets(message.id);check(target&&!target->selectionUnresolved&&target->targets.size()==2);
    check(target->snapshotId==text(root/L"snapshot-id.txt"));
    check(store.deliveryTargets(unresolved.id)->selectionUnresolved); // A later snapshot never expands an already queued ID.
    auto prepared=adapter.execute(8,read(root/L"keys-batch.bin"),message.id,true,store,owner,known,sessions);
    protocol::Reader list(prepared);check(list.type()==protocol::Accepted&&list.number()==2);
    for(int n=0;n<2;++n){auto binding=list.bytes();auto parsed=mailbox::parseBinding(binding);
        auto wire=adapter.execute(3,binding,message.id,true,store,owner,known,sessions);check(!wire.empty());
        write(root/(parsed.device==a?L"envelope-1.bin":L"envelope-2.bin"),wire);
    }
    list.end();check(!fs::exists(root/L"envelope-3.bin"));
}
static void stored(const fs::path& root){
    Storage store((root/L"sender").wstring());auto profile=store.profile();auto a=text(root/L"recipient-1-device.txt"),b=text(root/L"recipient-2-device.txt");
    auto known=friends(7,a,{a,b,text(root/L"unknown-device.txt")});delivery::DeliveryManager owner(store,profile.peerId);auto sessions=routes(profile.peerId);
    mailbox::WindowsMailbox adapter((root/L"mailbox-sender").wstring(),profile);
    for(int n=1;n<=2;++n){auto wire=read(root/(n==1?L"envelope-1.bin":L"envelope-2.bin"));check(adapter.execute(4,wire,"",true,store,owner,known,sessions)==Blob{1});}
    check(store.message(text(root/L"message-id.txt"))->delivery!=Delivery::Delivered);
}
static void receive(const fs::path& root,int number){
    Storage store((root/(number==1?L"recipient-1":L"recipient-2")).wstring());auto profile=store.profile();auto sender=text(root/L"sender-device.txt");
    auto known=friends(8,sender,{sender});delivery::DeliveryManager owner(store,profile.peerId);auto sessions=routes(profile.peerId);
    mailbox::WindowsMailbox adapter((root/(number==1?L"mailbox-1":L"mailbox-2")).wstring(),profile);
    auto binding=read(root/(number==1?L"binding-1.bin":L"binding-2.bin"));check(adapter.execute(1,binding,"",false,store,owner,known,sessions)==Blob{1});
    auto envelope=read(root/(number==1?L"fetched-1.bin":L"fetched-2.bin"));
    auto first=adapter.execute(5,envelope,"",false,store,owner,known,sessions);protocol::Reader result(first);
    check(result.type()==protocol::Ack&&result.number()==1);auto receipt=result.bytes();check(result.string()==sender&&result.string()==text(root/L"message-id.txt"));result.end();
    write(root/(number==1?L"receipt-1.bin":L"receipt-2.bin"),receipt);
    auto repeated=adapter.execute(5,envelope,"",false,store,owner,known,sessions);protocol::Reader replay(repeated);
    check(replay.type()==protocol::Ack&&replay.number()==0&&replay.bytes()==receipt);replay.string();replay.string();replay.end();
    check(store.history(sender).size()==1);
}
static void reconcile(const fs::path& root,int expected){
    Storage store((root/L"sender").wstring());auto profile=store.profile();auto a=text(root/L"recipient-1-device.txt"),b=text(root/L"recipient-2-device.txt");
    auto known=friends(7,a,{a,b,text(root/L"unknown-device.txt")});delivery::DeliveryManager owner(store,profile.peerId);auto sessions=routes(profile.peerId);
    mailbox::WindowsMailbox adapter((root/L"mailbox-sender").wstring(),profile);
    auto wire=read(root/(expected==1?L"server-receipt-2.bin":L"server-receipt-1.bin"));
    check(adapter.execute(6,wire,"",false,store,owner,known,sessions)==Blob{1});
    auto manifest=store.deliveryTargets(text(root/L"message-id.txt"));check(manifest&&manifest->targets.size()==2);
    int acknowledged=0;for(const auto& item:manifest->targets)if(item.status==DeliveryTargetStatus::Acknowledged)++acknowledged;
    check(acknowledged==expected&&store.message(manifest->messageId)->delivery==(expected==2?Delivery::Delivered:Delivery::Stored));
    check(store.deliveryTargets(text(root/L"unresolved-id.txt"))->selectionUnresolved);
}
int main(int argc,char** argv){try{
    check(argc==3);fs::path root(argv[2]);fs::create_directories(root);std::string action=argv[1];
    if(action=="setup")setup(root);else if(action=="send")send(root);else if(action=="stored")stored(root);
    else if(action=="receive1")receive(root,1);else if(action=="receive2")receive(root,2);
    else if(action=="reconcile1")reconcile(root,1);else if(action=="reconcile2")reconcile(root,2);
    else check(false);
    std::cout<<"PASS native phase "<<action<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
