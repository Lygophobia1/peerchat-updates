// Presentation-only C ABI. All networking, identity and persistence stay in Node.
// One owner calls this adapter serially; Node owns its existing worker thread.
#include "node.hpp"
#include "share_id.hpp"
#include "audio.hpp"
#include <sstream>
#include <stdexcept>
#include <map>
#define UI_API extern "C" __declspec(dllexport)
struct UiBridge { peerchat::Node node; std::wstring output,screenDiagnosticsOutput; std::map<std::string,peerchat::Contact> contacts; };
static std::wstring widen(const std::string& value){return {value.begin(),value.end()};}
static std::string identity(const wchar_t* value){std::wstring text=value?value:L"";std::string result;for(auto ch:text){if(ch>127)throw std::runtime_error("Invalid identity");result+=char(ch);}return result;}
static std::wstring quote(const std::wstring& value){
    std::wostringstream out;out<<L'"';
    for(auto ch:value){if(ch==L'"'||ch==L'\\')out<<L'\\'<<ch;else if(ch<32){const wchar_t* hex=L"0123456789abcdef";out<<L"\\u00"<<hex[(ch>>4)&15]<<hex[ch&15];}else out<<ch;}
    out<<L'"';return out.str();
}
UI_API void* ui_create(){try{return new UiBridge;}catch(...){return nullptr;}}
UI_API void ui_destroy(void* handle){delete static_cast<UiBridge*>(handle);}
// Profile maintenance deliberately has no Node/session ownership.  It opens two closed
// SQLite stores only while the managed bootstrap has verified the profile lock is free,
// and copies just the durable device identity into a brand-new current-schema store.
UI_API int ui_rebuild_profile_identity(const wchar_t* previous,const wchar_t* fresh){try{
    if(!previous||!*previous||!fresh||!*fresh)return 0;
    peerchat::Storage oldStore(previous),freshStore(fresh);
    freshStore.saveProfile(oldStore.profile());
    return 1;
}catch(...){return 0;}}
UI_API const wchar_t* ui_profile_peer_id(const wchar_t* directory){static thread_local std::wstring output;try{
    if(!directory||!*directory){output.clear();return output.c_str();}
    peerchat::Storage store(directory);output=widen(store.profile().peerId);return output.c_str();
}catch(...){output.clear();return output.c_str();}}
UI_API int ui_start(void* handle,const wchar_t* directory,unsigned short port,const wchar_t* name){try{static_cast<UiBridge*>(handle)->node.start(directory,port,name?name:L"",false);return 1;}catch(...){return 0;}}
UI_API int ui_connect(void* handle,const wchar_t* host,unsigned short port,const wchar_t* peer){try{static_cast<UiBridge*>(handle)->node.connect(host,port,identity(peer));return 1;}catch(...){return 0;}}
UI_API int ui_connect_ex(void* handle,const wchar_t* host,unsigned short port,const wchar_t* peer,const wchar_t* source,const wchar_t* route,int force){try{static_cast<UiBridge*>(handle)->node.connect(host,port,identity(peer),source?source:L"Direct",route?route:L"manual",force!=0);return 1;}catch(...){return 0;}}
UI_API int ui_invalidate_route(void* handle,const wchar_t* peer,const wchar_t* staleHost,unsigned short stalePort,int allRoutes){try{static_cast<UiBridge*>(handle)->node.invalidateRoute(identity(peer),staleHost?staleHost:L"",stalePort,allRoutes!=0);return 1;}catch(...){return 0;}}
UI_API int ui_invalidate_route_v2(void* handle,const wchar_t* peer,const wchar_t* staleHost,unsigned short stalePort,int allRoutes,unsigned long long expectedNetworkGeneration){try{static_cast<UiBridge*>(handle)->node.invalidateRoute(identity(peer),staleHost?staleHost:L"",stalePort,allRoutes!=0,expectedNetworkGeneration);return 1;}catch(...){return 0;}}
UI_API int ui_network_context(void* handle,unsigned long long generation,const wchar_t* fingerprint){try{static_cast<UiBridge*>(handle)->node.setNetworkContext(generation,fingerprint?fingerprint:L"");return 1;}catch(...){return 0;}}
// Credentials cross this presentation ABI only in memory.  No caller-visible
// output or diagnostic is produced from this operation.
UI_API int ui_media_ice_configuration(void* handle,const wchar_t* host,unsigned short port,const wchar_t* username,const wchar_t* password,long long expiry,unsigned long long profileSessionGeneration,unsigned long long discoveryGeneration,unsigned long long configurationGeneration){try{
    peerchat::MediaIceConfiguration configuration;configuration.policy=peerchat::MediaIceConfiguration::Policy::RelayOnly;
    configuration.host=identity(host);configuration.port=port;configuration.username=identity(username);configuration.password=identity(password);
    configuration.expiresUtc=expiry;configuration.profileSessionGeneration=profileSessionGeneration;configuration.discoveryGeneration=discoveryGeneration;configuration.generation=configurationGeneration;
    return static_cast<UiBridge*>(handle)->node.setMediaIceConfiguration(std::move(configuration))?1:0;
}catch(...){return 0;}}
UI_API int ui_candidates(void* handle,const wchar_t* peer,const wchar_t* lanHost,unsigned short lanPort,const wchar_t* publicHost,unsigned short publicPort,int relay,unsigned long long generation,int force){try{static_cast<UiBridge*>(handle)->node.updateCandidates(identity(peer),lanHost?lanHost:L"",lanPort,publicHost?publicHost:L"",publicPort,relay!=0,generation,force!=0);return 1;}catch(...){return 0;}}
UI_API int ui_prepare_scoped_p2p_listener(void* handle,const wchar_t* peer){try{return static_cast<UiBridge*>(handle)->node.prepareScopedP2pListener(identity(peer))?1:0;}catch(...){return 0;}}
UI_API int ui_begin_scoped_p2p(void* handle,const wchar_t* peer,const wchar_t* id,const wchar_t* host,unsigned short port,unsigned long long expiry,unsigned long long generation){try{return static_cast<UiBridge*>(handle)->node.beginScopedP2pProbe(identity(peer),identity(id),host?host:L"",port,expiry,generation)?1:0;}catch(...){return 0;}}
UI_API int ui_promote_scoped_p2p(void* handle,const wchar_t* peer,const wchar_t* id){try{return static_cast<UiBridge*>(handle)->node.promoteVerifiedP2P(identity(peer),identity(id))?1:0;}catch(...){return 0;}}
UI_API void ui_cancel_scoped_p2p(void* handle,const wchar_t* peer){try{static_cast<UiBridge*>(handle)->node.cancelScopedP2p(identity(peer));}catch(...){} }
UI_API int ui_authenticated_friends(void* handle,unsigned long long localAccount,unsigned long long generation,const wchar_t* encoded){try{
    std::vector<peerchat::AuthenticatedFriend> friends;std::wstring value=encoded?encoded:L"";std::wstringstream rows(value);std::wstring row;
    while(std::getline(rows,row,L'\n')){auto first=row.find(L'|'),second=first==std::wstring::npos?std::wstring::npos:row.find(L'|',first+1);if(first==std::wstring::npos||second==std::wstring::npos)continue;unsigned long long account=0;try{account=std::stoull(row.substr(0,first));}catch(...){continue;}peerchat::AuthenticatedFriend friendState;friendState.accountId=account;friendState.expectedPeerId=identity(row.substr(first+1,second-first-1).c_str());std::wstringstream peers(row.substr(second+1));std::wstring peer;while(std::getline(peers,peer,L','))if(!peer.empty())friendState.knownPeerIds.push_back(identity(peer.c_str()));friends.push_back(std::move(friendState));}
    static_cast<UiBridge*>(handle)->node.replaceAuthenticatedFriends(localAccount,generation,std::move(friends));return 1;
}catch(...){return 0;}}
UI_API int ui_pin_initial_authenticated_friend_identity(void* handle,const wchar_t* peer,unsigned long long attempt,unsigned long long generation){try{return static_cast<UiBridge*>(handle)->node.pinInitialAuthenticatedFriendIdentity(identity(peer),attempt,generation)?1:0;}catch(...){return 0;}}
UI_API int ui_relay_ready(void* handle,unsigned long long attempt,int incoming){try{return static_cast<UiBridge*>(handle)->node.relayReady(attempt,incoming!=0)?1:0;}catch(...){return 0;}}
UI_API int ui_relay_receive(void* handle,unsigned long long attempt,const unsigned char* bytes,unsigned length){try{return static_cast<UiBridge*>(handle)->node.relayReceive(attempt,bytes,length)?1:0;}catch(...){return 0;}}
UI_API void ui_relay_closed(void* handle,unsigned long long attempt,const wchar_t* reason){try{static_cast<UiBridge*>(handle)->node.relayClosed(attempt,reason?reason:L"");}catch(...){}}
UI_API int ui_relay_take_output(void* handle,unsigned long long attempt,unsigned char* bytes,unsigned capacity,unsigned waitMilliseconds){try{return static_cast<UiBridge*>(handle)->node.takeRelayOutput(attempt,bytes,capacity,waitMilliseconds);}catch(...){return -1;}}
UI_API int ui_approve(void* handle,const wchar_t* peer,int accepted){try{static_cast<UiBridge*>(handle)->node.approve(identity(peer),accepted!=0);return 1;}catch(...){return 0;}}
UI_API int ui_send(void* handle,const wchar_t* peer,const wchar_t* text){try{return !static_cast<UiBridge*>(handle)->node.send(identity(peer),text).empty();}catch(...){return 0;}}
UI_API int ui_remove(void* handle,const wchar_t* peer){try{auto& bridge=*static_cast<UiBridge*>(handle);std::string id=identity(peer);bridge.node.removeContact(id);bridge.contacts.erase(id);return 1;}catch(...){return 0;}}
UI_API int ui_history(void* handle,const wchar_t* peer,long long before){try{static_cast<UiBridge*>(handle)->node.loadHistory(identity(peer),before);return 1;}catch(...){return 0;}}
UI_API int ui_erase_history(void* handle,const wchar_t* peer){try{return static_cast<UiBridge*>(handle)->node.eraseHistory(identity(peer))?1:0;}catch(...){return 0;}}
UI_API int ui_group_account(void* handle,unsigned long long account,const wchar_t* name){try{static_cast<UiBridge*>(handle)->node.setGroupAccount(account,name?name:L"");return account&&name&&*name;}catch(...){return 0;}}
UI_API int ui_authenticated_group_roster(void* handle,unsigned long long account,unsigned long long discoveryGeneration,const wchar_t* group,const wchar_t* legacy,unsigned long long rosterGeneration,const wchar_t* distributor,const wchar_t* encodedMembers){try{
    if(!handle||!group||!legacy||!distributor||!encodedMembers)return 0;
    peerchat::group_keys::RosterSnapshot roster;roster.serverGroupId=identity(group);roster.legacyGroupId=identity(legacy);roster.distributorPeerId=identity(distributor);roster.generation=rosterGeneration;
    std::wstringstream rows(encodedMembers);std::wstring row;
    while(std::getline(rows,row,L'\n')){
        if(row.empty())continue;
        std::vector<std::wstring> fields;std::wstringstream columns(row);std::wstring field;while(std::getline(columns,field,L'|'))fields.push_back(field);
        if(fields.size()!=4)return 0;
        peerchat::group_keys::RosterDevice member;member.accountId=std::stoull(fields[0]);member.peerId=identity(fields[1].c_str());
        if(fields[2]==L"owner")member.role=peerchat::GroupRole::Owner;else if(fields[2]==L"admin")member.role=peerchat::GroupRole::Admin;else if(fields[2]==L"member")member.role=peerchat::GroupRole::Member;else return 0;
        if(fields[3]!=L"member"&&fields[3]!=L"invited")return 0;
        member.invited=fields[3]==L"invited";roster.devices.push_back(std::move(member));
    }
    return static_cast<UiBridge*>(handle)->node.setAuthenticatedGroupRoster(account,discoveryGeneration,std::move(roster))?1:0;
}catch(...){return 0;}}
UI_API void ui_clear_authenticated_group_rosters(void* handle){try{if(handle)static_cast<UiBridge*>(handle)->node.clearAuthenticatedGroupRosters();}catch(...){}}
UI_API int ui_begin_group_control(void* handle,const wchar_t* group,unsigned long long targetAccount,const wchar_t* targetPeer,unsigned long long discoveryGeneration){try{return handle&&group&&targetPeer&&static_cast<UiBridge*>(handle)->node.beginGroupControl(identity(group),targetAccount,identity(targetPeer),discoveryGeneration)?1:0;}catch(...){return 0;}}
UI_API const wchar_t* ui_group_create(void* handle,const wchar_t* name){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.output=widen(bridge.node.createGroup(name?name:L""));}catch(...){bridge.output.clear();}return bridge.output.c_str();}
UI_API const wchar_t* ui_group_create_proof(void* handle,const wchar_t* group){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.output=bridge.node.groupCreationProof(identity(group));}catch(...){bridge.output.clear();}return bridge.output.c_str();}
UI_API int ui_group_rename(void* handle,const wchar_t* group,const wchar_t* name){try{return static_cast<UiBridge*>(handle)->node.renameGroup(identity(group),name?name:L"");}catch(...){return 0;}}
UI_API int ui_group_invite(void* handle,const wchar_t* group,unsigned long long account,const wchar_t* peer,const wchar_t* name){try{return static_cast<UiBridge*>(handle)->node.inviteToGroup(identity(group),account,identity(peer),name?name:L"");}catch(...){return 0;}}
UI_API int ui_group_respond(void* handle,const wchar_t* group,int accepted){try{return static_cast<UiBridge*>(handle)->node.respondToGroupInvite(identity(group),accepted!=0);}catch(...){return 0;}}
UI_API int ui_group_leave(void* handle,const wchar_t* group){try{return static_cast<UiBridge*>(handle)->node.leaveGroup(identity(group));}catch(...){return 0;}}
UI_API int ui_group_remove_member(void* handle,const wchar_t* group,unsigned long long account){try{return static_cast<UiBridge*>(handle)->node.removeGroupMember(identity(group),account);}catch(...){return 0;}}
UI_API int ui_group_role(void* handle,const wchar_t* group,unsigned long long account,int admin){try{return static_cast<UiBridge*>(handle)->node.setGroupAdmin(identity(group),account,admin!=0);}catch(...){return 0;}}
UI_API int ui_group_send(void* handle,const wchar_t* group,const wchar_t* text){try{return static_cast<UiBridge*>(handle)->node.sendGroup(identity(group),text?text:L"");}catch(...){return 0;}}
UI_API int ui_groups(void* handle){try{static_cast<UiBridge*>(handle)->node.loadGroups();return 1;}catch(...){return 0;}}
UI_API int ui_group_history(void* handle,const wchar_t* group,long long before){try{static_cast<UiBridge*>(handle)->node.loadGroupHistory(identity(group),before);return 1;}catch(...){return 0;}}
UI_API const wchar_t* ui_voice_call(void* handle,const wchar_t* peer){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.output=widen(bridge.node.startVoiceCall(identity(peer)));}catch(...){bridge.output.clear();}return bridge.output.c_str();}
UI_API const wchar_t* ui_screen_start(void* handle,const wchar_t* peer,unsigned sourceKind,unsigned long long sourceHandle,unsigned fps,unsigned bitrate,int cursor,unsigned backend){auto& bridge=*static_cast<UiBridge*>(handle);try{if(backend>1)return L"";bridge.output=widen(bridge.node.startScreenShare(identity(peer),sourceKind,sourceHandle,fps,bitrate,cursor!=0,static_cast<peerchat::screen::Backend>(backend)));}catch(...){bridge.output.clear();}return bridge.output.c_str();}
UI_API int ui_screen_backend(void* handle,unsigned backend){try{return backend<=1&&static_cast<UiBridge*>(handle)->node.setScreenShareBackend(static_cast<peerchat::screen::Backend>(backend));}catch(...){return 0;}}
UI_API int ui_screen_answer(void* handle,const wchar_t* id,unsigned long long viewer,unsigned backend){try{return backend<=1&&static_cast<UiBridge*>(handle)->node.answerScreenShare(identity(id),viewer,static_cast<peerchat::screen::Backend>(backend));}catch(...){return 0;}}
UI_API int ui_screen_decline(void* handle,const wchar_t* id){try{return static_cast<UiBridge*>(handle)->node.declineScreenShare(identity(id));}catch(...){return 0;}}
UI_API int ui_screen_end(void* handle,const wchar_t* id){try{return static_cast<UiBridge*>(handle)->node.endScreenShare(identity(id));}catch(...){return 0;}}
UI_API const wchar_t* ui_screen_diagnostics(void* handle){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.screenDiagnosticsOutput=bridge.node.screenShareDiagnostics();}catch(...){bridge.screenDiagnosticsOutput=L"screen_media_state=unavailable";}return bridge.screenDiagnosticsOutput.c_str();}
UI_API int ui_voice_answer(void* handle,const wchar_t* call){try{return static_cast<UiBridge*>(handle)->node.answerVoiceCall(identity(call));}catch(...){return 0;}}
UI_API int ui_voice_decline(void* handle,const wchar_t* call){try{return static_cast<UiBridge*>(handle)->node.declineVoiceCall(identity(call));}catch(...){return 0;}}
UI_API int ui_voice_end(void* handle,const wchar_t* call){try{return static_cast<UiBridge*>(handle)->node.endVoiceCall(identity(call));}catch(...){return 0;}}
UI_API int ui_voice_mute(void* handle,int muted){try{return static_cast<UiBridge*>(handle)->node.setVoiceMuted(muted!=0);}catch(...){return 0;}}
UI_API int ui_voice_deafen(void* handle,int deafened){try{return static_cast<UiBridge*>(handle)->node.setVoiceDeafened(deafened!=0);}catch(...){return 0;}}
UI_API int ui_voice_devices_select(void* handle,int input,int output){try{return static_cast<UiBridge*>(handle)->node.setVoiceDevices(input,output);}catch(...){return 0;}}
UI_API int ui_voice_levels(void* handle,int input,int output){try{return static_cast<UiBridge*>(handle)->node.setVoiceVolumes(input,output);}catch(...){return 0;}}
UI_API int ui_voice_processing(void* handle,int enabled,int noiseSuppression,int automaticGain){try{return static_cast<UiBridge*>(handle)->node.setVoiceProcessing(enabled!=0,noiseSuppression!=0,automaticGain!=0);}catch(...){return 0;}}
UI_API const wchar_t* ui_voice_diagnostics(void* handle){auto& bridge=*static_cast<UiBridge*>(handle);try{bridge.output=bridge.node.voiceDiagnostics();}catch(...){bridge.output=L"media_state=unavailable\nlatest_audio_error=could not read native diagnostics";}return bridge.output.c_str();}
UI_API int ui_voice_telemetry(void* handle,float* input,float* output,unsigned* flags){try{if(!handle||!input||!output||!flags)return 0;auto value=static_cast<UiBridge*>(handle)->node.voiceTelemetry();*input=value.inputLevel;*output=value.outputLevel;*flags=(value.active?1u:0u)|(value.inputAvailable?2u:0u)|(value.outputAvailable?4u:0u);return 1;}catch(...){return 0;}}
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
        out<<L"{\"kind\":"<<int(event.kind)<<L",\"peer\":"<<quote(widen(event.peerId))<<L",\"expected\":"<<quote(widen(event.expectedPeerId))<<L",\"text\":"<<quote(event.text)<<L",\"detail\":"<<quote(event.detail)<<L",\"candidate\":"<<quote(event.candidate)<<L",\"source\":"<<quote(event.candidateSource)<<L",\"route\":"<<quote(event.route)<<L",\"attempt\":"<<event.attemptId<<L",\"retryMs\":"<<event.retryDelayMilliseconds<<L",\"sessionEpoch\":"<<event.sessionEpoch<<L",\"sessionRevision\":"<<event.sessionRevision<<L",\"networkGeneration\":"<<event.networkGeneration<<L",\"connected\":"<<(event.sessionConnected?L"true":L"false")<<L",\"reliableRoute\":"<<event.reliableRoute<<L",\"p2pAvailability\":"<<event.p2pAvailability<<L",\"callLifetime\":"<<event.callLifetime<<L",\"shareLifetime\":"<<event.shareLifetime<<L",\"shareGeneration\":"<<event.shareGeneration<<L",\"shareState\":"<<event.shareState<<L",\"screenRoute\":"<<event.screenRoute<<L",\"screenWidth\":"<<event.screenWidth<<L",\"screenHeight\":"<<event.screenHeight<<L",\"screenFps\":"<<event.screenFps<<L",\"screenBitrateMbps\":"<<event.screenBitrateMbps<<L",\"screenSummary\":"<<quote(event.screenSummary)<<L",\"screenDetails\":"<<quote(event.screenDetails)<<L",\"screenFailureDomain\":"<<quote(event.screenFailureDomain)<<L",\"screenFailureReason\":"<<quote(event.screenFailureReason)<<L",\"screenRecoveryTrigger\":"<<quote(event.screenRecoveryTrigger)<<L",\"screenRecoveryGenerationFrom\":"<<event.screenRecoveryGenerationFrom<<L",\"screenRecoveryGenerationTo\":"<<event.screenRecoveryGenerationTo<<L",\"callState\":"<<event.callState<<L",\"callType\":"<<event.callType<<L",\"mediaState\":"<<event.mediaState<<L",\"mediaGeneration\":"<<event.mediaGeneration<<L",\"mediaRoute\":"<<event.mediaRoute<<L",\"callControlAvailable\":"<<(event.callControlAvailable?L"true":L"false")<<L",\"screenMediaFailed\":"<<(event.screenMediaFailed?L"true":L"false")<<L",\"callMuted\":"<<(event.callMuted?L"true":L"false")<<L",\"callDeafened\":"<<(event.callDeafened?L"true":L"false")<<L",\"port\":"<<event.port<<L",\"contacts\":[";
        bool firstContact=true;for(const auto& contact:event.contacts){if(!firstContact)out<<L',';firstContact=false;
            out<<L"{\"peer\":"<<quote(widen(contact.peerId))<<L",\"name\":"<<quote(contact.name)<<L",\"verified\":"<<(contact.verified?L"true":L"false")<<L",\"host\":"<<quote(contact.endpoints.empty()?L"":contact.endpoints.front().host)<<L",\"port\":"<<(contact.endpoints.empty()?0:contact.endpoints.front().port)<<L'}';}
        out<<L"],\"messages\":[";bool firstMessage=true;for(const auto& message:event.messages){if(!firstMessage)out<<L',';firstMessage=false;
            out<<L"{\"id\":"<<quote(widen(message.id))<<L",\"sender\":"<<quote(widen(message.sender))<<L",\"text\":"<<quote(message.text)<<L",\"time\":"<<message.utcMilliseconds<<L",\"logical\":"<<message.logicalOrder<<L",\"delivery\":"<<int(message.delivery)<<L",\"sequence\":"<<message.sequence<<L'}';}
        out<<L"],\"groups\":[";bool firstGroup=true;for(const auto& group:event.groups){if(!firstGroup)out<<L',';firstGroup=false;
            out<<L"{\"id\":"<<quote(widen(group.groupId))<<L",\"name\":"<<quote(group.name)<<L",\"owner\":"<<group.ownerAccountId<<L",\"members\":[";bool firstMember=true;for(const auto& member:group.members){if(!firstMember)out<<L',';firstMember=false;out<<L"{\"account\":"<<member.first<<L",\"peer\":"<<quote(widen(member.second.peerId))<<L",\"name\":"<<quote(member.second.name)<<L",\"role\":"<<int(member.second.role)<<L'}';}out<<L"],\"invites\":[";bool firstInvite=true;for(const auto& invitation:group.invitations){if(!firstInvite)out<<L',';firstInvite=false;out<<L"{\"account\":"<<invitation.first<<L",\"peer\":"<<quote(widen(invitation.second.peerId))<<L",\"name\":"<<quote(invitation.second.name)<<L",\"role\":1}";}out<<L"]}";
        }
        out<<L"],\"groupEvents\":[";bool firstGroupEvent=true;for(const auto& groupEvent:event.groupEvents){if(!firstGroupEvent)out<<L',';firstGroupEvent=false;out<<L"{\"id\":"<<quote(widen(groupEvent.eventId))<<L",\"group\":"<<quote(widen(groupEvent.groupId))<<L",\"account\":"<<groupEvent.actorAccountId<<L",\"sender\":"<<quote(widen(groupEvent.actorPeerId))<<L",\"text\":"<<quote(groupEvent.message)<<L",\"time\":"<<groupEvent.createdMilliseconds<<L",\"logical\":"<<groupEvent.logicalOrder<<L'}';}
        out<<L"],\"authorizationGroupId\":"<<quote(widen(event.authorizationGroupId))<<L",\"authorizationRosterGeneration\":"<<event.authorizationRosterGeneration<<L",\"authorizationTargetAccountId\":"<<event.authorizationTargetAccountId<<L'}';
    }out<<L']';bridge.output=out.str();
}catch(...){bridge.output=L"[{\"kind\":8,\"text\":\"Could not read core events.\"}]";}return bridge.output.c_str();}
