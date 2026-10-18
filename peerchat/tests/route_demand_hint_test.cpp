#include "../storage.cpp"
#include "../delivery_manager.hpp"
#include <iostream>

namespace {
struct CountingProtector final : peerchat::platform::RecordProtector {
    peerchat::platform::WindowsRecordProtector delegate;
    unsigned reads=0;
    peerchat::Blob protect(const peerchat::Blob& value,const std::string& label) override {
        return delegate.protect(value,label);
    }
    peerchat::Blob unprotect(const peerchat::Blob& value,const std::string& label) override {
        ++reads;return delegate.unprotect(value,label);
    }
};
void object(peerchat::platform::SqlDatabase& sql,const std::string& key,const peerchat::Blob& value){
    auto statement=sql.prepare("INSERT INTO objects(k,b) VALUES(?,?)");
    statement->bindText(1,key);statement->bindBlob(2,value);statement->row();
}
}
int main(int argc,char** argv){
    using namespace peerchat;
    const auto base=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path()/L"build"/L"tests"/L"runtime";
    const auto root=base/storage_detail::wide(Storage::newMessageId());
    try{
        auto database=platform::openWindowsSqlDatabase((root/L"route-demand").wstring());
        auto protector=std::make_shared<CountingProtector>();
        Storage store(database,protector);
        const std::string sender(64,'a'),canonical(64,'b'),sibling(64,'c');
        delivery::DeliveryManager delivery(store,sender);
        std::vector<std::string> legacy;
        for(int i=0;i<128;++i){
            StoredMessage message{Storage::newMessageId(),sender,canonical,L"protected route hint fixture",1700000000000ull+static_cast<std::uint64_t>(i),Delivery::Queued};
            storage_detail::require(store.append(canonical,message),"Legacy queue failed");legacy.push_back(message.id);
        }
        protector->reads=0;
        storage_detail::require(delivery.pendingMessages(canonical,1000).size()==legacy.size()&&protector->reads>=legacy.size(),
                                "Baseline pending scan did not read protected messages");
        protector->reads=0;
        storage_detail::require(store.pendingRouteDemand(canonical)&&protector->reads==0,
                                "Route hint decrypted a queued message");
        for(const auto& id:legacy)store.acknowledge(id,Delivery::Delivered);
        protector->reads=0;
        storage_detail::require(!store.pendingRouteDemand(canonical)&&protector->reads==0,
                                "Acknowledged legacy outbox still demanded a route");

        StoredMessage stored{Storage::newMessageId(),sender,canonical,L"stored legacy fixture",1700000001000ull,Delivery::Queued};
        storage_detail::require(store.append(canonical,stored),"Stored fixture queue failed");
        store.acknowledge(stored.id,Delivery::Stored);
        storage_detail::require(store.pendingRouteDemand(canonical),"Unmarked Stored row must conservatively demand a route");
        // Model the durable key written by mailboxStored after exact server acceptance.
        object(*database,"mailbox-uploaded/"+stored.id+"/"+canonical,{1});
        protector->reads=0;
        storage_detail::require(!store.pendingRouteDemand(canonical)&&protector->reads==0,
                                "Exact durable Stored marker failed to suppress route demand");

        StoredMessage targeted{Storage::newMessageId(),sender,canonical,L"two independent devices",1700000002000ull,Delivery::Queued};
        DeliveryTargetManifest manifest;manifest.messageId=targeted.id;manifest.canonicalRecipient=canonical;
        manifest.snapshotId=Storage::newMessageId();manifest.senderAccount=7;manifest.recipientAccount=8;
        manifest.snapshotSequence=1;manifest.issuedUtcMs=1700000000000ull;manifest.freshUntilUtcMs=1700000100000ull;
        manifest.targets={{canonical,"",0,DeliveryTargetStatus::Pending},{sibling,"",0,DeliveryTargetStatus::Pending}};
        storage_detail::require(store.append(canonical,targeted),"Targeted fixture queue failed");
        const auto manifestBytes=storage_detail::encodeTargets(manifest);
        object(*database,"delivery-targets/v1/"+targeted.id,
               protector->protect(manifestBytes,"PeerChat.v1/delivery-targets/v1/"+targeted.id));
        for(const auto& device:{canonical,sibling}){
            auto route=database->prepare("INSERT INTO delivery_target_outbox(id,device) VALUES(?,?)");
            route->bindText(1,targeted.id);route->bindText(2,device);route->row();
        }
        protector->reads=0;
        storage_detail::require(store.pendingRouteDemand(canonical)&&store.pendingRouteDemand(sibling)&&protector->reads==0,
                                "Target hint decrypted a message or missed a sibling");
        auto settled=database->prepare("DELETE FROM delivery_target_outbox WHERE id=? AND device=?");
        settled->bindText(1,targeted.id);settled->bindText(2,canonical);settled->row();
        protector->reads=0;
        storage_detail::require(!store.pendingRouteDemand(canonical)&&store.pendingRouteDemand(sibling)&&protector->reads==0,
                                "Settling one device suppressed its pending sibling");
        storage_detail::require(store.pending(sibling,100).size()==1,
                                "Sibling outbox row was lost");
        const auto hex=[](unsigned n){constexpr char digits[]="0123456789abcdef";return std::string{digits[(n>>4)&15],digits[n&15]};};
        for(unsigned i=0;i<100;++i){
            const auto id=std::string(62,'d')+hex(i);
            store.saveContact({id,L"Counting contact",{}, {},true});
        }
        const auto selected=std::string(62,'d')+hex(90);
        protector->reads=0;
        storage_detail::require(store.contacts().size()==100&&protector->reads==100,
                                "Full contact scan did not read all protected contacts");
        protector->reads=0;
        auto one=store.contact(selected);
        storage_detail::require(one&&one->peerId==selected&&one->verified&&protector->reads==1,
                                "Targeted contact lookup did not preserve verified identity");
        protector->reads=0;
        storage_detail::require(!store.contact(std::string(64,'e'))&&protector->reads==0,
                                "Missing contact lookup read an unrelated record");
        storage_detail::Writer malformed;malformed.string(std::string(64,'e'));malformed.text(L"Wrong identity");
        malformed.bytes({});malformed.number(1);malformed.number(0);
        auto corrupt=database->prepare("UPDATE objects SET b=? WHERE k=?");
        corrupt->bindBlob(1,protector->protect(malformed.data,"PeerChat.v1/contact/"+selected));
        corrupt->bindText(2,"contact/"+selected);corrupt->row();
        bool rejected=false;try{store.contact(selected);}catch(const std::runtime_error&){rejected=true;}
        storage_detail::require(rejected,"Targeted lookup accepted a contact bound to another peer");
        std::cout<<"PASS: route hint avoids protected reads; targeted contact lookup preserves identity binding.\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}
}
