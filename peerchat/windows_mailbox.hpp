#pragma once
#include "mailbox_key_coordinator.hpp"
#include "delivery_manager.hpp"
#include "reliable_peer_protocol.hpp"
#include "session_transport_manager.hpp"
#include "platform/windows_sql_database.hpp"
#include "platform/windows_record_protection.hpp"
#include "platform/windows_recipient_key.hpp"
#include "platform/sql_key_registry.hpp"
#include <filesystem>
#include <chrono>

namespace peerchat::mailbox {
// Thin native profile application adapter. Called ONLY on the existing Node
// worker. Borrows the current identity, session and delivery authorities. No
// socket, timer, reconnect scheduler or independently owned message queue.
class WindowsMailbox {
    struct Signer final : platform::IdentityProvider {
        const Profile& profile;
        explicit Signer(const Profile& value):profile(value){}
        Blob publicKey()override{return profile.publicKey;}
        Blob sign(const Blob& original)override{return signDevicePayload(original,profile);}
    } identity_;
    platform::WindowsRecordProtector records_;
    std::shared_ptr<platform::SqlDatabase> database_;
    platform::SqlKeyRegistryStore registryStore_;
    platform::ProtectedKeyRegistry registry_;
    platform::WindowsRecipientKeyProvider provider_;
    DeviceKeyManager keys_;
    KeyCoordinator coordinator_;
    std::shared_ptr<platform::HpkeProvider> hpke_;
    const Profile& profile_;
    static std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
    Contact contact(Storage& storage,const reliable::AuthenticatedFriendSnapshot& friends,const std::string& device,std::uint64_t account){
        require(account>0&&friends.accountFor(device)==account);
        auto list=storage.contacts();auto found=std::find_if(list.begin(),list.end(),[&](const auto& value){return value.peerId==device;});
        require(found!=list.end()&&found->verified&&Storage::publicKeyId(found->publicKey)==device);return *found;
    }
public:
    // Node-only attachment adapter; the existing device-key registry and HPKE
    // provider retain key custody and operation leases.
    void attachmentKeys(const std::function<void(DeviceKeyManager&,platform::HpkeProvider&,platform::IdentityProvider&)>& operation){operation(keys_,*hpke_,identity_);}
    WindowsMailbox(const std::wstring& directory,const Profile& profile):identity_(profile),
        database_(platform::openWindowsSqlDatabase((std::filesystem::path(directory)/L"device-key-registry").wstring())),
        registryStore_(database_),registry_(registryStore_,records_,profile.peerId),
        keys_(identity_,provider_,registry_),coordinator_(keys_,identity_,verifyDevicePayload,Storage::publicKeyId),profile_(profile){
        hpke_=platform::createShippingHpke();
    }
    Blob execute(unsigned operation,const Blob& input,const std::string& id,bool allowed,
                 Storage& storage,delivery::DeliveryManager& deliveries,const reliable::AuthenticatedFriendSnapshot& friends,
                 session::SessionTransportManager& sessions){
        if(!friends.localAccount())throw std::runtime_error("Mailbox account context is not ready");
        require(input.size()<=MaxWire);
        if(operation==0){
            auto binding=coordinator_.prepare(friends.localAccount(),allowed,[&](std::uint32_t generation){
                return "PeerChat.Recipient."+profile_.peerId+".v"+std::to_string(generation);
            },Storage::newMessageId,now());return serializeBinding(binding);
        }
        if(operation==1){auto binding=parseBinding(input);require(binding.accountId==friends.localAccount());coordinator_.accepted(binding);return {1};}
        if(operation==2){
            std::vector<StoredMessage> pending;std::set<std::string> seen;
            for(auto& message:storage.pendingMailboxUploads())if(friends.accountFor(message.recipient)>0){seen.insert(message.id);pending.push_back(std::move(message));}
            for(const auto& known:storage.contacts())if(known.verified&&friends.accountFor(known.peerId)>0){
                auto route=sessions.snapshot(known.peerId);
                if(delivery::mailboxRoutePolicy(allowed,route?&*route:nullptr)!=delivery::MailboxRoutePolicy::Unavailable)continue;
                for(auto& message:deliveries.pendingMessages(known.peerId,1000)){if(pending.size()==64)break;if(message.delivery!=Delivery::Stored&&seen.insert(message.id).second)pending.push_back(std::move(message));}
                if(pending.size()==64)break;
            }
            protocol::Writer result(protocol::Accepted);result.number(pending.size());
            for(const auto& message:pending){result.string(message.id);result.string(message.recipient);result.number(friends.accountFor(message.recipient));}
            return result.data;
        }
        if(operation==3){
            require(mailbox::id(id,32));auto binding=parseBinding(input);auto verified=contact(storage,friends,binding.device,binding.accountId);
            auto message=storage.message(id);require(message.has_value());auto canonical=contact(storage,friends,message->recipient,binding.accountId);
            auto route=sessions.snapshot(binding.device);
            const bool waitForLive=delivery::mailboxRoutePolicy(allowed,route?&*route:nullptr)!=delivery::MailboxRoutePolicy::Unavailable;
            return deliveries.prepareMailbox(id,friends.localAccount(),binding.accountId,binding,verified,identity_,*hpke_,now(),allowed,waitForLive,&canonical);
        }
        if(operation==4){auto envelope=parse(input);require(envelope.header.senderAccount==friends.localAccount());
            contact(storage,friends,envelope.header.recipientDevice,envelope.header.recipientAccount);
            deliveries.mailboxStored(envelope.header.messageId,envelope.header.recipientDevice,input);return {1};}
        if(operation==5){
            auto envelope=parse(input);const auto& header=envelope.header;require(header.recipientAccount==friends.localAccount());
            auto verified=contact(storage,friends,header.senderDevice,header.senderAccount);
            auto entries=keys_.snapshot();auto key=std::find_if(entries.begin(),entries.end(),[&](const auto& value){return value.id==header.keyId;});
            require(key!=entries.end()&&!key->signedBinding.empty());auto binding=parseBinding(key->signedBinding);
            auto accepted=deliveries.acceptMailbox(input,header.senderAccount,verified,binding,keys_,identity_,*hpke_,now());
            protocol::Writer result(protocol::Ack);result.number(accepted.inserted?1:0);result.bytes(accepted.receipt);
            result.string(header.senderDevice);result.string(header.messageId);return result.data;
        }
        if(operation==6){auto receipt=parseReceipt(input);auto verified=contact(storage,friends,receipt.recipientDevice,receipt.recipientAccount);
            deliveries.acknowledgeMailbox(input,receipt.recipientAccount,verified);return {1};}
        if(operation==7)return identity_.publicKey();
        if(operation==8){
            require(allowed&&mailbox::id(id,32));auto message=storage.message(id);require(message.has_value());
            const auto accountId=friends.accountFor(message->recipient);auto canonical=contact(storage,friends,message->recipient,accountId);
            auto primaryRoute=sessions.snapshot(canonical.peerId);std::vector<RecipientBinding> prepared;
            if(delivery::mailboxRoutePolicy(allowed,primaryRoute?&*primaryRoute:nullptr)==delivery::MailboxRoutePolicy::Unavailable)for(const auto& binding:parseBindingBatch(input)){
                require(binding.accountId==accountId);
                // Server authorization never substitutes for an existing native
                // identity pin. An unverified device is not silently trusted.
                auto known=storage.contacts();auto found=std::find_if(known.begin(),known.end(),[&](const auto& c){return c.peerId==binding.device&&c.verified;});
                if(found==known.end()||friends.accountFor(binding.device)!=accountId)continue;
                auto verified=contact(storage,friends,binding.device,accountId);
                auto wire=deliveries.prepareMailbox(id,friends.localAccount(),accountId,binding,verified,identity_,*hpke_,now(),allowed,false,&canonical);
                if(!wire.empty()&&!storage.mailboxUploadAccepted(id,binding.device))prepared.push_back(binding);
            }
            // No network upload until all authorized envelopes are durable. If
            // the process stops here, existing storage drives the next pass.
            protocol::Writer result(protocol::Accepted);result.number(prepared.size());
            for(const auto& binding:prepared)result.bytes(serializeBinding(binding));
            return result.data;
        }
        throw std::runtime_error("Unsupported native mailbox command");
    }
};
}
