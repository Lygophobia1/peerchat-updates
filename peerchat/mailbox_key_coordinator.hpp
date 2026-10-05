#pragma once
#include "device_key_manager.hpp"
#include "mailbox_protocol.hpp"

namespace peerchat::mailbox {
// Profile-scoped application adapter. Identity and recipient custody remain in
// existing providers/KeyManager; server requests stay in the authenticated
// channel owner. This class has no transport, timer, retry loop or TLS state.
class KeyCoordinator {
    DeviceKeyManager& manager_;
    platform::IdentityProvider& identity_;
    Verify verify_;Fingerprint fingerprint_;
public:
    KeyCoordinator(DeviceKeyManager& manager,platform::IdentityProvider& identity,Verify verify,Fingerprint fingerprint)
        :manager_(manager),identity_(identity),verify_(std::move(verify)),fingerprint_(std::move(fingerprint)){}
    RecipientBinding prepare(std::uint64_t accountId,bool allowed,
                             const std::function<std::string(std::uint32_t)>& alias,
                             const std::function<std::string()>& keyId,std::uint64_t now) {
        require(account(accountId)&&now>0);auto entries=manager_.snapshot();DeviceKeyMetadata metadata;
        if(entries.empty())metadata=manager_.provisionRecipient(keyId(),alias(1),1,now);
        else {
            auto pending=std::find_if(entries.begin(),entries.end(),[](const auto& e){return e.state==DeviceKeyState::Provisioning;});
            if(pending!=entries.end())metadata=manager_.provisionRecipient(pending->id,pending->providerAlias,pending->generation,pending->createdUtcMilliseconds);
            else {
                auto active=std::find_if(entries.begin(),entries.end(),[](const auto& e){return e.state==DeviceKeyState::Active;});
                require(active!=entries.end());metadata=*active;
            }
        }
        if(!metadata.signedBinding.empty()) {
            auto binding=parseBinding(metadata.signedBinding);
            require(binding.keyId==metadata.id&&binding.publicKey==metadata.publicKey&&binding.generation==metadata.generation&&
                verifyBinding(binding,accountId,fingerprint_(identity_.publicKey()),identity_.publicKey(),verify_,fingerprint_));
            if(binding.storageAllowed==allowed||metadata.registration!=DeviceKeyRegistration::Accepted)return binding;
            // Consent is immutable within a signed generation. Finish/reconcile
            // any pending registration first, then explicitly rotate; retired
            // keys remain available only for already-authorized old envelopes.
            require(metadata.generation<std::numeric_limits<std::uint32_t>::max());
            metadata=manager_.rotateRecipient(metadata.id,keyId(),alias(metadata.generation+1),metadata.generation+1,now);
        }
        RecipientBinding binding;binding.accountId=accountId;binding.device=fingerprint_(identity_.publicKey());binding.keyId=metadata.id;
        binding.generation=metadata.generation;binding.createdUtc=metadata.createdUtcMilliseconds;binding.predecessorId=metadata.predecessorId;
        binding.publicKey=metadata.publicKey;binding.storageAllowed=allowed;binding.signature=identity_.sign(bindingPayload(binding));
        require(verifyBinding(binding,accountId,binding.device,identity_.publicKey(),verify_,fingerprint_));
        manager_.recordRegistration(binding.keyId,serializeBinding(binding),false);return binding;
    }
    void accepted(const RecipientBinding& binding) {
        require(verifyBinding(binding,binding.accountId,fingerprint_(identity_.publicKey()),identity_.publicKey(),verify_,fingerprint_));
        manager_.recordRegistration(binding.keyId,serializeBinding(binding),true);
    }
};
}
