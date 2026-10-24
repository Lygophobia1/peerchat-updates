#include "../dm_attachment_initial_identity.hpp"
#include "../platform/windows_recipient_key.hpp"
#include <filesystem>
#include <iostream>
using namespace peerchat;
static void check(bool ok){if(!ok)throw std::runtime_error("Initial identity assertion failed");}
template<class F>static void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);}
struct Signing {Profile profile;explicit Signing(Profile p):profile(std::move(p)){}Blob publicKey(){return profile.publicKey;}Blob sign(const Blob& b){return signDevicePayload(b,profile);}};
struct RecipientFixture {
 std::string alias="PeerChat.Recipient.test.initial."+Storage::newMessageId();std::shared_ptr<platform::RecipientKeyHandle> key;
 RecipientFixture(){platform::WindowsRecipientKeyProvider provider;key=provider.create(alias);}
 ~RecipientFixture(){key.reset();platform::recipient_cng::Object provider,opened;if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;auto name=platform::recipient_cng::alias(alias);if(NCryptOpenKey(provider.value,&opened.value,name.c_str(),0,0)==0&&NCryptDeleteKey(opened.value,0)==0)opened.value=0;}
};
int main(int argc,char** argv){try{
 check(argc==2);auto root=std::filesystem::path(argv[1]);Storage local((root/L"local").wstring()),one((root/L"one").wstring()),two((root/L"two").wstring());Signing a(one.profile()),b(two.profile());RecipientFixture encryption;
 auto binding=[&](Signing& signer,char id){mailbox::RecipientBinding value;value.accountId=8;value.device=signer.profile.peerId;value.keyId=std::string(32,id);value.generation=1;value.createdUtc=100;value.publicKey=encryption.key->publicKey();value.storageAllowed=true;value.signature=signer.sign(mailbox::bindingPayload(value));return value;};
 dm_attachments::Audience audience;Signing sender(local.profile());audience.source=binding(sender,'c');audience.source.accountId=1;audience.source.signature=sender.sign(mailbox::bindingPayload(audience.source));audience.relationship=std::string(32,'d');audience.recipients={binding(a,'a'),binding(b,'b')};
 auto proof=[](Signing& signer,const mailbox::RecipientBinding& value){protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/dm-cache-device/v1");w.number(value.accountId);w.string(value.device);w.string(value.keyId);w.number(value.generation);w.number(1);w.number(1);w.bytes(signer.publicKey());w.bytes(signer.sign(w.data));return w.data;};
 std::vector<Blob> proofs{proof(a,audience.recipients[0]),proof(b,audience.recipients[1])};
 auto account=[&](const std::string& id)->std::uint64_t{return id==a.profile.peerId||id==b.profile.peerId?8:0;};
 auto enroll=[&](const auto& wire){return dm_attachments::initialIdentities(audience,wire,local.contacts(),account);};
 auto unknown=[&](const std::string& id)->std::uint64_t{return id==a.profile.peerId?8:0;};
 auto selected=dm_attachments::selectKnownAudience(audience,a.profile.peerId,{},unknown,[](const auto&){return false;});
 check(selected.recipients.size()==1&&selected.recipients[0].device==a.profile.peerId);
 auto pinned=std::vector<Contact>{Contact{a.profile.peerId,L"Friend",a.publicKey(),{},true}};
 selected=dm_attachments::selectKnownAudience(audience,b.profile.peerId,pinned,account,[&](const auto& v){return v.device==a.profile.peerId;});
 check(selected.recipients.size()==1&&selected.recipients[0].device==a.profile.peerId); // Unknown sibling cannot force re-verification.
 rejects([&]{dm_attachments::selectKnownAudience(audience,b.profile.peerId,pinned,account,[](const auto&){return false;});});
 rejects([&]{dm_attachments::selectKnownAudience(audience,a.profile.peerId,{},[](const auto&){return 9;},[](const auto&){return true;});});
 // The preflight accepts an exact verified sibling when the representative is
 // absent. Creation must use that same audience rather than persist the stale peer.
 auto current=[&](const auto& value){return value.device==a.profile.peerId&&mailbox::verifyBinding(value,8,a.profile.peerId,a.publicKey(),verifyDevicePayload,Storage::publicKeyId);};
 check(dm_attachments::verifiedAudiencePeer(selected,b.profile.peerId,8,account,current)==a.profile.peerId);
 check(dm_attachments::verifiedAudiencePeer(selected,a.profile.peerId,8,account,current)==a.profile.peerId);
 rejects([&]{dm_attachments::verifiedAudiencePeer(selected,b.profile.peerId,9,account,current);});
 rejects([&]{dm_attachments::verifiedAudiencePeer(selected,b.profile.peerId,8,unknown,current);});
 rejects([&]{dm_attachments::verifiedAudiencePeer(selected,b.profile.peerId,8,account,[](const auto&){return false;});});
 auto stale=selected;stale.recipients[0].signature.back()^=1;rejects([&]{dm_attachments::verifiedAudiencePeer(stale,b.profile.peerId,8,account,current);});
 stale=selected;stale.recipients[0].accountId=9;rejects([&]{dm_attachments::verifiedAudiencePeer(stale,b.profile.peerId,8,account,current);});
 auto contacts=enroll(proofs);check(contacts.size()==2&&contacts[0].verified&&contacts[1].verified);
 auto broken=proofs;broken[1].back()^=1;rejects([&]{enroll(broken);});check(local.contacts().empty());
 broken=proofs;std::swap(broken[0],broken[1]);rejects([&]{enroll(broken);});
 rejects([&]{dm_attachments::initialIdentities(audience,proofs,{},[](const auto&){return 9;});});
 auto changed=audience;changed.recipients[1].generation++;rejects([&]{dm_attachments::initialIdentities(changed,proofs,{},account);});
 changed=audience;changed.recipients[1].signature.back()^=1;rejects([&]{dm_attachments::initialIdentities(changed,proofs,{},account);});
 rejects([&]{dm_attachments::initialIdentities({}, {}, {}, account);});
 auto invalid=contacts;invalid[1].name.clear();rejects([&]{local.saveContactsAtomically(invalid);});check(local.contacts().empty());
 local.saveContactsAtomically(contacts);check(local.contacts().size()==2&&enroll(proofs).empty());
 local.removeContact(b.profile.peerId);check(enroll(proofs).empty()&&local.contacts().size()==1); // Historical first pin prevents silent new-device enrollment.
 {Storage restarted((root/L"local").wstring());check(restarted.contacts().size()==1&&restarted.contacts()[0].publicKey==a.publicKey());}
 std::cout<<"PASS first cached friend pins: complete signed batch, forgery/order/account/generation/binding rejection, atomic rollback, persisted reuse and historical-pin protection\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
