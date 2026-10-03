#include "../storage.cpp"
#include <fstream>
#include <iostream>

template<class F> void rejectSql(F call){bool rejected=false;try{call();}catch(const std::exception&){rejected=true;}peerchat::storage_detail::require(rejected,"Invalid SQL/storage operation accepted");}
int main(){using namespace peerchat;using namespace storage_detail;try{
    auto root=std::filesystem::current_path()/L"build"/L"android-foundation"/L"profiles"/wide(Storage::newMessageId());
    Profile profile;StoredMessage message;const std::string remote(64,'b');
    {Storage legacy(root.wstring());profile=legacy.profile();message={Storage::newMessageId(),profile.peerId,remote,L"Provider SQL persistence",42,Delivery::Queued,0,17};require(legacy.append(remote,message),"Original insert failed");}
    for(int i=0;i<32;++i){
        auto database=std::make_shared<WindowsSqlDatabase>(root.wstring());
        Storage reopened(std::static_pointer_cast<platform::SqlDatabase>(database));
        require(reopened.profile().peerId==profile.peerId,"SQL adapter rotated identity");
        auto page=reopened.history(remote,1);require(page.size()==1&&page[0].id==message.id&&page[0].logicalOrder==17,"SQL restart lost history/order");
        require(!reopened.append(remote,message),"Restart duplicate inserted");
        require(reopened.pending(remote).size()==1,"Restart lost durable outbox");
        rejectSql([&]{reopened.history(remote,1001);});
    }
    {
        auto first=std::make_shared<WindowsSqlDatabase>(root.wstring());
        auto second=std::make_shared<WindowsSqlDatabase>(root.wstring());
        Storage a(std::static_pointer_cast<platform::SqlDatabase>(first)),b(std::static_pointer_cast<platform::SqlDatabase>(second));
        {platform::SqlTransaction lock(*first);rejectSql([&]{b.nextLogicalOrder();});}
        require(b.nextLogicalOrder()>0,"Busy rollback prevented later transaction");
        a.acknowledge(message.id,Delivery::Delivered);require(b.pending(remote).empty(),"ACK not persisted across connections");
    }
    rejectSql([&]{Storage invalid{std::shared_ptr<platform::SqlDatabase>{}};});
    auto broken=root/L"broken";std::filesystem::create_directories(broken);
    {std::ofstream file(broken/L"peerchat.sqlite",std::ios::binary);file<<"Not a SQLite database";}
    rejectSql([&]{Storage invalid(broken.wstring());});
    {Storage reopened(root.wstring());require(reopened.history(remote).size()==1&&reopened.pending(remote).empty(),"Final restart lost ACK/history");}
    std::cout<<"PASS: production SQL adapter, legacy reopen, 32 open/close cycles, history/order/outbox/dedupe, busy rollback, ACK restart and malformed DB rejection\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
