#pragma once
#include "device_key_manager.hpp"
#include "mailbox_protocol.hpp"

namespace peerchat::mailbox {
enum class KeyPreparationStage {
    RegistrySnapshot, RecipientProvision, RecipientRotation,
    BindingReadValidate, BindingParse, BindingKeyId, BindingPublicKey, BindingGeneration,
    BindingIdentityRead, BindingAccount, BindingDevice, BindingIdentityFingerprint, BindingSignature,
    BindingSign, BindingValidate, BindingPersist, BindingPrepared
};
inline const char* keyPreparationStageName(KeyPreparationStage stage) noexcept {
    switch(stage) {
    case KeyPreparationStage::RegistrySnapshot:return "registry_snapshot";
    case KeyPreparationStage::RecipientProvision:return "recipient_provision";
    case KeyPreparationStage::RecipientRotation:return "recipient_rotation";
    case KeyPreparationStage::BindingReadValidate:return "binding_read_validate";
    case KeyPreparationStage::BindingParse:return "binding_parse";
    case KeyPreparationStage::BindingKeyId:return "binding_metadata_key_id";
    case KeyPreparationStage::BindingPublicKey:return "binding_metadata_public_key";
    case KeyPreparationStage::BindingGeneration:return "binding_metadata_generation";
    case KeyPreparationStage::BindingIdentityRead:return "binding_identity_read";
    case KeyPreparationStage::BindingAccount:return "binding_account";
    case KeyPreparationStage::BindingDevice:return "binding_device";
    case KeyPreparationStage::BindingIdentityFingerprint:return "binding_identity_fingerprint";
    case KeyPreparationStage::BindingSignature:return "binding_signature";
    case KeyPreparationStage::BindingSign:return "binding_sign";
    case KeyPreparationStage::BindingValidate:return "binding_validate";
    case KeyPreparationStage::BindingPersist:return "binding_persist";
    case KeyPreparationStage::BindingPrepared:return "binding_prepared";
    }
    return "unknown";
}
// Profile-scoped application adapter. Identity and recipient custody remain in
// existing providers/KeyManager; server requests stay in the authenticated
// channel owner. This class has no transport, timer, retry loop or TLS state.
class KeyCoordinator {
    DeviceKeyManager& manager_;
    platform::IdentityProvider& identity_;
    Verify verify_;Fingerprint fingerprint_;
    std::string bindingDiagnostic_="binding_inspection=not_requested",inspectedKeyId_;
    static std::string publicId(const std::string& value,std::size_t length){return id(value,length)?value:"invalid";}
public:
    KeyCoordinator(DeviceKeyManager& manager,platform::IdentityProvider& identity,Verify verify,Fingerprint fingerprint)
        :manager_(manager),identity_(identity),verify_(std::move(verify)),fingerprint_(std::move(fingerprint)){}
    // Public identifiers and equality results only; no aliases/private/protected bytes.
    void resetDiagnostic(){bindingDiagnostic_="binding_inspection=not_read";inspectedKeyId_.clear();}
    const std::string& bindingDiagnostic()const noexcept{return bindingDiagnostic_;}
    const std::string& inspectedKeyId()const noexcept{return inspectedKeyId_;}
    RecipientBinding prepare(std::uint64_t accountId,bool allowed,
                             const std::function<std::string(std::uint32_t)>& alias,
                             const std::function<std::string()>& keyId,std::uint64_t now,
                             const std::function<void(KeyPreparationStage)>& diagnostic = {}) {
        // Emit before each operation so an unwinding SQL ROLLBACK cannot hide
        // the failed native phase. Only fixed enum values cross this boundary.
        auto stage=[&](KeyPreparationStage value){if(diagnostic)diagnostic(value);};
        resetDiagnostic();
        require(account(accountId)&&now>0);stage(KeyPreparationStage::RegistrySnapshot);
        auto entries=manager_.snapshot();DeviceKeyMetadata metadata;
        if(entries.empty()) {
            stage(KeyPreparationStage::RecipientProvision);
            metadata=manager_.provisionRecipient(keyId(),alias(1),1,now);
        }
        else {
            auto pending=std::find_if(entries.begin(),entries.end(),[](const auto& e){return e.state==DeviceKeyState::Provisioning;});
            if(pending!=entries.end()) {
                stage(KeyPreparationStage::RecipientProvision);
                metadata=manager_.provisionRecipient(pending->id,pending->providerAlias,pending->generation,pending->createdUtcMilliseconds);
            }
            else {
                auto active=std::find_if(entries.begin(),entries.end(),[](const auto& e){return e.state==DeviceKeyState::Active;});
                require(active!=entries.end());metadata=*active;
            }
        }
        if(!metadata.signedBinding.empty()) {
            stage(KeyPreparationStage::BindingReadValidate);
            inspectedKeyId_=metadata.id;
            bindingDiagnostic_="registry_state=read_valid registry_schema=PKR1 metadata_key_id="+publicId(metadata.id,32)+
                " metadata_generation="+std::to_string(metadata.generation)+" metadata_registration="+std::to_string(static_cast<unsigned>(metadata.registration))+
                " binding_inspection=parse_pending";
            stage(KeyPreparationStage::BindingParse);
            auto binding=parseBinding(metadata.signedBinding);
            bindingDiagnostic_="registry_state=read_valid registry_schema=PKR1 binding_schema=1 binding_canonical=true metadata_key_id="+publicId(metadata.id,32)+
                " metadata_generation="+std::to_string(metadata.generation)+" metadata_registration="+std::to_string(static_cast<unsigned>(metadata.registration))+
                " binding_account="+std::to_string(binding.accountId)+" binding_device="+publicId(binding.device,64)+
                " binding_key_id="+publicId(binding.keyId,32)+" binding_generation="+std::to_string(binding.generation)+
                " binding_key_matches="+(binding.keyId==metadata.id?"true":"false")+
                " binding_public_matches="+(binding.publicKey==metadata.publicKey?"true":"false")+
                " metadata_algorithm_matches=true"+
                " binding_generation_matches="+(binding.generation==metadata.generation?"true":"false")+
                " binding_created_matches="+(binding.createdUtc==metadata.createdUtcMilliseconds?"true":"false")+
                " binding_predecessor_matches="+(binding.predecessorId==metadata.predecessorId?"true":"false")+
                " binding_account_matches="+(binding.accountId==accountId?"true":"false");
            stage(KeyPreparationStage::BindingKeyId);require(binding.keyId==metadata.id);
            stage(KeyPreparationStage::BindingPublicKey);require(binding.publicKey==metadata.publicKey);
            stage(KeyPreparationStage::BindingGeneration);require(binding.generation==metadata.generation);
            stage(KeyPreparationStage::BindingIdentityRead);auto identity=identity_.publicKey();auto device=fingerprint_(identity);
            bool signatureValid=false;try{signatureValid=verify_(bindingPayload(binding),binding.signature,identity);}catch(...){}
            bindingDiagnostic_+=" local_identity_device="+publicId(device,64)+" binding_device_matches="+(binding.device==device?"true":"false")+
                " binding_signature_valid="+(signatureValid?"true":"false");
            stage(KeyPreparationStage::BindingAccount);require(binding.accountId==accountId);
            stage(KeyPreparationStage::BindingDevice);require(binding.device==device);
            stage(KeyPreparationStage::BindingIdentityFingerprint);require(fingerprint_(identity)==device);
            stage(KeyPreparationStage::BindingSignature);
            require(verifyBinding(binding,accountId,device,identity,verify_,fingerprint_));
            if(binding.storageAllowed==allowed||metadata.registration!=DeviceKeyRegistration::Accepted) {
                stage(KeyPreparationStage::BindingPrepared);return binding;
            }
            // Consent is immutable within a signed generation. Finish/reconcile
            // any pending registration first, then explicitly rotate; retired
            // keys remain available only for already-authorized old envelopes.
            require(metadata.generation<std::numeric_limits<std::uint32_t>::max());
            stage(KeyPreparationStage::RecipientRotation);
            metadata=manager_.rotateRecipient(metadata.id,keyId(),alias(metadata.generation+1),metadata.generation+1,now);
        }
        stage(KeyPreparationStage::BindingSign);
        RecipientBinding binding;binding.accountId=accountId;binding.device=fingerprint_(identity_.publicKey());binding.keyId=metadata.id;
        binding.generation=metadata.generation;binding.createdUtc=metadata.createdUtcMilliseconds;binding.predecessorId=metadata.predecessorId;
        binding.publicKey=metadata.publicKey;binding.storageAllowed=allowed;binding.signature=identity_.sign(bindingPayload(binding));
        stage(KeyPreparationStage::BindingValidate);
        require(verifyBinding(binding,accountId,binding.device,identity_.publicKey(),verify_,fingerprint_));
        stage(KeyPreparationStage::BindingPersist);
        manager_.recordRegistration(binding.keyId,serializeBinding(binding),false);
        stage(KeyPreparationStage::BindingPrepared);return binding;
    }
    void accepted(const RecipientBinding& binding) {
        require(verifyBinding(binding,binding.accountId,fingerprint_(identity_.publicKey()),identity_.publicKey(),verify_,fingerprint_));
        manager_.recordRegistration(binding.keyId,serializeBinding(binding),true);
    }
};
}
