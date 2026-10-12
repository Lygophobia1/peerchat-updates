#include "peerchat_core.h"
#include "../session_transport_manager.hpp"
#include "../group.hpp"
#include "../group_device_binding.hpp"
#include "../group_device_reconciliation.hpp"
#include "../group_device_replica.hpp"
#include "../delivery_manager.hpp"
#include "../archive_application.hpp"
#include "../protocol.hpp"
#include "reliable_endpoint.hpp"
#include "relay_requests.hpp"
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#if defined(__ANDROID__)
#include "android_storage_provider.hpp"
#include "android_hpke_provider.hpp"
#include "../platform/sql_key_registry.hpp"
#include "android_tls_provider.hpp"
#include "../tls.hpp"
#include "../archive_attachment_transfer.hpp"
#include "transfer_storage.hpp"
#include "attachment_file_io.hpp"
#include "../protected_text_source.hpp"
#endif

namespace {
struct Instance {
    // Existing owner, not a mobile session implementation. No profile/device is
    // authenticated and no route can be opened by this foundation ABI.
    peerchat::session::SessionTransportManager sessions{"portability-proof","",{1}};
    std::mutex gate;
#if defined(__ANDROID__)
    std::unique_ptr<peerchat::Storage> storage;
    peerchat::Profile profile;
    std::unique_ptr<peerchat::delivery::DeliveryManager> deliveries;
    std::shared_ptr<peerchat::transfer::posix::Cache> attachmentCache;
    std::unique_ptr<peerchat::transfer::ProtectedStorageJournal> transferJournal;
    std::unique_ptr<peerchat::transfer::TransferManager> transfers;
    std::shared_ptr<peerchat::platform::RecordProtector> attachmentRecords;
    std::unique_ptr<peerchat::transfer::Source> openAttachment(const peerchat::transfer::DurableState& d){
        using namespace peerchat;auto open=[&](const transfer::DurableState& row){return transfer::posix::FileSource::reopen(attachmentCache,row);};
        if(transfer::ProtectedTextSource::token(d.location))return transfer::ProtectedTextSource::reopen(d,attachmentRecords,open);
        return open(d);
    }
    std::unique_ptr<peerchat::archive::Enrollment> archiveApplication;
    std::unique_ptr<peerchat::archive::Execution> archiveExecution;
    std::unique_ptr<peerchat::archive::AttachmentTransfers> archiveTransfers;
    std::set<std::string> cataloguedTransfers;
    void prepareAttachments(std::uint64_t now){
        using namespace peerchat;
        protocol::check(bool(storage));
        if(!transfers){
            auto identity=std::dynamic_pointer_cast<platform::android::Identity>(profile.identity);protocol::check(bool(identity));
            auto path=identity->context->bytes("attachmentDirectory","()[B");attachmentCache=std::make_shared<transfer::posix::Cache>(std::string(path.begin(),path.end()));
            auto cache=attachmentCache;
            attachmentRecords=std::make_shared<platform::android::Records>(identity->context);
            transferJournal=std::make_unique<transfer::ProtectedStorageJournal>(*storage,profile.peerId,
                [this](const transfer::DurableState& d){return openAttachment(d);},
                [cache](const transfer::DurableState& d){return transfer::posix::FileSink::reopen(cache,d);});
            auto manager=std::make_unique<transfer::TransferManager>([]{return std::make_unique<transfer::posix::Sha256>();},transferJournal.get());manager->restore(now);transfers=std::move(manager);
        }
        if(!archiveTransfers&&archiveApplication&&archiveExecution){auto cache=attachmentCache;
            archiveTransfers=std::make_unique<archive::AttachmentTransfers>(*storage,profile,archiveApplication->policy(),*transfers,
                [this](const archive::Attachment& a){transfer::DurableState d;d.metadata={a.id,a.name,a.size,a.digest};d.location=a.location;return openAttachment(d);},
                [cache](const transfer::Metadata& m,const Blob&){cache->reserve(m.size);return std::make_unique<transfer::posix::FileSink>(cache,m);},
                [cache](const transfer::DurableState& d){auto file=transfer::posix::FileSink::reopen(cache,d);return file->sourceToken();},
                [this](const session::ReliableAuthorization& scope,std::uint64_t time){archiveExecution->hold(scope,time,relayRequests->network());});
        }
    }
    std::uint64_t archiveGeneration=0;
    // Provider objects only; existing SessionTransportManager will own route
    // association when networking is ported. No second reconnect/session policy.
    uint64_t nextTls=1;
    std::map<uint64_t,std::unique_ptr<peerchat::TlsSession>> tls;
    peerchat::reliable::AuthenticatedFriendSnapshot friends;
    std::unique_ptr<peerchat::portable::RelayRequests> relayRequests;
    std::set<std::string> liveOutboxPeers;
    std::uint64_t demandCheckAt=0;
    bool outboxDemandLoaded=false;
    // Concrete platform executions only; the one existing sessions/deliveries
    // above remain route/winner/ACK/retry/history authorities.
    std::map<uint64_t,std::unique_ptr<peerchat::portable::ReliableEndpoint>> peers;
    std::unique_ptr<peerchat::platform::android::RecipientProvider> recipientProvider;
    std::shared_ptr<peerchat::platform::android::Context> registryContext;
    std::shared_ptr<peerchat::platform::android::Database> registryDatabase;
    std::unique_ptr<peerchat::platform::android::Records> registryRecords;
    std::unique_ptr<peerchat::platform::SqlKeyRegistryStore> keyRegistryStore;
    std::unique_ptr<peerchat::platform::ProtectedKeyRegistry> keyRegistry;
    std::unique_ptr<peerchat::DeviceKeyManager> keys;
    std::unique_ptr<peerchat::mailbox::KeyCoordinator> keyCoordinator;
    std::shared_ptr<peerchat::platform::HpkeProvider> hpke;
    std::optional<peerchat::mailbox::RecipientBinding> recipientBinding;
#if defined(PEERCHAT_STORAGE_TEST_HOOKS)
    bool testProfile=false;
    std::shared_ptr<peerchat::platform::SqlDatabase> testDatabase;
#endif
#endif
};
struct Registry {
    std::mutex gate;uint64_t next=1;
    std::map<pc_core_handle,std::shared_ptr<Instance>> instances;
};
Registry& registry(){static Registry value;return value;}
std::shared_ptr<Instance> lookup(pc_core_handle handle){auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);auto it=r.instances.find(handle);return it==r.instances.end()?nullptr:it->second;}
template<class F> pc_core_status guarded(F operation) noexcept {
    try{return operation();}catch(const std::bad_alloc&){return PC_CORE_OUT_OF_MEMORY;}catch(...){return PC_CORE_INTERNAL_ERROR;}
}
}
extern "C" {
pc_core_status pc_core_create(pc_core_handle* result){return guarded([&]()->pc_core_status{
    if(!result)return PC_CORE_INVALID_ARGUMENT;
    *result=0;auto instance=std::make_shared<Instance>();auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);
    if(r.next==std::numeric_limits<uint64_t>::max())return PC_CORE_INTERNAL_ERROR;
    auto id=r.next++;r.instances.emplace(id,std::move(instance));*result=id;return PC_CORE_OK;
});}
pc_core_status pc_core_destroy(pc_core_handle handle){return guarded([&]()->pc_core_status{
    auto& r=registry();std::lock_guard<std::mutex> lock(r.gate);return r.instances.erase(handle)?PC_CORE_OK:PC_CORE_INVALID_HANDLE;
});}
}
#if defined(__ANDROID__)
namespace {
std::uint64_t runtimeNow(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
std::string peerArgument(JNIEnv* env,jbyteArray bytes){
    if(!bytes||env->GetArrayLength(bytes)!=64)throw std::invalid_argument("Invalid peer identity");
    auto copy=peerchat::platform::android::Context::output(env,bytes);std::string peer(copy.begin(),copy.end());
    peerchat::protocol::check(peerchat::protocol::id(peer));return peer;
}
void retireEndpoints(Instance& instance,peerchat::portable::ReliableEndpoint& endpoint){
    for(auto retired:endpoint.takeRetired()){
        auto found=instance.peers.find(retired.value);
        if(found!=instance.peers.end())found->second->close(false);
    }
}
void messageRecord(peerchat::protocol::Writer& value,const peerchat::StoredMessage& message){
    value.string(message.sender);value.string(message.recipient);value.number(message.utcMilliseconds);value.number(message.logicalOrder);
    value.number(static_cast<std::uint64_t>(message.sequence));value.number(static_cast<std::uint64_t>(message.delivery));value.text(message.text);
}
std::uint64_t mailboxNow(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
void mailboxProviders(Instance& instance){
    using namespace peerchat;
    mailbox::require(instance.storage&&instance.deliveries&&instance.friends.localAccount()>0);
    if(instance.keys)return;
    auto identity=std::dynamic_pointer_cast<platform::android::Identity>(instance.profile.identity);
    mailbox::require(identity!=nullptr);
    auto context=identity->context;
    platform::android::Context::Call call(context->vm);
    auto facade=call.env->CallObjectMethod(context->object,context->method(call.env,"keyRegistryProvider","()Ljava/lang/Object;"));
    platform::android::Context::checked(call.env);mailbox::require(facade!=nullptr);
    auto registryContext=std::make_shared<platform::android::Context>(call.env,facade);
    auto database=std::make_shared<platform::android::Database>(registryContext);
    auto records=std::make_unique<platform::android::Records>(context);
    auto store=std::make_unique<platform::SqlKeyRegistryStore>(database);
    auto registry=std::make_unique<platform::ProtectedKeyRegistry>(*store,*records,instance.profile.peerId);
    auto provider=std::make_unique<platform::android::RecipientProvider>(context);
    auto manager=std::make_unique<DeviceKeyManager>(*identity,*provider,*registry);
    auto coordinator=std::make_unique<mailbox::KeyCoordinator>(*manager,*identity,verifyDevicePayload,Storage::publicKeyId);
    auto hpke=std::make_shared<platform::android::AndroidHpkeProvider>(context);
    instance.recipientProvider=std::move(provider);instance.registryContext=std::move(registryContext);
    instance.registryDatabase=std::move(database);instance.registryRecords=std::move(records);
    instance.keyRegistryStore=std::move(store);instance.keyRegistry=std::move(registry);
    instance.keys=std::move(manager);instance.keyCoordinator=std::move(coordinator);instance.hpke=std::move(hpke);
}
peerchat::Contact mailboxContact(Instance& instance,const std::string& device,std::uint64_t account){
    peerchat::mailbox::require(account>0&&instance.friends.accountFor(device)==account);
    auto contacts=instance.storage->contacts();
    auto found=std::find_if(contacts.begin(),contacts.end(),[&](const auto& contact){return contact.peerId==device;});
    peerchat::mailbox::require(found!=contacts.end()&&found->verified&&peerchat::Storage::publicKeyId(found->publicKey)==device);
    return *found;
}
peerchat::Blob mailboxArgument(JNIEnv* env,jbyteArray input,std::size_t maximum){
    peerchat::mailbox::require(input&&static_cast<std::size_t>(env->GetArrayLength(input))<=maximum);
    return peerchat::platform::android::Context::output(env,input);
}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_MailboxBridge_registration(JNIEnv* env,jobject,jlong handle,jboolean allowed){
    try{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);mailboxProviders(*instance);
        auto context=instance->recipientProvider->context;
        auto binding=instance->keyCoordinator->prepare(instance->friends.localAccount(),allowed==JNI_TRUE,[&](std::uint32_t generation){
            peerchat::platform::android::Context::Call call(context->vm);
            auto result=static_cast<jbyteArray>(call.env->CallObjectMethod(context->object,context->method(call.env,"recipientAlias","(I)[B"),static_cast<jint>(generation)));
            auto bytes=peerchat::platform::android::Context::output(call.env,result);return std::string(bytes.begin(),bytes.end());
        },peerchat::Storage::newMessageId,mailboxNow());
        return peerchat::platform::android::Context::input(env,peerchat::mailbox::serializeBinding(binding));
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_MailboxBridge_registered(JNIEnv* env,jobject,jlong handle,jbyteArray input){
    return guarded([&]()->pc_core_status{
        auto binding=peerchat::mailbox::parseBinding(mailboxArgument(env,input,4096));
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);mailboxProviders(*instance);
        peerchat::mailbox::require(binding.accountId==instance->friends.localAccount());
        instance->keyCoordinator->accepted(binding);instance->recipientBinding=std::move(binding);return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_MailboxBridge_pending(JNIEnv* env,jobject,jlong handle){
    try{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);
        peerchat::mailbox::require(instance->storage&&instance->friends.localAccount()>0);
        std::vector<peerchat::StoredMessage> pending;std::set<std::string> seen;
        for(auto& message:instance->storage->pendingMailboxUploads())if(instance->friends.accountFor(message.recipient)>0){seen.insert(message.id);pending.push_back(std::move(message));}
        for(const auto& contact:instance->storage->contacts())if(contact.verified&&instance->friends.accountFor(contact.peerId)>0){
            auto route=instance->sessions.snapshot(contact.peerId);if(route&&route->connected)continue;
            auto messages=instance->deliveries->pendingMessages(contact.peerId,1000);
            for(auto& message:messages){if(pending.size()==64)break;if(message.delivery!=peerchat::Delivery::Stored&&seen.insert(message.id).second)pending.push_back(std::move(message));}
            if(pending.size()==64)break;
        }
        peerchat::protocol::Writer record(peerchat::protocol::Accepted);record.number(pending.size());
        for(const auto& message:pending){record.string(message.id);record.string(message.recipient);record.number(instance->friends.accountFor(message.recipient));}
        return peerchat::platform::android::Context::input(env,record.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_MailboxBridge_prepare(JNIEnv* env,jobject,jlong handle,jbyteArray idInput,jbyteArray bindingInput,jboolean allowed){
    try{
        auto idBytes=mailboxArgument(env,idInput,32);std::string id(idBytes.begin(),idBytes.end());
        peerchat::mailbox::require(peerchat::mailbox::id(id,32));
        auto binding=peerchat::mailbox::parseBinding(mailboxArgument(env,bindingInput,4096));
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);mailboxProviders(*instance);
        auto contact=mailboxContact(*instance,binding.device,binding.accountId);
        auto message=instance->storage->message(id);peerchat::mailbox::require(message.has_value());auto canonical=mailboxContact(*instance,message->recipient,binding.accountId);
        auto route=instance->sessions.snapshot(binding.device);
        auto wire=instance->deliveries->prepareMailbox(id,instance->friends.localAccount(),binding.accountId,binding,contact,
            *instance->profile.identity,*instance->hpke,mailboxNow(),allowed==JNI_TRUE,route&&route->connected,&canonical);
        return peerchat::platform::android::Context::input(env,wire);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_MailboxBridge_prepareDevices(JNIEnv* env,jobject,jlong handle,jbyteArray idInput,jbyteArray batchInput,jboolean allowed){
    try{
        auto bytes=mailboxArgument(env,idInput,32);std::string id(bytes.begin(),bytes.end());peerchat::mailbox::require(allowed==JNI_TRUE&&peerchat::mailbox::id(id,32));
        auto bindings=peerchat::mailbox::parseBindingBatch(mailboxArgument(env,batchInput,peerchat::mailbox::MaxWire));
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);mailboxProviders(*instance);
        auto message=instance->storage->message(id);peerchat::mailbox::require(message.has_value());auto account=instance->friends.accountFor(message->recipient);
        auto canonical=mailboxContact(*instance,message->recipient,account);auto primaryRoute=instance->sessions.snapshot(canonical.peerId);
        std::vector<peerchat::mailbox::RecipientBinding> prepared;
        if(!primaryRoute||!primaryRoute->connected)for(const auto& binding:bindings){
            peerchat::mailbox::require(binding.accountId==account);auto contacts=instance->storage->contacts();
            auto known=std::find_if(contacts.begin(),contacts.end(),[&](const auto& c){return c.peerId==binding.device&&c.verified;});
            if(known==contacts.end()||instance->friends.accountFor(binding.device)!=account)continue;
            auto contact=mailboxContact(*instance,binding.device,account);
            auto wire=instance->deliveries->prepareMailbox(id,instance->friends.localAccount(),account,binding,contact,*instance->profile.identity,*instance->hpke,mailboxNow(),true,false,&canonical);
            if(!wire.empty()&&!instance->storage->mailboxUploadAccepted(id,binding.device))prepared.push_back(binding);
        }
        peerchat::protocol::Writer record(peerchat::protocol::Accepted);record.number(prepared.size());
        for(const auto& binding:prepared)record.bytes(peerchat::mailbox::serializeBinding(binding));
        return peerchat::platform::android::Context::input(env,record.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_MailboxBridge_stored(JNIEnv* env,jobject,jlong handle,jbyteArray input){
    return guarded([&]()->pc_core_status{
        auto wire=mailboxArgument(env,input,peerchat::mailbox::MaxWire);auto header=peerchat::mailbox::parse(wire).header;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);peerchat::mailbox::require(instance->deliveries&&header.senderAccount==instance->friends.localAccount());
        mailboxContact(*instance,header.recipientDevice,header.recipientAccount);
        instance->deliveries->mailboxStored(header.messageId,header.recipientDevice,wire);return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_MailboxBridge_receive(JNIEnv* env,jobject,jlong handle,jbyteArray input){
    try{
        auto wire=mailboxArgument(env,input,peerchat::mailbox::MaxWire);auto header=peerchat::mailbox::parse(wire).header;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);mailboxProviders(*instance);
        auto contact=mailboxContact(*instance,header.senderDevice,header.senderAccount);
        peerchat::mailbox::require(header.recipientAccount==instance->friends.localAccount());
        auto entries=instance->keys->snapshot();auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& value){return value.id==header.keyId;});
        peerchat::mailbox::require(entry!=entries.end()&&!entry->signedBinding.empty());
        auto binding=peerchat::mailbox::parseBinding(entry->signedBinding);
        auto result=instance->deliveries->acceptMailbox(wire,header.senderAccount,contact,binding,*instance->keys,*instance->profile.identity,*instance->hpke,mailboxNow());
        peerchat::protocol::Writer record(peerchat::protocol::Ack);record.number(result.inserted?1:0);
        record.string(std::string(result.receipt.begin(),result.receipt.end()));record.string(header.senderDevice);record.string(header.messageId);
        return peerchat::platform::android::Context::input(env,record.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_MailboxBridge_receipt(JNIEnv* env,jobject,jlong handle,jbyteArray input){
    return guarded([&]()->pc_core_status{
        auto wire=mailboxArgument(env,input,2048);auto receipt=peerchat::mailbox::parseReceipt(wire);
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);peerchat::mailbox::require(instance->deliveries!=nullptr);
        auto contact=mailboxContact(*instance,receipt.recipientDevice,receipt.recipientAccount);
        instance->deliveries->acknowledgeMailbox(wire,receipt.recipientAccount,contact);return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_friends(JNIEnv* env,jobject,jlong handle,jlong account,jlong generation,jbyteArray snapshot){
    return guarded([&]()->pc_core_status{
        if(!snapshot||account<0||generation<=0||env->GetArrayLength(snapshot)>262144)return PC_CORE_INVALID_ARGUMENT;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return PC_CORE_INVALID_ARGUMENT;
        if(static_cast<std::uint64_t>(generation)<instance->friends.generation())return PC_CORE_INVALID_ARGUMENT;
        auto bytes=peerchat::platform::android::Context::output(env,snapshot);peerchat::protocol::Reader reader(bytes);
        peerchat::protocol::check(reader.type()==peerchat::protocol::Hello);auto count=reader.number();peerchat::protocol::check(count<=1024);
        std::vector<peerchat::reliable::FriendIdentity> values;
        for(std::uint64_t i=0;i<count;++i){
            peerchat::reliable::FriendIdentity value;value.accountId=reader.number();value.expectedPeerId=reader.string();
            peerchat::protocol::check(value.accountId&&peerchat::protocol::id(value.expectedPeerId));
            auto known=reader.number();peerchat::protocol::check(known<=32);
            for(std::uint64_t j=0;j<known;++j){auto peer=reader.string();peerchat::protocol::check(peerchat::protocol::id(peer));value.knownPeerIds.push_back(std::move(peer));}
            values.push_back(std::move(value));
        }
        reader.end();peerchat::protocol::check(account||values.empty());
        if(instance->archiveApplication)instance->archiveApplication->invalidate();
        auto validatedFriends=instance->friends;
        validatedFriends.replace(static_cast<std::uint64_t>(account),static_cast<std::uint64_t>(generation),values);
        if(account){
            instance->storage->rememberAuthenticatedDeviceAccount(instance->profile.peerId,static_cast<std::uint64_t>(account));
            for(const auto& value:values){
                instance->storage->rememberAuthenticatedDeviceAccount(value.expectedPeerId,value.accountId);
                for(const auto& device:value.knownPeerIds)instance->storage->rememberAuthenticatedDeviceAccount(device,value.accountId);
            }
        }
        instance->friends=std::move(validatedFriends);
        instance->relayRequests->revoke();
        for(auto& peer:instance->peers)if(!instance->friends.contains(peer.second->expected())){
            peer.second->close(false);instance->sessions.removePeer(peer.second->expected());
        }
        return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ArchiveBridge_request(JNIEnv* env,jobject,jlong handle,jint operation,jbyteArray input){
    try{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        auto bytes=mailboxArgument(env,input,peerchat::archive::MaxApplicationBytes);
        std::lock_guard<std::mutex> lock(instance->gate);
        peerchat::archive::require(instance->storage&&instance->friends.localAccount()>0&&instance->friends.generation()>0);
        if(!instance->archiveApplication)instance->archiveApplication=std::make_unique<peerchat::archive::Enrollment>(
            *instance->storage,instance->profile,instance->friends.localAccount(),runtimeNow);
        if(instance->archiveGeneration!=instance->friends.generation()){
            instance->archiveApplication->authenticated(instance->friends.generation());instance->archiveGeneration=instance->friends.generation();
        }
        if(!instance->archiveExecution)instance->archiveExecution=std::make_unique<peerchat::archive::Execution>(*instance->storage,instance->profile,
            instance->archiveApplication->policy(),*instance->deliveries,instance->sessions);
        if(operation>=16&&operation<=18){
            using namespace peerchat;protocol::Reader r(bytes);archive::require(r.type()==archive::LocalFormat&&r.string()=="PeerChat/ArchiveApplication/v1"&&r.number()==1&&r.number()==static_cast<unsigned>(operation));
            auto account=r.number(),generation=r.number();archive::require(instance->archiveApplication->matches(account,generation));auto result=archive::applicationHeader(operation);
            if(operation==16){auto after=r.number();r.end();archive::require(after<=INT64_MAX);auto page=instance->storage->attachmentMetadataPage(after,instance->storage->attachmentMetadataWatermark());
                result.number(page.next);result.number(page.complete);result.number(page.records.size());for(const auto& record:page.records){auto a=archive::decodeLocalAttachment(record.record);result.string(a.id);result.string(a.name);result.number(a.size);result.number(!a.location.empty());result.number(!a.proof.empty());}}
            if(operation==17){auto source=r.string(),id=r.string();r.end();archive::require(instance->archiveExecution->scope()&&instance->archiveExecution->scope()->archive.source==source);instance->prepareAttachments(runtimeNow());result.string(instance->archiveTransfers->download(*instance->archiveExecution->scope(),id,Blob{'c','a','c','h','e'}));}
            if(operation==18){auto id=r.string();r.end();instance->prepareAttachments(runtimeNow());instance->archiveTransfers->cancel(id);}
            return platform::android::Context::input(env,result.data);
        }
        auto output=operation>=12?peerchat::archive::executeSyncOperation(*instance->archiveApplication,*instance->archiveExecution,static_cast<unsigned>(operation),bytes,runtimeNow(),instance->relayRequests->network()):
            peerchat::archive::executeEnrollmentOperation(*instance->archiveApplication,static_cast<unsigned>(operation),bytes,mailboxNow());
        return peerchat::platform::android::Context::input(env,output);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_presence(JNIEnv* env,jobject,jlong handle,jbyteArray pin,jboolean online,jlong generation,jlong network){
    return guarded([&]()->pc_core_status{
        if(generation<=0||network<=0)return PC_CORE_INVALID_ARGUMENT;
        auto expected=peerArgument(env,pin);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->relayRequests)return PC_CORE_INVALID_ARGUMENT;
        return instance->relayRequests->presence(expected,online==JNI_TRUE,generation,network,runtimeNow())?PC_CORE_OK:PC_CORE_INVALID_ARGUMENT;
    });
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_network(JNIEnv*,jobject,jlong handle,jlong generation,jlong network){
    return guarded([&]()->pc_core_status{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(generation<=0||network<=0||!instance->relayRequests)return PC_CORE_INVALID_ARGUMENT;
        return instance->relayRequests->authenticatedNetwork(generation,network)?PC_CORE_OK:PC_CORE_INVALID_ARGUMENT;
    });
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_demand(JNIEnv* env,jobject,jlong handle,jbyteArray pin){
    return guarded([&]()->pc_core_status{
        auto expected=peerArgument(env,pin);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->friends.contains(expected))return PC_CORE_INVALID_ARGUMENT;
        const auto now=runtimeNow();instance->sessions.demandLiveRoute(expected,now+45000,now);
        return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_requests(JNIEnv* env,jobject,jlong handle){
    try{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->relayRequests)return nullptr;
        // Poll execution commands, not a Kotlin retry/session policy.
        auto now=runtimeNow();
        if(instance->archiveExecution){try{instance->prepareAttachments(now);instance->archiveTransfers->maintain(now);}catch(const std::exception&){}instance->archiveExecution->tick(now,instance->relayRequests->network());}
        if(!instance->outboxDemandLoaded){
            for(const auto& contact:instance->storage->contacts())if(!instance->storage->pending(contact.peerId,1).empty())instance->liveOutboxPeers.insert(contact.peerId);
            instance->outboxDemandLoaded=true;
        }
        if(now>=instance->demandCheckAt){
            instance->demandCheckAt=now+1000;
            for(auto it=instance->liveOutboxPeers.begin();it!=instance->liveOutboxPeers.end();){
                auto pending=instance->deliveries->pendingMessages(*it,1000);
                if(std::none_of(pending.begin(),pending.end(),[](const auto& message){return message.delivery!=peerchat::Delivery::Stored;})){it=instance->liveOutboxPeers.erase(it);continue;}
                auto contacts=instance->storage->contacts();
                if(std::none_of(contacts.begin(),contacts.end(),[&](const auto& contact){return contact.peerId==*it&&contact.verified;})){++it;continue;}
                instance->sessions.demandLiveRoute(*it,now+2000,now);++it;
            }
            instance->sessions.retireIdleRoutes(now);
        }
        for(auto& peer:instance->peers){
            if(peer.second->closed()||!instance->sessions.attachablePeer({1},{peer.first})||
               (!peer.second->ready()&&peer.second->authenticationExpired(now))){
                peer.second->close(true);
                if(auto scope=peer.second->archiveScope()){
                    if(instance->archiveExecution)instance->archiveExecution->disconnected(*scope);
                    instance->sessions.scheduleRelayRetry(scope->continuityKey(),now,static_cast<std::uint32_t>(now));
                }else if(instance->friends.contains(peer.second->expected())&&!instance->sessions.hasActiveWinner(peer.second->expected()))
                    instance->sessions.scheduleRelayRetry(peer.second->expected(),now,static_cast<std::uint32_t>(now));
            }
            else if(auto scope=peer.second->archiveScope())if(peer.second->ready()&&instance->archiveExecution){
                try{auto request=instance->archiveTransfers?instance->archiveTransfers->request(*scope,peer.first):peerchat::Blob{};if(request.empty())request=instance->archiveExecution->request(*scope,now);if(!request.empty())peer.second->sendArchiveControl(request);}
                catch(...){instance->archiveExecution->disconnected(*scope);peer.second->close(false);}
            }
        }
        for(auto it=instance->peers.begin();it!=instance->peers.end();){
            if(it->second->closed())it=instance->peers.erase(it);else ++it;
        }
        if(instance->transfers)try{
            using namespace peerchat;
            for(const auto& row:instance->transfers->snapshots())if((!instance->archiveTransfers||!instance->archiveTransfers->wasArchiveTransfer(row.metadata.id))&&instance->friends.contains(row.peer)&&
                (row.state==transfer::State::Preparing||row.state==transfer::State::Offered||row.state==transfer::State::Transferring||row.state==transfer::State::Recovering||instance->transfers->hasPendingControl(row.metadata.id)))
                instance->sessions.demandLiveRoute(row.peer,now+2000,now);
            auto transferScope=[&](const std::string& id){return instance->archiveTransfers?instance->archiveTransfers->scopeForTransfer(id):std::optional<session::ReliableAuthorization>{};};
            auto routeFor=[&](const transfer::Snapshot& row){auto scope=transferScope(row.metadata.id);
                if(!scope&&!instance->friends.contains(row.peer))return decltype(instance->sessions.snapshot(row.peer)){};
                return instance->sessions.snapshot(scope?scope->continuityKey():row.peer);};
            instance->transfers->tickScoped(now,[&](const transfer::Snapshot& row)->std::uint64_t{auto route=routeFor(row);if(!route||!route->connected)return 0;auto peer=instance->peers.find(route->activeTransport.value);return peer!=instance->peers.end()&&peer->second->ready()?route->activeTransport.value:0;},
                [&](const transfer::Snapshot& row,const Blob& bytes){auto route=routeFor(row);if(!route)return false;auto peer=instance->peers.find(route->activeTransport.value);if(peer==instance->peers.end()||!peer->second->ready())return false;
                    auto scope=transferScope(row.metadata.id);
                    try{if(scope){peer->second->sendArchiveControl(instance->archiveTransfers->outgoing(*scope,bytes));return true;}return peer->second->sendTransfer(bytes);}catch(...){peer->second->close(true);return false;}});
            archive::AttachmentCatalog catalog(*instance->storage);
            for(auto row:instance->transfers->completedRecords())if(!instance->cataloguedTransfers.count(row.metadata.id)&&(!instance->archiveTransfers||!instance->archiveTransfers->wasArchiveTransfer(row.metadata.id))){
                if(!row.sending)row.location=transfer::posix::FileSink::reopen(instance->attachmentCache,row)->sourceToken();
                catalog.completed(row,instance->profile.peerId,[&](const std::string& device){return instance->storage->historicalDeviceAccount(device);});instance->cataloguedTransfers.insert(row.metadata.id);
            }
        }catch(const peerchat::transfer::PersistenceError&){}
        auto used=instance->peers.size()+instance->tls.size();
        auto requests=instance->relayRequests->take(now,static_cast<std::uint32_t>(now),used<32?32-used:0);
        peerchat::protocol::Writer value(peerchat::protocol::Hello);value.number(requests.size());
        for(const auto& request:requests){value.number(request.id.value);value.string(request.peer);value.number(request.route.networkRevision);
            value.bytes(request.authorization.permitsArchiveControl()?peerchat::archive::sessionBinding(request.authorization).data:peerchat::Blob{});}
        return peerchat::platform::android::Context::input(env,value.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jlong JNICALL Java_com_peerchat_android_ReliableBridge_open(JNIEnv* env,jobject,jlong handle,jlong attempt,jboolean incoming,jbyteArray pin){
    try{
        auto expected=peerArgument(env,pin);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return 0;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage||instance->tls.size()+instance->peers.size()>=32||attempt<=0)return 0;
        auto request=instance->relayRequests->attach({static_cast<std::uint64_t>(attempt)},expected);if(!request)return 0;
        auto id=request->id.value;
        try{
        auto identity=std::dynamic_pointer_cast<peerchat::platform::android::Identity>(instance->profile.identity);
        peerchat::protocol::check(identity!=nullptr);
        auto factory=std::make_shared<peerchat::platform::android::TlsFactory>(identity->context);
        peerchat::TlsCredentials credentials(instance->profile,std::move(factory));
        auto tls=std::make_unique<peerchat::TlsSession>(credentials,incoming==JNI_TRUE,expected);
        auto route=request->route;route.incoming=incoming==JNI_TRUE;
        std::optional<peerchat::portable::ReliableEndpoint::ArchiveAdmission> archive;
        if(request->authorization.permitsArchiveControl()){
            peerchat::protocol::check(instance->archiveApplication&&instance->archiveExecution);
            auto scope=request->authorization;auto owner=instance.get();
            archive=peerchat::portable::ReliableEndpoint::ArchiveAdmission{&instance->archiveApplication->policy(),scope,
                [owner,scope](const peerchat::Blob& bytes){peerchat::protocol::Reader r(bytes);if(r.type()==peerchat::protocol::ArchiveControl&&r.string()==peerchat::archive::AttachmentDomain){peerchat::protocol::check(r.number()==1);if(r.number()>=3){owner->prepareAttachments(runtimeNow());owner->archiveTransfers->control(scope,bytes,runtimeNow());return peerchat::Blob{};}}return owner->archiveExecution->control(scope,bytes,mailboxNow(),runtimeNow());},true};
        }
        instance->prepareAttachments(runtimeNow());auto owner=instance.get();
        auto endpoint=std::make_unique<peerchat::portable::ReliableEndpoint>(instance->profile,*instance->storage,instance->sessions,*instance->deliveries,
            instance->friends,peerchat::session::SessionEpoch{1},peerchat::session::TransportInstanceId{id},std::move(tls),expected,route,runtimeNow(),std::move(archive),
            [owner](const std::string& peer,const peerchat::Blob& frame,std::uint64_t now){peerchat::protocol::check(owner->friends.contains(peer)&&(!owner->archiveTransfers||!owner->archiveTransfers->wasArchiveTransfer(peerchat::archive::transferPacketId(frame))));owner->transfers->receive(peer,frame,now);});
        instance->peers.emplace(id,std::move(endpoint));return static_cast<jlong>(id);
        }catch(...){instance->relayRequests->failed(*request,runtimeNow(),static_cast<std::uint32_t>(runtimeNow()),true);throw;}
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return 0;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_exchange(JNIEnv* env,jobject,jlong handle,jlong token,jint operation,jbyteArray input){
    try{
        if(!input||env->GetArrayLength(input)>65536)return nullptr;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);auto found=instance->peers.find(static_cast<std::uint64_t>(token));if(found==instance->peers.end())return nullptr;
        auto& endpoint=*found->second;peerchat::Blob output;
        try{
            if(operation==0){endpoint.receive(peerchat::platform::android::Context::output(env,input),runtimeNow());retireEndpoints(*instance,endpoint);}
            else{
                if(env->GetArrayLength(input)!=0)return nullptr;
                if(operation==1)output=endpoint.takeOutput();
                else if(operation==2)output={static_cast<unsigned char>(endpoint.ready()),static_cast<unsigned char>(endpoint.closed())};
                else if(operation==3){
                    output={static_cast<unsigned char>(endpoint.initialPin(instance->friends.generation(),runtimeNow()))};retireEndpoints(*instance,endpoint);
                }else if(operation==4){
                    if(auto event=endpoint.poll()){
                        peerchat::protocol::Writer value(peerchat::protocol::Ack);value.number(static_cast<std::uint64_t>(event->kind));value.string(event->peer);value.string(event->id);value.number(event->message?1:0);
                        if(event->message)messageRecord(value,*event->message);
                        output=std::move(value.data);
                    }
                }else return nullptr;
            }
        }catch(...){endpoint.close(false);throw;}
        return peerchat::platform::android::Context::input(env,output);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_close(JNIEnv*,jobject,jlong handle,jlong token,jboolean recoverable){
    return guarded([&]()->pc_core_status{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);auto found=instance->peers.find(static_cast<std::uint64_t>(token));
        auto now=runtimeNow();
        if(found==instance->peers.end())return instance->relayRequests&&instance->relayRequests->close({static_cast<std::uint64_t>(token)},now,static_cast<std::uint32_t>(now),recoverable==JNI_TRUE)?PC_CORE_OK:PC_CORE_INVALID_HANDLE;
        auto expected=found->second->expected();std::optional<peerchat::session::ReliableAuthorization> scope;
        if(auto archive=found->second->archiveScope())scope=*archive;
        found->second->close(recoverable==JNI_TRUE);instance->peers.erase(found);
        if(scope){if(instance->archiveExecution)instance->archiveExecution->disconnected(*scope);
            if(recoverable==JNI_TRUE)instance->sessions.scheduleRelayRetry(scope->continuityKey(),now,static_cast<std::uint32_t>(now));}
        else if(recoverable==JNI_TRUE&&instance->friends.contains(expected)&&!instance->sessions.hasActiveWinner(expected))
            instance->sessions.scheduleRelayRetry(expected,now,static_cast<std::uint32_t>(now));
        return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_send(JNIEnv* env,jobject,jlong handle,jbyteArray destination,jbyteArray text){
    try{
        if(!text||env->GetArrayLength(text)>16384)return nullptr;
        auto peer=peerArgument(env,destination);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage||!instance->friends.contains(peer))return nullptr;
        auto bytes=peerchat::platform::android::Context::output(env,text);
        auto content=peerchat::unicode::decode(std::string(bytes.begin(),bytes.end()));
        auto contacts=instance->storage->contacts();auto known=std::find_if(contacts.begin(),contacts.end(),[&](const auto& value){return value.peerId==peer;});
        auto id=peerchat::Storage::newMessageId();
        peerchat::reliable::queueText(*instance->storage,instance->profile,known==contacts.end()?nullptr:&*known,peer,id,content,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
        // A committed message remains a successful durable send even if this
        // particular route fails. Retire it; the same message ID stays pending.
        for(auto& endpoint:instance->peers)if(endpoint.second->expected()==peer){
            try{endpoint.second->pump();}catch(...){endpoint.second->close(false);}
        }
        return peerchat::platform::android::Context::input(env,peerchat::Blob(id.begin(),id.end()));
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
// Bounded local adapter commands; wire framing, durability and ACKs stay in
// TransferManager. Only its validated private-cache source tokens can be viewed.
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_TransferBridge_request(JNIEnv* env,jobject,jlong handle,jint operation,jbyteArray input){
    try{
        using namespace peerchat;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);instance->prepareAttachments(runtimeNow());
        protocol::check(input&&env->GetArrayLength(input)<=4096);
        auto bytes=platform::android::Context::output(env,input);protocol::Reader r(bytes);protocol::check(r.type()==protocol::FileTransfer);
        protocol::Writer w(protocol::FileTransfer);auto now=runtimeNow();
        auto directory=[&]{auto identity=std::dynamic_pointer_cast<platform::android::Identity>(instance->profile.identity);protocol::check(bool(identity));auto bytes=identity->context->bytes("attachmentDirectory","()[B");return std::string(bytes.begin(),bytes.end());};
        if(operation==0){r.end();w.string(directory());}
        else if(operation==1){
            auto peer=r.string(),path=r.string(),name=r.string();r.end();protocol::check(instance->friends.contains(peer));
            auto id=Storage::newMessageId();auto source=std::make_unique<transfer::posix::FileSource>(instance->attachmentCache,path,name);
            instance->transfers->offer(peer,id,std::move(source),now);instance->sessions.demandLiveRoute(peer,now+2000,now);w.string(id);
        }else if(operation==2||operation==3){
            auto id=r.string();r.end();protocol::check(protocol::id(id,32));
            auto rows=instance->transfers->snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& item){return item.metadata.id==id;});
            protocol::check(row!=rows.end()&&instance->friends.contains(row->peer)&&(!instance->archiveTransfers||!instance->archiveTransfers->wasArchiveTransfer(id)));
            if(operation==2){instance->attachmentCache->reserve(row->metadata.size);instance->transfers->accept(id,std::make_unique<transfer::posix::FileSink>(instance->attachmentCache,row->metadata),now);}
            else instance->transfers->cancel(id);
        }else if(operation==4){
            r.end();auto rows=instance->transfers->snapshots();rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& row){return instance->archiveTransfers&&instance->archiveTransfers->wasArchiveTransfer(row.metadata.id);}),rows.end());
            w.number(rows.size());
            for(const auto& row:rows){w.string(row.metadata.id);w.string(row.peer);w.string(row.metadata.name);w.number(row.metadata.size);w.number(static_cast<unsigned>(row.state));w.number(row.sending);w.number(row.done);w.number(row.created);w.string(row.error);
                std::string path;
                if(row.state==transfer::State::Complete)for(auto d:instance->transfers->completedRecords())if(d.metadata.id==row.metadata.id){
                    try{if(!d.sending)d.location=transfer::posix::FileSink::reopen(instance->attachmentCache,d)->sourceToken();
                        auto source=transfer::posix::FileSource::reopen(instance->attachmentCache,d);path=directory()+"/"+source->cacheName();
                    }catch(const std::exception&){/* Missing copy is never presented as viewable. */}
                }
                w.string(path);
            }
        }else return nullptr;
        return platform::android::Context::input(env,w.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_queueLocal(JNIEnv* env,jobject,jlong handle,jbyteArray destination,jbyteArray text){
    try{
        if(!text||env->GetArrayLength(text)>16384)return nullptr;
        auto peer=peerArgument(env,destination);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return nullptr;
        auto contacts=instance->storage->contacts();
        const auto requested=peer;
        peer=peerchat::reliable::queueRecipient(requested,contacts,instance->friends);
        auto known=std::find_if(contacts.begin(),contacts.end(),[&](const auto& value){return value.peerId==peer;});
        // Account authorization permits only protected local queueing. It does
        // not pin a key, authorize encryption or establish an endpoint.
        auto bytes=peerchat::platform::android::Context::output(env,text);
        auto content=peerchat::unicode::decode(std::string(bytes.begin(),bytes.end()));auto id=peerchat::Storage::newMessageId();
        peerchat::reliable::queueText(*instance->storage,instance->profile,known==contacts.end()?nullptr:&*known,peer,id,content,mailboxNow(),&instance->friends);
        instance->liveOutboxPeers.insert(peer);
        const auto now=runtimeNow();instance->sessions.demandLiveRoute(peer,now+45000,now);
        if(requested!=peer)instance->sessions.demandLiveRoute(requested,now+45000,now);
        return peerchat::platform::android::Context::input(env,peerchat::Blob(id.begin(),id.end()));
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_TransferBridge_offerText(JNIEnv* env,jobject,jlong handle,jbyteArray destination,jbyteArray input){
    try{
        using namespace peerchat;protocol::check(input&&env->GetArrayLength(input)>0&&static_cast<size_t>(env->GetArrayLength(input))<=transfer::ProtectedTextSource::MaxBytes);
        auto peer=peerArgument(env,destination);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);protocol::check(instance->friends.contains(peer));instance->prepareAttachments(runtimeNow());
        auto plain=platform::android::Context::output(env,input);auto id=Storage::newMessageId();
        auto sealed=transfer::ProtectedTextSource::seal(plain,id,*instance->attachmentRecords);
        auto record=transfer::posix::FileSource::create(instance->attachmentCache,id+"-"+transfer::ProtectedTextSource::Name,transfer::ProtectedTextSource::Name,sealed);
        auto source=std::make_unique<transfer::ProtectedTextSource>(std::move(record),id,instance->attachmentRecords);
        instance->transfers->offer(peer,id,std::move(source),runtimeNow());instance->sessions.demandLiveRoute(peer,runtimeNow()+2000,runtimeNow());
        return platform::android::Context::input(env,id);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_TransferBridge_readText(JNIEnv* env,jobject,jlong handle,jbyteArray input){
    try{
        using namespace peerchat;protocol::check(input&&env->GetArrayLength(input)==32);
        auto bytes=platform::android::Context::output(env,input);std::string id(bytes.begin(),bytes.end());protocol::check(protocol::id(id,32));
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;std::lock_guard<std::mutex> lock(instance->gate);instance->prepareAttachments(runtimeNow());
        std::optional<transfer::DurableState> found;
        for(auto row:instance->transfers->retainedRecords())if(row.metadata.id==id&&(row.state==transfer::State::Complete||(row.sending&&transfer::ProtectedTextSource::token(row.location)))){if(!row.sending)row.location=transfer::posix::FileSink::reopen(instance->attachmentCache,row)->sourceToken();found=std::move(row);}
        protocol::check(found&&found->metadata.size<=transfer::ProtectedTextSource::MaxBytes);
        auto source=instance->openAttachment(*found);Blob plain;
        for(std::uint64_t offset=0;offset<source->size();offset+=transfer::ChunkBytes){auto part=source->read(offset,std::min<std::uint64_t>(transfer::ChunkBytes,source->size()-offset));plain.insert(plain.end(),part.begin(),part.end());}
        unicode::decode(std::string(plain.begin(),plain.end()));return platform::android::Context::input(env,plain);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_history(JNIEnv* env,jobject,jlong handle,jbyteArray destination,jlong before){
    try{
        if(before<=0)return nullptr;
        auto peer=peerArgument(env,destination);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return nullptr;
        auto history=instance->storage->history(peer,8,static_cast<std::int64_t>(before));
        peerchat::protocol::Writer record(peerchat::protocol::Text);record.number(history.size());
        for(const auto& message:history){record.string(message.id);messageRecord(record,message);}
        return peerchat::platform::android::Context::input(env,record.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ReliableBridge_state(JNIEnv* env,jobject,jlong handle,jbyteArray destination){
    try{
        auto peer=peerArgument(env,destination);auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return nullptr;
        auto snapshot=instance->sessions.snapshot(peer);
        const bool demanded=instance->sessions.liveRouteDemanded(peer,runtimeNow());
        peerchat::protocol::Writer value(peerchat::protocol::Accepted);
        value.number(snapshot&&snapshot->connected?1:0);value.number(snapshot&&snapshot->pending?1:0);
        value.number(demanded&&snapshot&&snapshot->retryScheduled?1:0);value.number(demanded&&snapshot?snapshot->retryDelayMilliseconds:0);
        value.number(snapshot?snapshot->networkRevision:0);value.string(snapshot?snapshot->latestFailureUtf8:"none");
        return peerchat::platform::android::Context::input(env,value.data);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
#endif
extern "C" {
pc_core_status pc_core_get_info(pc_core_handle handle,pc_core_info* info){return guarded([&]()->pc_core_status{
    if(!info||info->struct_size!=sizeof(pc_core_info))return PC_CORE_INVALID_ARGUMENT;
    auto instance=lookup(handle);if(!instance)return PC_CORE_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(instance->gate);
    uint32_t loaded=0;
#if defined(__ANDROID__)
    loaded=instance->storage?1:0;
#endif
    *info={sizeof(pc_core_info),PC_CORE_ABI_VERSION,1,loaded,loaded,loaded};return PC_CORE_OK;
});}
pc_core_status pc_core_exchange_utf8(pc_core_handle handle,const uint8_t* input,uint32_t length,uint8_t* output,uint32_t capacity,uint32_t* required){return guarded([&]()->pc_core_status{
    if(!required)return PC_CORE_INVALID_ARGUMENT;
    *required=0;if((!input&&length)||(!output&&capacity)||length>PC_CORE_MAX_EXCHANGE_BYTES)return PC_CORE_INVALID_ARGUMENT;
    auto instance=lookup(handle);if(!instance)return PC_CORE_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(instance->gate);
    std::string text;if(length)text.assign(reinterpret_cast<const char*>(input),length);
    try{(void)peerchat::unicode::decode(text);}catch(const std::invalid_argument&){return PC_CORE_INVALID_UTF8;}
    peerchat::protocol::Writer wire(peerchat::protocol::Text);wire.string(text);
    peerchat::protocol::Reader reader(wire.data);auto copy=reader.string();reader.end();
    *required=static_cast<uint32_t>(copy.size());if(capacity<copy.size())return PC_CORE_BUFFER_TOO_SMALL;
    if(!copy.empty())std::memmove(output,copy.data(),copy.size());
    return PC_CORE_OK;
});}
pc_core_status pc_core_open_profile(pc_core_handle handle,const uint8_t* path,uint32_t length){return guarded([&]()->pc_core_status{
    if(!path||!length||length>32768)return PC_CORE_INVALID_ARGUMENT;
    if(!lookup(handle))return PC_CORE_INVALID_HANDLE;
    return PC_CORE_PLATFORM_UNAVAILABLE;
});}
}
#if defined(__ANDROID__)
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ProfileBridge_bind(JNIEnv* env,jobject,jlong handle,jobject facade){
    return guarded([&]()->pc_core_status{
        if(!facade)return PC_CORE_INVALID_ARGUMENT;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);if(instance->storage)return PC_CORE_INVALID_ARGUMENT;
        auto providers=peerchat::platform::android::platform(env,facade);
#if defined(PEERCHAT_STORAGE_TEST_HOOKS)
        auto context=std::static_pointer_cast<peerchat::platform::android::Identity>(providers->identity)->context;
        auto scope=context->bytes("scope","()[B");std::string name(scope.begin(),scope.end());
        instance->testProfile=name.rfind("test_",0)==0;
        if(instance->testProfile)instance->testDatabase=providers->database;
#endif
        auto store=std::make_unique<peerchat::Storage>(providers);auto profile=store->profile();
        auto delivery=std::make_unique<peerchat::delivery::DeliveryManager>(*store,profile.peerId);
        // Rebind the existing session owner; no second session/retry owner.
        instance->sessions=peerchat::session::SessionTransportManager(profile.peerId,profile.peerId,{1});
        instance->profile=std::move(profile);instance->storage=std::move(store);instance->deliveries=std::move(delivery);
        auto owner=instance.get();
        instance->relayRequests=std::make_unique<peerchat::portable::RelayRequests>(instance->sessions,instance->friends,peerchat::session::SessionEpoch{1},instance->nextTls,
            [owner](const peerchat::session::ReliableAuthorization& scope){return owner->friends.localAccount()==scope.archive.account&&owner->archiveApplication&&
                peerchat::archive::currentSessionAuthorization(owner->archiveApplication->policy(),scope);});
        return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jlong JNICALL Java_com_peerchat_android_PeerTlsBridge_open(JNIEnv* env,jobject,jlong handle,jboolean incoming,jbyteArray pin){
    try{
        if(!pin || env->GetArrayLength(pin)!=64)return 0;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return 0;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage || instance->tls.size()+instance->peers.size()>=32 || instance->nextTls==std::numeric_limits<uint64_t>::max())return 0;
        auto bytes=peerchat::platform::android::Context::output(env,pin);
        auto identity=std::dynamic_pointer_cast<peerchat::platform::android::Identity>(instance->profile.identity);
        if(!identity)return 0;
        auto factory=std::make_shared<peerchat::platform::android::TlsFactory>(identity->context);
        peerchat::TlsCredentials credentials(instance->profile,std::move(factory));
        auto session=std::make_unique<peerchat::TlsSession>(credentials,incoming==JNI_TRUE,std::string(bytes.begin(),bytes.end()));
        auto id=instance->nextTls++;instance->tls.emplace(id,std::move(session));return static_cast<jlong>(id);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return 0;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_PeerTlsBridge_exchange(JNIEnv* env,jobject,jlong handle,jlong token,jint operation,jbyteArray input){
    try{
        if(!input || env->GetArrayLength(input)>262144)return nullptr;
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);
        auto found=instance->tls.find(static_cast<uint64_t>(token));if(found==instance->tls.end())return nullptr;
        auto& session=*found->second;peerchat::Blob output;
        try{
            if(operation==0 || operation==1){
                auto bytes=peerchat::platform::android::Context::output(env,input);
                if(operation==0)session.receive(bytes);else session.send(bytes);
            }else{
                if(env->GetArrayLength(input)!=0)return nullptr;
                switch(operation){
                    case 2:output=session.takeOutput();break;
                    case 3:output=session.takePlaintext();break;
                    case 4:output=session.remotePublicKey();break;
                    case 5:output={static_cast<unsigned char>(session.ready()),static_cast<unsigned char>(session.closed())};break;
                    case 6:session.shutdown();break;
                    default:return nullptr;
                }
            }
        }catch(...){instance->tls.erase(found);throw;}
        return peerchat::platform::android::Context::input(env,output);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_PeerTlsBridge_close(JNIEnv*,jobject,jlong handle,jlong token){
    return guarded([&]()->pc_core_status{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);
        return instance->tls.erase(static_cast<uint64_t>(token))?PC_CORE_OK:PC_CORE_INVALID_HANDLE;
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ProfileBridge_publicKey(JNIEnv* env,jobject,jlong handle){
    try{auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage)return nullptr;
        return peerchat::platform::android::Context::input(env,instance->profile.publicKey);
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_ProfileBridge_sign(JNIEnv* env,jobject,jlong handle,jbyteArray bytes){
    try{if(!bytes||env->GetArrayLength(bytes)>70000)return nullptr;auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return nullptr;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage)return nullptr;
        auto value=peerchat::platform::android::Context::output(env,bytes);
        return peerchat::platform::android::Context::input(env,peerchat::signDevicePayload(value,instance->profile));
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
#if defined(PEERCHAT_STORAGE_TEST_HOOKS)
// Debug-only synthetic fixture. Never available for the normal profile or release.
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ProfileBridge_attachmentTest(JNIEnv*,jobject,jlong handle){
    return guarded([&]()->pc_core_status{
        using namespace peerchat;auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);if(!instance->storage||!instance->testProfile)return PC_CORE_INVALID_ARGUMENT;
        auto identity=std::dynamic_pointer_cast<platform::android::Identity>(instance->profile.identity);protocol::check(bool(identity));auto path=identity->context->bytes("attachmentDirectory","()[B");
        auto cache=std::make_shared<transfer::posix::Cache>(std::string(path.begin(),path.end()));Blob content(100000);for(size_t n=0;n<content.size();++n)content[n]=static_cast<unsigned char>(n*17);
        transfer::posix::Sha256 hash;for(size_t n=0;n<content.size();n+=transfer::ChunkBytes)hash.update(content.data()+n,std::min(transfer::ChunkBytes,content.size()-n));
        transfer::DurableState d;d.metadata={Storage::newMessageId(),u8"résumé-日本.bin",content.size(),hash.finish()};
        cache->reserve(d.metadata.size);
        {transfer::posix::FileSink file(cache,d.metadata);file.append(Blob(content.begin(),content.begin()+transfer::ChunkBytes));file.flush();d.location=file.resumeToken();d.done=transfer::ChunkBytes;}
        {auto file=transfer::posix::FileSink::reopen(cache,d);protocol::check(file->read(0,transfer::ChunkBytes)==Blob(content.begin(),content.begin()+transfer::ChunkBytes));
            for(size_t n=transfer::ChunkBytes;n<content.size();n+=transfer::ChunkBytes)file->append(Blob(content.begin()+n,content.begin()+std::min(content.size(),n+transfer::ChunkBytes)));
            file->commit();file->commit();d.done=d.metadata.size;d.state=transfer::State::Complete;d.location=file->resumeToken();}
        auto committed=transfer::posix::FileSink::reopen(cache,d);auto sourceRecord=d;sourceRecord.location=committed->sourceToken();
        {auto source=transfer::posix::FileSource::reopen(cache,sourceRecord);transfer::posix::Sha256 verified;
            for(size_t n=0;n<content.size();n+=transfer::ChunkBytes){auto bytes=source->read(n,std::min(transfer::ChunkBytes,content.size()-n));protocol::check(bytes==Blob(content.begin()+n,content.begin()+n+bytes.size()));verified.update(bytes.data(),bytes.size());}protocol::check(verified.finish()==d.metadata.digest);}
        bool rejected=false;{transfer::posix::FileSink replacement(cache,d.metadata);for(size_t n=0;n<content.size();n+=transfer::ChunkBytes)replacement.append(Blob(content.begin()+n,content.begin()+std::min(content.size(),n+transfer::ChunkBytes)));
            try{replacement.commit();}catch(const std::exception&){rejected=true;}protocol::check(rejected);replacement.discard();}
        // Partial replacement cannot overwrite the independently committed file.
        protocol::check(transfer::posix::FileSource::reopen(cache,sourceRecord)->size()==content.size());
        auto cancel=d.metadata;cancel.id=Storage::newMessageId();{transfer::posix::FileSink file(cache,cancel);file.append(Blob{1});file.discard();}
        struct stat s{};protocol::check(fstatat(cache->fd(),(cancel.id+".part").c_str(),&s,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT);
        auto wrong=sourceRecord;wrong.location.back()^=1;rejected=false;try{transfer::posix::FileSource::reopen(cache,wrong);}catch(const std::exception&){rejected=true;}protocol::check(rejected);
        return PC_CORE_OK;
    });
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ProfileBridge_storageTest(JNIEnv*,jobject,jlong handle,jint mode){
    return guarded([&]()->pc_core_status{
        auto instance=lookup(static_cast<pc_core_handle>(handle));if(!instance)return PC_CORE_INVALID_HANDLE;
        std::lock_guard<std::mutex> lock(instance->gate);
        if(!instance->storage||!instance->testProfile||mode<0||mode>3)return PC_CORE_INVALID_ARGUMENT;
        auto& store=*instance->storage;auto& delivery=*instance->deliveries;
        const std::string peer(64,'b'),id(32,'c');
        const std::string utf8=u8"Native Android profile proof / 日本語 / 😀";
        peerchat::StoredMessage message{id,instance->profile.peerId,peer,peerchat::unicode::decode(utf8)+std::wstring(1,L'\0')+L"tail",42,peerchat::Delivery::Queued,0,1};
        auto check=[](bool value){if(!value)throw std::runtime_error("Native storage proof failed");};
        if(mode==0){store.append(peer,message);check(!store.append(peer,message));check(store.pending(peer).size()==1);}
        if(mode==1){check(delivery.authorizeTransport({77},peer)&&delivery.submittedMessage(peer,id,{77})&&delivery.acknowledgeMessage(peer,{77},id));}
        if(mode==2){check(store.pending(peer).empty());}
        auto history=store.history(peer);check(history.size()==1&&history[0].text==message.text&&history[0].logicalOrder==1);
        if(mode==1||mode==2)check(history[0].delivery==peerchat::Delivery::Delivered);
        if(mode==3){
            auto query=instance->testDatabase->prepare("SELECT b FROM messages WHERE id=?");query->bindText(1,id);check(query->row());auto bad=query->bytes(0);query.reset();bad[0]^=255;
            auto update=instance->testDatabase->prepare("UPDATE messages SET b=? WHERE id=?");update->bindBlob(1,bad);update->bindText(2,id);update->row();update.reset();
            bool rejected=false;try{store.history(peer);}catch(...){rejected=true;}check(rejected);
        }
        return PC_CORE_OK;
    });
}
#endif
#endif
