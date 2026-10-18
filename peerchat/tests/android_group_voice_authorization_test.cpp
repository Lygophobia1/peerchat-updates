// Actual AndroidGroupVoiceApplication authorization fence with a native
// authenticated GroupApplication roster. HOST ONLY synthetic audio device.
#include "portable/android_group_voice_application.hpp"
#include "android_voice_media_integration_audio.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace peerchat;
namespace device=voice_integration_audio;
static void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: executable profileDirectory genuineReceiveDll");
    Storage store(std::filesystem::path(argv[1]).wstring());auto profile=store.profile();
    delivery::DeliveryManager deliveries(store,profile.peerId);
    portable::GroupApplication groups(store,profile,deliveries,71,true);
    group_keys::RosterSnapshot roster;roster.serverGroupId=std::string(32,'a');roster.legacyGroupId=std::string(32,'b');
    roster.distributorPeerId=profile.peerId;roster.generation=3;
    const auto remote=std::string(64,'f');
    roster.devices={{71,profile.peerId,GroupRole::Owner,false},{23,remote,GroupRole::Member,false}};
    groups.authenticated(4,{roster});
    std::wstring receive;for(const char* p=argv[2];*p;++p)receive.push_back(static_cast<unsigned char>(*p));
    device::OwnerScope owner(profile.peerId);
    portable::AndroidGroupVoiceApplication app(receive);
    VoiceRoomBinding binding{roster.serverGroupId,std::string(32,'c'),std::string(32,'d'),profile.peerId,
        std::string(32,'e'),71,1,3,1};
    MediaIceConfiguration ice;ice.host="turn.invalid";ice.port=3478;ice.username="fixture";ice.password="fixture";
    ice.expiresUtc=std::time(nullptr)+3600;ice.profileSessionGeneration=4;ice.discoveryGeneration=4;ice.generation=1;
    const auto member=AuthorizedVoiceSource{23,remote,std::string(32,'1')};
    check(!app.start(binding,4,&groups,ice,{{24,remote,member.participation}}),
        "Mismatched remote account/device mapping admitted");
    check(!app.start(binding,4,&groups,ice,{{23,std::string(64,'9'),member.participation}}),
        "Unlisted remote device admitted");
    check(!app.start(binding,4,&groups,ice,{member,{23,remote,std::string(32,'2')}}),
        "Duplicate remote device with alternate participation admitted");
    auto early=device::snapshots();
    check(!app.active()&&std::none_of(early.begin(),early.end(),
        [](const auto& value){return value.active||value.starts>0;}),"Rejected roster scope started an audio device");
    // The private wrapper uses the same static/dynamic receive engine path as
    // the direct host integration. Start alone opens no network route.
    check(app.start(binding,4,&groups,ice,{}),"Authorized one-person room did not start");
    check(app.active()&&app.current(&groups,71,4,profile.peerId),"Current roster was not retained");
    check(!app.reconcile(binding,2,3,{{24,remote,member.participation}},&groups,4),
        "Reconcile admitted an unlisted account");
    check(app.reconcile(binding,2,3,{member},&groups,4),"Authorized remote source was rejected");
    groups.invalidate();check(!app.current(&groups,71,4,profile.peerId)&&!app.active(),
        "Group revocation retained capture or authorized sources");
    check(!app.stop(binding),"Retired room lifetime accepted stale stop");
    std::cout<<"PASS: native group voice application rejects mismatched account/device, duplicate participation, unauthorized reconcile and revokes media on group invalidation. HOST ONLY.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
