// Isolated real Android OS ↔ Windows cache fixture. The Android process uses
// production JNI/POSIX/Keystore; this socket carries only test control and
// opaque native records via ADB reverse. No test-only trust hook is linked.
#define main groupAttachmentFixtureMain
#include "windows_group_attachment_application_test.cpp"
#undef main
#include <winsock.h>
#include "windows_dm_attachment_adapter.hpp"
#include "reliable_transfer_interop_fixture.hpp"
#include "../device_identity_application.hpp"
#include <chrono>

using namespace peerchat;
namespace ri=peerchat::reliable;
namespace dm=peerchat::dm_attachments;
static void need(bool value,const std::string& reason){need(value,reason.c_str());}
static std::uint64_t utcMilliseconds(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
static Blob identityCommand(unsigned op,unsigned account,const std::string& pin,const std::function<void(protocol::Writer&)>& fields={},std::uint64_t started=0){
 protocol::Writer writer(protocol::FileTransfer);writer.string("PeerChat/IdentityDirectoryCommand/v1");writer.number(op);writer.number(account);writer.number(1);writer.string(pin);writer.number(started?started:utcMilliseconds());if(fields)fields(writer);return writer.data;
}

struct DmPeer {
 ApplicationPeer crypto;unsigned account;std::string pin;std::vector<DmPeer*>* trusted=nullptr;
 std::shared_ptr<platform::SqlDatabase> directoryDatabase;std::unique_ptr<device_identity::Application> directory;
 std::unique_ptr<dm::windows::Adapter> io;std::unique_ptr<io::DurableJournal> ordinary;
 std::unique_ptr<cache::CacheStateStore> groupJournal;std::unique_ptr<transfer::DirectCacheJournal> journal;
 std::unique_ptr<transfer::TransferManager> manager;std::unique_ptr<dm::Application> app;
 DmPeer(const std::filesystem::path& root,unsigned value):crypto(root,value),account(value){}
 dm::Evidence evidence(){
  dm::Evidence e;
  e.identity=[this](std::uint64_t owner,const std::string& device){need(bool(directory),"Native identity directory missing");return directory->resolve(account,1,owner,device,utcMilliseconds(),GetTickCount64());};
  e.current=[this,identity=e.identity](const mailbox::RecipientBinding& binding,const std::string& relationship,std::uint64_t other){
   if(!protocol::id(relationship,32)||other==binding.accountId||!binding.storageAllowed)return false;
   for(auto* peer:*trusted)if(peer->account==binding.accountId&&peer->crypto.profile.peerId==binding.device&&mailbox::serializeBinding(peer->crypto.binding)==mailbox::serializeBinding(binding))return mailbox::verifyBinding(binding,binding.accountId,binding.device,identity(binding.accountId,binding.device),verifyDevicePayload,Storage::publicKeyId);
   return false;
  };return e;
 }
 void activate(const std::string& server,std::vector<DmPeer*>& peers){
  pin=server;trusted=&peers;directoryDatabase=platform::openWindowsSqlDatabase((crypto.root/L"identity-directory").wstring());directory=std::make_unique<device_identity::Application>(crypto.identity,*directoryDatabase,crypto.protector,crypto.profile.peerId);io=std::make_unique<dm::windows::Adapter>(crypto.root.wstring());ordinary=std::make_unique<io::DurableJournal>(crypto.root.wstring(),crypto.profile.peerId);
  groupJournal=std::make_unique<cache::CacheStateStore>(crypto.root.wstring(),crypto.profile.peerId,*ordinary,[](const transfer::DurableState&)->std::unique_ptr<transfer::Source>{throw std::runtime_error("Unexpected group journal record");});groupJournal->load();
  journal=std::make_unique<transfer::DirectCacheJournal>(crypto.storage,crypto.profile.peerId,*groupJournal,[this](const auto& row){return app->reopen(row);},[this](const auto& row){return io->receive(row);});
  manager=std::make_unique<transfer::TransferManager>([]{return std::make_unique<io::Sha256>();},journal.get());
  app=std::make_unique<dm::Application>(crypto.storage,crypto.profile,*manager,io->platform(),[this]{return evidence();},[this](const std::string& id,const auto& operation){
   auto keys=crypto.manager.snapshot();auto found=std::find_if(keys.begin(),keys.end(),[&](const auto& key){return id.empty()?key.state==DeviceKeyState::Active:key.id==id;});need(found!=keys.end(),"Windows registered recipient key unavailable");
   auto binding=mailbox::parseBinding(found->signedBinding);crypto.manager.withRecipient(found->id,!id.empty(),[&](const auto&,const auto& handle){operation(*crypto.hpke,crypto.identity,binding,handle);});
  });manager->restore(GetTickCount64());app->session(account,1,pin);
 }
 void step(Host& host,unsigned actor){app->tick();io->tick();manager->tickScoped(GetTickCount64(),[this](const auto& row){return app->route(row);},[this](const auto& row,const auto& frame){return app->send(row,frame);});auto command=app->nextCommand();if(command.empty())return;
  host.write("{\"operation\":\"native_command\",\"actor\":"+std::to_string(actor)+",\"command\":\""+rpc::base64(command,false)+"\"}");app->reply(host.read());
 }
 dm::Application::Snapshot snapshot(const std::string& id){for(const auto& row:app->snapshots())if(row.id==id)return row;throw std::runtime_error("Windows DM snapshot missing");}
 Blob identity(const Blob& command){auto mapped=[this](const std::string& id)->std::uint64_t{for(auto* peer:*trusted)if(peer->crypto.profile.peerId==id)return peer->account;return 0;};return directory->execute(command,account,1,utcMilliseconds(),GetTickCount64(),{},mapped);}
};

struct DeviceSocket {
 SOCKET listener=INVALID_SOCKET,client=INVALID_SOCKET;
 explicit DeviceSocket(unsigned port){
  WSADATA startup{};need(WSAStartup(MAKEWORD(2,2),&startup)==0,"ADB fixture WSA startup");
  listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);need(listener!=INVALID_SOCKET,"ADB fixture socket");
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(static_cast<u_short>(port));
  need(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,1)==0,"ADB fixture listener");
  std::cout<<"LISTENING "<<port<<std::endl;
  client=accept(listener,nullptr,nullptr);need(client!=INVALID_SOCKET,"ADB fixture accept");
  DWORD timeout=45000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
 }
 ~DeviceSocket(){if(client!=INVALID_SOCKET)closesocket(client);if(listener!=INVALID_SOCKET)closesocket(listener);WSACleanup();}
 void line(const std::string& value){need(value.size()<=150000,"ADB fixture output bound");auto payload=value+"\n";size_t at=0;while(at<payload.size()){auto sent=send(client,payload.data()+at,static_cast<int>(payload.size()-at),0);need(sent>0,"ADB fixture write");at+=static_cast<size_t>(sent);}}
 std::string line(){std::string value;for(;;){char c=0;need(recv(client,&c,1,0)==1,"ADB fixture read");if(c=='\n')break;if(c!='\r')value+=c;need(value.size()<=150000,"ADB fixture input bound");}return value;}
 Blob read(){return rpc::unbase64(line(),100000,false);}
 Blob call(const std::string& command){line(command);return read();}
};

struct WindowsPinSide {
 Storage store;Profile profile;std::string remote;std::unique_ptr<TlsCredentials> credentials;
 session::SessionTransportManager sessions;delivery::DeliveryManager deliveries;
 ri::AuthenticatedFriendSnapshot friends;std::unique_ptr<portable::ReliableEndpoint> endpoint;
 std::uint64_t now=1;
 WindowsPinSide(const std::filesystem::path& path,const std::string& android,unsigned account,bool incoming)
  :store(path.wstring()),profile(store.profile()),remote(android),credentials(std::make_unique<TlsCredentials>(profile)),
   sessions(profile.peerId,profile.peerId,session::SessionEpoch{1}),deliveries(store,profile.peerId){
  friends.replace(account,1,{{1,android,{android}}});
  auto route=session::RouteObservation{{1},"relay:emulator-dm-fixture","authenticated fixture","opaque bytes",incoming,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};
  auto tls=std::make_unique<TlsSession>(*credentials,incoming);
  endpoint=std::make_unique<portable::ReliableEndpoint>(profile,store,sessions,deliveries,friends,session::SessionEpoch{1},session::TransportInstanceId{1},std::move(tls),remote,route,now,std::nullopt,
    [](const std::string&,const Blob&,std::uint64_t){throw std::runtime_error("Unexpected direct frame during cache pinning");});
 }
 Blob exchange(const Blob& input){
  endpoint->receive(input,++now);
  while(auto event=endpoint->poll())if(event->kind==portable::ReliableEndpoint::EventKind::TrustRequired)
   need(endpoint->initialPin(1,++now),"Windows first-device TLS pin rejected");
  return endpoint->takeOutput();
 }
 bool ready()const{return endpoint->ready();}
};

static Blob fixtureRecord(const std::string& domain,const std::vector<Blob>& values){protocol::Writer writer(protocol::FileTransfer);writer.string(domain);for(const auto& value:values)writer.bytes(value);return writer.data;}
static bool emulatorStep(DeviceSocket& device,Host& host){
 auto command=device.call("C");if(command==Blob{0}||command.empty())return false;
 protocol::Reader reader(command);need(reader.type()==protocol::FileTransfer&&reader.string()=="PeerChat/DMCacheCommand/v1","Android native cache command");
 reader.number();reader.string();reader.string();
 host.write("{\"operation\":\"native_command\",\"actor\":1,\"command\":\""+rpc::base64(command,false)+"\"}");
 auto reply=host.read();need(device.call("R|"+rpc::base64(reply,false))==Blob{1},"Android native cache reply rejected");return true;
}
struct AndroidRow {std::string id,name,path,error;std::uint64_t state=0,done=0,size=0;};
static AndroidRow emulatorRow(DeviceSocket& device,const std::string& id){
 auto wire=device.call("V");
 protocol::Reader reader(wire);need(reader.type()==protocol::FileTransfer,"Android transfer snapshot");auto count=reader.number();need(count<=64,"Android snapshot bound");
 for(auto n=count;n--;){AndroidRow row;row.id=reader.string();reader.string();row.name=reader.string();row.size=reader.number();row.state=reader.number();reader.number();row.done=reader.number();reader.number();row.error=reader.string();row.path=reader.string();if(row.id==id)return row;}
 throw std::runtime_error("Android DM snapshot missing");
}
static V managed(Host& host,const std::string& operation,unsigned actor,const Blob& receipt={}){
 V request=V::map();request["operation"]=V(operation);request["actor"]=V(std::uint64_t(actor));if(!receipt.empty())request["receipt"]=V(rpc::base64(receipt,false));host.write(rpc::json(request));return host.decode(host.read());
}
static V identityRpc(Host& host,unsigned actor,const std::string& operation,const std::string& field="",const std::string& value=""){
 V request=V::map();request["operation"]=V("identity");request["actor"]=V(std::uint64_t(actor));request["type"]=V(operation);if(!field.empty())request[field]=V(value);
 host.write(rpc::json(request));auto response=host.decode(host.read());need(response.at("ok").flag(),"Pinned identity RPC rejected "+operation);return response;
}
static Blob identityNative(DeviceSocket& device,DmPeer& first,DmPeer& second,unsigned actor,const Blob& command){
 if(actor==1){Sleep(600);return device.call("I|"+rpc::base64(command,false));}return (actor==2?first:second).identity(command);
}
static void registerIdentity(DeviceSocket& device,Host& host,DmPeer& first,DmPeer& second,unsigned actor,unsigned account,const std::string& pin){
 auto proof=identityNative(device,first,second,actor,identityCommand(0,account,pin));need(proof.size()>100,"Native signed possession missing for actor "+std::to_string(actor)+" (reply bytes "+std::to_string(proof.size())+")");identityRpc(host,actor,"device_identity_register","proof",rpc::base64(proof,false));
}
static void selectIdentityAuthority(DeviceSocket& device,Host& host,DmPeer& first,DmPeer& second,unsigned actor,unsigned account,const std::string& pin,const std::string& fingerprint){
 auto challenge=identityRpc(host,actor,"device_identity_authority_challenge");auto payload=rpc::unbase64(challenge.string("payload"),2048,false);
 auto proof=identityNative(device,first,second,actor,identityCommand(1,account,pin,[&](auto& w){w.bytes(payload);w.string(fingerprint);}));need(proof.size()>100,"Native authority signature missing");identityRpc(host,actor,"device_identity_authority_commit","proof",rpc::base64(proof,false));
}
static void endorseIdentity(DeviceSocket& device,Host& host,DmPeer& first,DmPeer& second,unsigned actor,unsigned account,const std::string& pin,const std::string& target){
 auto challenge=identityRpc(host,actor,"device_identity_endorse_challenge","targetDevice",target);auto payload=rpc::unbase64(challenge.string("payload"),2048,false);
 auto proof=identityNative(device,first,second,actor,identityCommand(2,account,pin,[&](auto& w){w.bytes(payload);w.string(target);}));need(proof.size()>100,"Native endorsement signature missing");identityRpc(host,actor,"device_identity_endorse_commit","proof",rpc::base64(proof,false));
}
static void applyIdentity(DeviceSocket& device,Host& host,DmPeer& first,DmPeer& second,unsigned actor,unsigned local,unsigned remote,const std::string& pin,const std::string& confirmed){
 auto started=utcMilliseconds();auto snapshot=identityRpc(host,actor,"device_identity_snapshot","accountId",std::to_string(remote));auto authority=rpc::unbase64(snapshot.string("authorityProof"),2048,false);need(!authority.empty()&&snapshot.at("authorityActive").flag(),"Fixture authority inactive");
 auto endorsements=snapshot.at("endorsements").array;auto command=identityCommand(3,local,pin,[&](auto& w){w.number(remote);w.number(snapshot.integer("revision"));w.bytes(authority);w.number(1);w.number(endorsements.size());for(const auto& e:endorsements){w.bytes(rpc::unbase64(e.string("proof"),2048,false));w.number(e.at("active").flag()?1:0);}w.string(confirmed);},started);
 need(identityNative(device,first,second,actor,command)==Blob{1},"Native identity snapshot/explicit root confirmation rejected");
}

int main(int argc,char** argv){try{
 need(argc==4,"Usage android-emulator-dm root managed-host.dll bridge-port");
 auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 auto port=std::stoul(argv[3]);need(port>=1024&&port<=65535,"ADB bridge port bound");
 DeviceSocket device(static_cast<unsigned>(port));
 auto androidPublic=device.read();need(androidPublic.size()==72,"Android Keystore identity size");
 auto androidId=Storage::publicKeyId(androidPublic);
 DmPeer sourceDirectory(root/L"android-public-directory",1),first(root/L"windows-one",2),second(root/L"windows-two",2);
 need(first.crypto.profile.peerId!=second.crypto.profile.peerId,"Distinct Windows recipients");
 device.line(rpc::base64(fixtureRecord("PeerChat/EmulatorDmSetup/v1",{first.crypto.profile.publicKey,second.crypto.profile.publicKey}),false));
 auto androidBinding=device.read();auto parsed=mailbox::parseBinding(androidBinding);
 need(parsed.accountId==1&&parsed.device==androidId&&mailbox::verifyBinding(parsed,1,androidId,androidPublic,verifyDevicePayload,Storage::publicKeyId),"Android signed binding");
 sourceDirectory.crypto.profile.peerId=androidId;sourceDirectory.crypto.profile.publicKey=androidPublic;sourceDirectory.crypto.binding=parsed;
 std::vector<DmPeer*> peers{&sourceDirectory,&first,&second};
 V setup=V::map();setup["publics"]=V::list();setup["bindings"]=V::list();for(auto* peer:peers){setup["publics"].array.push_back(V(rpc::base64(peer->crypto.profile.publicKey,false)));setup["bindings"].array.push_back(V(rpc::base64(mailbox::serializeBinding(peer->crypto.binding),false)));}
 auto json=rpc::json(setup);writeFile(root/L"fixture.json",Blob(json.begin(),json.end()));
 Host host(std::filesystem::absolute(argv[2]),root);auto ready=host.decode(host.read());
 first.activate(ready.string("pin"),peers);second.activate(ready.string("pin"),peers);
 protocol::Writer server(protocol::FileTransfer);server.string("PeerChat/EmulatorDmServer/v1");server.number(ready.integer("port"));server.string(ready.string("pin"));device.line(rpc::base64(server.data,false));
 stage="Signed device enrollment over pinned authenticated RPC";
 const auto identityPin=ready.string("pin");
 registerIdentity(device,host,first,second,2,2,identityPin);
 registerIdentity(device,host,first,second,3,2,identityPin);
 registerIdentity(device,host,first,second,1,1,identityPin);
 selectIdentityAuthority(device,host,first,second,1,1,identityPin,androidId);
 selectIdentityAuthority(device,host,first,second,2,2,identityPin,first.crypto.profile.peerId);
 applyIdentity(device,host,first,second,2,2,2,identityPin,first.crypto.profile.peerId);
 endorseIdentity(device,host,first,second,2,2,identityPin,second.crypto.profile.peerId);
 applyIdentity(device,host,first,second,1,1,1,identityPin,androidId);
 applyIdentity(device,host,first,second,2,2,2,identityPin,"");
 applyIdentity(device,host,first,second,3,2,2,identityPin,first.crypto.profile.peerId);
 applyIdentity(device,host,first,second,1,1,2,identityPin,first.crypto.profile.peerId);
 applyIdentity(device,host,first,second,2,2,1,identityPin,androidId);
 applyIdentity(device,host,first,second,3,2,1,identityPin,androidId);
 stage="Android↔first Windows authenticated live TLS device pinning; second sibling remains offline";
 for(unsigned index=0;index<1;++index){
  DmPeer& window=index?second:first;need(device.call("T|"+std::to_string(index))==Blob{1},"Android relay demand/open");
  WindowsPinSide side(window.crypto.root,androidId,2,androidId<window.crypto.profile.peerId);
  Blob toAndroid;bool complete=false;
  for(unsigned turn=0;turn<1000;++turn){
   auto result=device.call("X|"+std::to_string(index)+"|"+rpc::base64(toAndroid,false));
   protocol::Reader state(result);need(state.type()==protocol::FileTransfer,"Android TLS round record");auto fromAndroid=state.bytes();bool androidReady=state.number()==1;bool androidClosed=state.number()==1;state.end();need(!androidClosed,"Android TLS closed during verification");
   toAndroid=side.exchange(fromAndroid);if(androidReady&&side.ready()&&toAndroid.empty()&&fromAndroid.empty()){complete=true;break;}
  }
  need(complete,"Real Android/Windows live TLS verification timeout");
 }
 stage="Android and Windows cache capability registration";auto deadline=GetTickCount64()+30000;bool androidReady=false;
 while(GetTickCount64()<deadline){
  emulatorStep(device,host);first.step(host,2);second.step(host,3);
  auto capabilityWire=device.call("P");protocol::Reader capability(capabilityWire);need(capability.type()==protocol::FileTransfer,"Android cache capability state");androidReady=capability.number()==1;capability.end();
  if(androidReady&&first.app->supported()&&second.app->supported())break;Sleep(1);
 }
 need(androidReady&&first.app->supported()&&second.app->supported(),"Android/Windows accepted cache capability timeout");
 stage="Android cached upload through actual server";
 auto offered=device.call("O");protocol::Reader offer(offered);need(offer.type()==protocol::FileTransfer,"Android offer record");auto id=offer.string();offer.end();need(protocol::id(id,32),"Android cached offer ID");
 Blob payload(256*1024+17);for(size_t i=0;i<payload.size();++i)payload[i]=static_cast<unsigned char>(i*37+23);
 deadline=GetTickCount64()+90000;bool uploaded=false;
 while(GetTickCount64()<deadline){emulatorStep(device,host);auto row=emulatorRow(device,id);need(row.state!=6&&row.state!=7,"Android upload failed: "+row.error);if(row.state==4){need(row.name==u8"emulator-日本-📷.bin","Android Unicode source basename changed");uploaded=true;break;}Sleep(1);}
 need(uploaded,"Android cached upload/notices timeout");
 auto receiveWindows=[&](DmPeer& recipient,unsigned actor){
  stage="Windows cached notice/ACK/download";auto fetched=managed(host,"fetch",actor);need(fetched.string("state")=="ready"&&fetched.at("envelopes").array.size()==1,"Windows frozen notice missing");
  auto notice=rpc::unbase64(fetched.at("envelopes").array.front().string("envelope"),70000,false);std::optional<Blob> ack;auto until=GetTickCount64()+30000;
  while(GetTickCount64()<until){ack=recipient.app->dispatchNotice(notice);if(ack&&!ack->empty())break;recipient.step(host,actor);Sleep(1);}
  need(ack&&!ack->empty(),"Windows durable notice ACK unavailable");need(managed(host,"mailbox_ack",actor,*ack).string("state")=="acknowledged","Server rejected Windows signed notice ACK");
  recipient.app->accept(id);until=GetTickCount64()+90000;bool verified=false;
  while(GetTickCount64()<until){recipient.step(host,actor);auto row=recipient.snapshot(id);need(row.state!="failed"&&row.state!="paused","Windows cache download failed: "+row.error);auto path=recipient.io->path(id);if(row.verified&&!path.empty()){need(row.name==u8"emulator-日本-📷.bin"&&readFile(path)==payload,"Android→Windows exact Unicode/plaintext mismatch");verified=true;break;}Sleep(1);}
  need(verified,"Android→Windows verified download timeout");
 };
 receiveWindows(first,2);receiveWindows(second,3);
 stage="Android sender signed verification receipt aggregation";deadline=GetTickCount64()+30000;bool sourceComplete=false;
 while(GetTickCount64()<deadline){first.step(host,2);second.step(host,3);emulatorStep(device,host);sourceComplete=device.call("S|"+id)==Blob{1};if(sourceComplete)break;Sleep(1);}
 need(sourceComplete,"Android source did not verify both recipients' signed file receipts");
 stage="Windows cached reverse upload";auto original=root/L"windows-original-日本-📷.bin";writeFile(original,payload);
 auto reverse=first.app->start(androidId,1,std::make_unique<io::FileSource>(original.wstring()));deadline=GetTickCount64()+90000;
 while(GetTickCount64()<deadline&&!(first.snapshot(reverse).noticesStored&&first.snapshot(reverse).uploadComplete)){first.step(host,2);auto row=first.snapshot(reverse);need(row.state!="failed"&&row.state!="paused","Windows reverse upload failed: "+row.error);Sleep(1);}
 need(first.snapshot(reverse).noticesStored,"Windows reverse cached notice timeout");
 stage="Android reverse notice/ACK/verified import";auto fetched=managed(host,"fetch",1);need(fetched.string("state")=="ready"&&fetched.at("envelopes").array.size()==1,"Android reverse notice missing");
 auto notice=rpc::unbase64(fetched.at("envelopes").array.front().string("envelope"),70000,false);Blob accepted;deadline=GetTickCount64()+30000;
 while(GetTickCount64()<deadline){accepted=device.call("N|"+rpc::base64(notice,false));if(accepted!=Blob{0})break;emulatorStep(device,host);Sleep(1);}
 protocol::Reader acknowledgement(accepted);need(acknowledgement.type()==protocol::Ack,"Android reverse typed notice ACK");acknowledgement.number();auto receipt=acknowledgement.bytes();acknowledgement.string();acknowledgement.string();acknowledgement.end();
 need(managed(host,"mailbox_ack",1,receipt).string("state")=="acknowledged","Server rejected Android durable signed notice ACK");device.call("A|"+reverse);
 deadline=GetTickCount64()+90000;bool androidVerified=false;
 while(GetTickCount64()<deadline){emulatorStep(device,host);auto row=emulatorRow(device,reverse);need(row.state!=6&&row.state!=7,"Android reverse cache failed: "+row.error);if(row.state==4&&!row.path.empty()){need(device.call("D|"+reverse)==Blob{1},"Android exact verified plaintext mismatch");androidVerified=true;break;}Sleep(1);}
 need(androidVerified,"Windows→Android verified cache import timeout");
 stage="Windows sender signed verification receipt aggregation";deadline=GetTickCount64()+30000;bool reverseComplete=false;
 while(GetTickCount64()<deadline){emulatorStep(device,host);first.step(host,2);auto row=first.snapshot(reverse);need(row.state!="failed"&&row.state!="paused","Windows reverse receipt failed: "+row.error);if(row.state=="complete"){reverseComplete=true;break;}Sleep(1);}
 need(reverseComplete,"Windows source did not verify Android signed file receipt");
 for(unsigned i=0;i<4;++i){emulatorStep(device,host);first.step(host,2);second.step(host,3);}
 need(device.call("Q")==Blob{1},"Android fixture shutdown");need(host.command("quit").at("stopped").flag(),"Managed fixture shutdown");
 std::cout<<"PASS real emulator POSIX/Keystore↔Windows first-device Schannel TLS pin, offline second Windows sibling resolved only through signed identity directory, pinned managed TLS server DM cache registration, bidirectional cached Unicode binary plaintext, durable signed notice ACKs, and sender-verified signed file receipts; server RPC proxied by Windows fixture, not Android ServerAccounts\n";
 return 0;
 }catch(const std::exception& error){std::cerr<<stage<<": "<<error.what()<<'\n';return 1;}}
