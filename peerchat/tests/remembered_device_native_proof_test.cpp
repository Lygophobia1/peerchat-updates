#include "../windows_mailbox.hpp"
#include <filesystem>
#include <iostream>

using namespace peerchat;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static const char* stage="setup";
template<class F> static void rejected(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,"out-of-scope device proof was signed");}
static Blob request(std::uint64_t issued,const std::string& action,std::uint64_t account,
                    const std::string& device,const std::string& challenge,const std::string& binding,bool badInput=false){
    auto text="PeerChat remembered device v1|"+action+"|"+std::to_string(account)+"|"+device+"|"+challenge+"|"+binding;
    if(badInput)text+="|extra";
    protocol::Writer wire(protocol::FileTransfer);wire.string("PeerChat/RememberedDeviceProof/v1");wire.number(issued);
    wire.string(action);wire.number(account);wire.string(device);wire.string(challenge);wire.string(binding);
    wire.bytes(Blob(text.begin(),text.end()));return wire.data;
}
int main(int argc,char** argv){try{
    check(argc==2,"fixture directory required");auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
    Storage storage((root/L"profile").wstring());auto profile=storage.profile();
    mailbox::WindowsMailbox adapter((root/L"native-adapter").wstring(),profile);
    const auto issued=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const auto account=std::uint64_t(71);const auto challenge=std::string(64,'a'),binding=std::string(64,'b');
    auto enroll=request(issued,"enroll",account,profile.peerId,challenge,binding);
    check(enroll.size()>512&&enroll.size()<=1024,"structured proof bound differs from Node op9");
    stage="reject unauthenticated";rejected([&]{adapter.rememberedDeviceProof(enroll,account,0);});
    rejected([&]{adapter.rememberedDeviceProof(enroll,account+1,account);});
    rejected([&]{adapter.rememberedDeviceProof(enroll,account,account+1);});
    rejected([&]{adapter.rememberedDeviceProof(request(issued,"enroll",account,std::string(64,'c'),challenge,binding),account,account);});
    rejected([&]{adapter.rememberedDeviceProof(request(issued,"revoke",account,profile.peerId,challenge,binding),account,account);});
    rejected([&]{adapter.rememberedDeviceProof(request(issued,"enroll",account,profile.peerId,challenge,binding,true),account,account);});
    rejected([&]{adapter.rememberedDeviceProof(request(issued-61000,"enroll",account,profile.peerId,challenge,binding),account,account);});
    rejected([&]{adapter.rememberedDeviceProof(request(issued+61000,"enroll",account,profile.peerId,challenge,binding),account,account);});
    stage="valid enroll";
    check(Storage::publicKeyId(profile.publicKey)==profile.peerId,"profile identity mismatch");
    {protocol::Reader inspect(enroll);check(inspect.type()==protocol::FileTransfer,"request type");check(inspect.string()=="PeerChat/RememberedDeviceProof/v1","request domain");check(inspect.number()==issued,"request issuance");check(inspect.string()=="enroll","request action");check(inspect.number()==account,"request account");check(inspect.string()==profile.peerId,"request device");check(inspect.string()==challenge,"request challenge");check(inspect.string()==binding,"request digest");auto raw=inspect.bytes();check(!raw.empty(),"request input");inspect.end();}
    auto result=adapter.rememberedDeviceProof(enroll,account,account);
    protocol::Reader response(result);check(response.type()==protocol::Accepted,"proof ABI type");
    auto key=response.bytes(),signature=response.bytes();response.end();
    check(key==profile.publicKey&&key.size()==72&&signature.size()==64,"proof ABI bytes");
    protocol::Reader parsed(enroll);parsed.string();parsed.number();parsed.string();parsed.number();parsed.string();parsed.string();parsed.string();auto original=parsed.bytes();parsed.end();
    stage="verify signature";check(verifyDevicePayload(original,signature,key),"server-compatible original-message signature");
    rejected([&]{adapter.rememberedDeviceProof(enroll,account,account);});
    auto resume=request(issued,"resume",account,profile.peerId,std::string(64,'d'),binding);
    stage="valid resume";check(!adapter.rememberedDeviceProof(resume,account,account).empty(),"scoped resume proof unavailable");
    stage="mailbox dispatch";
    storage.saveGroupAccount({account,L"Fixture account"});
    delivery::DeliveryManager deliveries(storage,profile.peerId);
    session::SessionTransportManager sessions(profile.peerId,profile.peerId,{1});
    reliable::AuthenticatedFriendSnapshot friends;
    auto dispatched=request(issued,"resume",account,profile.peerId,std::string(64,'e'),binding);
    rejected([&]{adapter.execute(9,dispatched,"",false,storage,deliveries,friends,sessions);});
    friends.replace(account,1,{});
    rejected([&]{adapter.execute(9,dispatched,"message-id",false,storage,deliveries,friends,sessions);});
    rejected([&]{adapter.execute(9,dispatched,"",true,storage,deliveries,friends,sessions);});
    auto dispatchedProof=adapter.execute(9,dispatched,"",false,storage,deliveries,friends,sessions);
    protocol::Reader accepted(dispatchedProof);check(accepted.type()==protocol::Accepted&&accepted.bytes()==profile.publicKey&&accepted.bytes().size()==64,"mailbox op9 output");accepted.end();
    rejected([&]{adapter.execute(9,dispatched,"",false,storage,deliveries,friends,sessions);});
    std::cout<<"PASS scoped native remembered-device enroll/resume proof, account/device/purpose/freshness/replay/ABI\n";
    return 0;
}catch(const std::exception& error){std::cerr<<stage<<": "<<error.what()<<'\n';return 1;}}
