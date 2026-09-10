// Presentation-only C ABI. All networking, identity and persistence stay in Node.
// One owner calls this adapter serially; Node owns its existing worker thread.
#include "node.hpp"
#include "share_id.hpp"
#include "audio.hpp"
#include <sstream>
#include <stdexcept>
#include <map>
#define UI_API extern "C" __declspec(dllexport)
struct UiBridge { peerchat::Node node; std::wstring output; std::map<std::string,peerchat::Contact> contacts; };
static std::wstring widen(const std::string& value){return {value.begin(),value.end()};}
static std::string identity(const wchar_t* value){std::wstring text=value?value:L"";std::string result;for(auto ch:text){if(ch>127)throw std::runtime_error("Invalid identity");result+=char(ch);}return result;}
static std::wstring quote(const std::wstring& value){
    std::wostringstream out;out<<L'"';
    for(auto ch:value){if(ch==L'"'||ch==L'\\')out<<L'\\'<<ch;else if(ch<32){const wchar_t* hex=L"0123456789abcdef";out<<L"\\u00"<<hex[(ch>>4)&15]<<hex[ch&15];}else out<<ch;}
    out<<L'"';return out.str();
}
UI_API void* ui_create(){try{return new UiBridge;}catch(...){return nullptr;}}
UI_API void ui_destroy(void* handle){delete static_cast<UiBridge*>(handle);}
UI_API int ui_start(void* handle,const wchar_t* directory,unsigned short port,const wchar_t* name){try{static_cast<UiBridge*>(handle)->node.start(directory,port,name?name:L"");return 1;}catch(...){return 0;}}
UI_API int ui_connect(void* handle,const wchar_t* host,unsigned short port,const wchar_t* peer){try{static_cast<UiBridge*>(handle)->node.connect(host,port,identity(peer));return 1;}catch(...){return 0;}}
UI_API int ui_connect_ex(void* handle,const wchar_t* host,unsigned short port,const wchar_t* peer,const wchar_t* source,const wchar_t* route,int force){try{static_cast<UiBridge*>(handle)->node.connect(host,port,identity(peer),source?source:L"Direct",route?route:L"manual",force!=0);return 1;}catch(...){return 0;}}
UI_API int ui_approve(void* handle,const wchar_t* peer,int accepted){try{static_cast<UiBridge*>(handle)->node.approve(identity(peer),accepted!=0);return 1;}catch(...){return 0;}}
UI_API int ui_send(void* handle,const wchar_t* peer,const wchar_t* text){try{return !static_cast<UiBridge*>(handle)->node.send(identity(peer),text).empty();}catch(...){return 0;}}
UI_API int ui_remove(void* handle,const wchar_t* peer){try{auto& bridge=*static_cast<UiBridge*>(handle);std::string id=identity(peer);bridge.node.removeContact(id);bridge.contacts.erase(id);return 1;}catch(...){return 0;}}
UI_API int ui_history(void* handle,const wchar_t* peer,long long before){try{static_cast<UiBridge*>(handle)->node.loadHistory(identity(peer),before);return 1;}catch(...){return 0;}}
UI_API int ui_group_account(void* handle,unsigned long long account,const wchar_t* name){try{static_cast<UiBridge*>(handle)->node.setGroupAccount(account,name?name:L"");return account&&name&&*name;}catch(...){return 0;}}
UI_API const wchar_t* ui_group_create(void* handle,const wchar_t* name){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.output=widen(bridge.node.createGroup(name?name:L""));}catch(...){bridge.output.clear();}return bridge.output.c_str();}
UI_API int ui_group_rename(void* handle,const wchar_t* group,const wchar_t* name){try{return static_cast<UiBridge*>(handle)->node.renameGroup(identity(group),name?name:L"");}catch(...){return 0;}}
UI_API int ui_group_invite(void* handle,const wchar_t* group,unsigned long long account,const wchar_t* peer,const wchar_t* name){try{return static_cast<UiBridge*>(handle)->node.inviteToGroup(identity(group),account,identity(peer),name?name:L"");}catch(...){return 0;}}
UI_API int ui_group_respond(void* handle,const wchar_t* group,int accepted){try{return static_cast<UiBridge*>(handle)->node.respondToGroupInvite(identity(group),accepted!=0);}catch(...){return 0;}}
UI_API int ui_group_leave(void* handle,const wchar_t* group){try{return static_cast<UiBridge*>(handle)->node.leaveGroup(identity(group));}catch(...){return 0;}}
UI_API int ui_group_remove_member(void* handle,const wchar_t* group,unsigned long long account){try{return static_cast<UiBridge*>(handle)->node.removeGroupMember(identity(group),account);}catch(...){return 0;}}
UI_API int ui_group_role(void* handle,const wchar_t* group,unsigned long long account,int admin){try{return static_cast<UiBridge*>(handle)->node.setGroupAdmin(identity(group),account,admin!=0);}catch(...){return 0;}}
UI_API int ui_group_send(void* handle,const wchar_t* group,const wchar_t* text){try{return static_cast<UiBridge*>(handle)->node.sendGroup(identity(group),text?text:L"");}catch(...){return 0;}}
UI_API int ui_groups(void* handle){try{static_cast<UiBridge*>(handle)->node.loadGroups();return 1;}catch(...){return 0;}}
UI_API const wchar_t* ui_voice_call(void* handle,const wchar_t* peer){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.output=widen(bridge.node.startVoiceCall(identity(peer)));}catch(...){bridge.output.clear();}return bridge.output.c_str();}
UI_API int ui_voice_answer(void* handle,const wchar_t* call){try{return static_cast<UiBridge*>(handle)->node.answerVoiceCall(identity(call));}catch(...){return 0;}}
UI_API int ui_voice_decline(void* handle,const wchar_t* call){try{return static_cast<UiBridge*>(handle)->node.declineVoiceCall(identity(call));}catch(...){return 0;}}
UI_API int ui_voice_end(void* handle,const wchar_t* call){try{return static_cast<UiBridge*>(handle)->node.endVoiceCall(identity(call));}catch(...){return 0;}}
UI_API int ui_voice_mute(void* handle,int muted){try{return static_cast<UiBridge*>(handle)->node.setVoiceMuted(muted!=0);}catch(...){return 0;}}
UI_API int ui_voice_deafen(void* handle,int deafened){try{return static_cast<UiBridge*>(handle)->node.setVoiceDeafened(deafened!=0);}catch(...){return 0;}}
UI_API int ui_voice_devices_select(void* handle,int input,int output){try{return static_cast<UiBridge*>(handle)->node.setVoiceDevices(input,output);}catch(...){return 0;}}
UI_API const wchar_t* ui_voice_devices(void* handle){auto& bridge=*static_cast<UiBridge*>(handle);try{std::wostringstream out;out<<L"{\"inputs\":[";bool first=true;for(const auto& device:peerchat::Audio::inputDevices()){if(!first)out<<L',';first=false;out<<L"{\"id\":"<<device.id<<L",\"name\":"<<quote(device.name)<<L'}';}out<<L"],\"outputs\":[";first=true;for(const auto& device:peerchat::Audio::outputDevices()){if(!first)out<<L',';first=false;out<<L"{\"id\":"<<device.id<<L",\"name\":"<<quote(device.name)<<L'}';}out<<L"]}";bridge.output=out.str();}catch(...){bridge.output=L"{\"inputs\":[],\"outputs\":[]}";}return bridge.output.c_str();}
UI_API const wchar_t* ui_share(void* handle,const wchar_t* host,unsigned short port,const wchar_t* peer){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.output=peerchat::encodeShareId({host,port,identity(peer)});}catch(...){bridge.output.clear();}return bridge.output.c_str();}
UI_API const wchar_t* ui_add(void* handle,const wchar_t* invite,const wchar_t* ownPeer){auto& bridge=*static_cast<UiBridge*>(handle);try{
    peerchat::SharedContact shared;std::wstring error;
    if(!peerchat::parseShareId(invite,shared,error))bridge.output=error;
    else if(shared.peerId==identity(ownPeer))bridge.output=L"That is your own PeerChat ID.";
    else {
        // Re-pasting a saved identity must not discard its verified public key.
        auto known=bridge.contacts.find(shared.peerId);
        peerchat::Contact contact=known==bridge.contacts.end()?peerchat::Contact{shared.peerId,L"Friend",{},{},false}:known->second;
        bool exists=false;for(const auto& endpoint:contact.endpoints)if(endpoint.host==shared.host&&endpoint.port==shared.port)exists=true;
        if(!exists){if(contact.endpoints.size()>=128)contact.endpoints.pop_back();contact.endpoints.insert(contact.endpoints.begin(),{shared.host,shared.port});}
        bridge.node.saveContact(contact);bridge.output.clear();
    }
}catch(...){bridge.output=L"Could not queue this contact.";}return bridge.output.c_str();}
UI_API const wchar_t* ui_poll(void* handle){auto& bridge=*static_cast<UiBridge*>(handle);try{
    std::wostringstream out;out<<L'[';bool first=true;
    for(const auto& event:bridge.node.poll()){
        if(event.kind==peerchat::NodeEvent::Kind::Contacts){bridge.contacts.clear();for(const auto& contact:event.contacts)bridge.contacts[contact.peerId]=contact;}
        if(!first)out<<L',';
        first=false;
        out<<L"{\"kind\":"<<int(event.kind)<<L",\"peer\":"<<quote(widen(event.peerId))<<L",\"expected\":"<<quote(widen(event.expectedPeerId))<<L",\"text\":"<<quote(event.text)<<L",\"detail\":"<<quote(event.detail)<<L",\"candidate\":"<<quote(event.candidate)<<L",\"source\":"<<quote(event.candidateSource)<<L",\"route\":"<<quote(event.route)<<L",\"attempt\":"<<event.attemptId<<L",\"retryMs\":"<<event.retryDelayMilliseconds<<L",\"port\":"<<event.port<<L",\"contacts\":[";
        bool firstContact=true;for(const auto& contact:event.contacts){if(!firstContact)out<<L',';firstContact=false;
            out<<L"{\"peer\":"<<quote(widen(contact.peerId))<<L",\"name\":"<<quote(contact.name)<<L",\"verified\":"<<(contact.verified?L"true":L"false")<<L",\"host\":"<<quote(contact.endpoints.empty()?L"":contact.endpoints.front().host)<<L",\"port\":"<<(contact.endpoints.empty()?0:contact.endpoints.front().port)<<L'}';}
        out<<L"],\"messages\":[";bool firstMessage=true;for(const auto& message:event.messages){if(!firstMessage)out<<L',';firstMessage=false;
            out<<L"{\"id\":"<<quote(widen(message.id))<<L",\"sender\":"<<quote(widen(message.sender))<<L",\"text\":"<<quote(message.text)<<L",\"time\":"<<message.utcMilliseconds<<L",\"logical\":"<<message.logicalOrder<<L",\"delivery\":"<<int(message.delivery)<<L",\"sequence\":"<<message.sequence<<L'}';}
        out<<L"],\"groups\":[";bool firstGroup=true;for(const auto& group:event.groups){if(!firstGroup)out<<L',';firstGroup=false;
            out<<L"{\"id\":"<<quote(widen(group.groupId))<<L",\"name\":"<<quote(group.name)<<L",\"owner\":"<<group.ownerAccountId<<L",\"members\":[";bool firstMember=true;for(const auto& member:group.members){if(!firstMember)out<<L',';firstMember=false;out<<L"{\"account\":"<<member.first<<L",\"peer\":"<<quote(widen(member.second.peerId))<<L",\"name\":"<<quote(member.second.name)<<L",\"role\":"<<int(member.second.role)<<L'}';}out<<L"],\"invites\":[";bool firstInvite=true;for(const auto& invitation:group.invitations){if(!firstInvite)out<<L',';firstInvite=false;out<<L"{\"account\":"<<invitation.first<<L",\"peer\":"<<quote(widen(invitation.second.peerId))<<L",\"name\":"<<quote(invitation.second.name)<<L",\"role\":1}";}out<<L"]}";
        }
        out<<L"],\"groupEvents\":[";bool firstGroupEvent=true;for(const auto& groupEvent:event.groupEvents){if(!firstGroupEvent)out<<L',';firstGroupEvent=false;out<<L"{\"id\":"<<quote(widen(groupEvent.eventId))<<L",\"group\":"<<quote(widen(groupEvent.groupId))<<L",\"account\":"<<groupEvent.actorAccountId<<L",\"sender\":"<<quote(widen(groupEvent.actorPeerId))<<L",\"text\":"<<quote(groupEvent.message)<<L",\"time\":"<<groupEvent.createdMilliseconds<<L",\"logical\":"<<groupEvent.logicalOrder<<L'}';}
        out<<L"]}";
    }out<<L']';bridge.output=out.str();
}catch(...){bridge.output=L"[{\"kind\":8,\"text\":\"Could not read core events.\"}]";}return bridge.output.c_str();}
