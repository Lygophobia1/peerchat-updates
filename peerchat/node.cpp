#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define FD_SETSIZE 256
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include "node.hpp"
#include "group_message_envelope.hpp"
#include "group_device_replica.hpp"
#include "group_text.hpp"
#include "delivery_manager.hpp"
#include "archive_application.hpp"
#include "archive_attachment_transfer.hpp"
#include "windows_transfer_io.hpp"
#include "windows_transfer_timing.hpp"
#include "portable/transfer_statistics_storage.hpp"
#include "windows_mailbox.hpp"
#include "windows_group_attachment_application.hpp"
#include "windows_download_save.hpp"
#include "direct_transport_adapter.hpp"
#include "session_transport_manager.hpp"
#include "tls.hpp"
#include "protocol.hpp"
#include "reliable_peer_protocol.hpp"
#include "shared_peer_runtime.hpp"
#include "voice_media.hpp"
#include "call_manager.hpp"
#include "screen_share_manager.hpp"
#include "screen_share_media.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <future>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

// These Windows 8+ APIs are present on Windows 10/11 but missing from older
// MinGW headers. Their documented ABI lets DNS finish/cancel without blocking
// the peer worker and every other connection.
extern "C" INT WSAAPI GetAddrInfoExCancel(LPHANDLE nameHandle);
extern "C" INT WSAAPI GetAddrInfoExOverlappedResult(LPOVERLAPPED lookup);

namespace peerchat {
namespace node_detail {
using Clock=std::chrono::steady_clock;
constexpr size_t MaxConnections=128,MaxQueue=1024*1024;
#ifdef PEERCHAT_TEST_SHORT_CALL_TIMEOUTS
constexpr auto VoiceRingTimeout=std::chrono::milliseconds(750);
constexpr auto VoiceConnectTimeout=std::chrono::seconds(3);
#else
constexpr auto VoiceRingTimeout=std::chrono::seconds(30);
constexpr auto VoiceConnectTimeout=std::chrono::seconds(20);
#endif
#ifdef PEERCHAT_TEST_SHORT_CALL_TIMEOUTS
constexpr std::uint64_t MediaRecoveryWindowMilliseconds=2500,MediaRecoveryRetryMilliseconds=400;
#else
constexpr std::uint64_t MediaRecoveryWindowMilliseconds=30000,MediaRecoveryRetryMilliseconds=4000;
#endif
struct RelayPipe {std::mutex mutex;std::condition_variable changed;std::deque<Blob> output;size_t queued=0;bool closed=false;};
struct Connection : reliable::SharedPeerRuntime {
    SOCKET socket=INVALID_SOCKET;
    bool incoming=false,connecting=false,dead=false,online=false;
    std::wstring host,source,route,closeReason;std::uint64_t attemptId=0;
    session::ReliableAuthorization authorization;
    bool relay=false,archiveBound=false;
    std::shared_ptr<RelayPipe> relayPipe;
    std::unique_ptr<TlsSession> tls;
    Blob output;size_t sent=0;
    Clock::time_point began=Clock::now(),lastProgress=Clock::now();
    std::optional<session::DirectTransportAdapter> directTransport;
    std::set<std::string> sentGroupRosterEvents;
    std::unique_ptr<archive::GroupHistoryJob> groupHistory;
    ~Connection(){stop();if(socket!=INVALID_SOCKET)closesocket(socket);}
};
struct Resolver {
    std::wstring host,service,source,route;std::string expected;std::uint16_t port;std::uint64_t attemptId=0;
    OVERLAPPED overlap{};HANDLE cancel{};PADDRINFOEXW result{};bool pending=false;
    Resolver(const std::wstring& host,std::uint16_t port,const std::string& expected,const std::wstring& source,const std::wstring& route,std::uint64_t attemptId):host(host),service(std::to_wstring(port)),source(source),route(route),expected(expected),port(port),attemptId(attemptId){overlap.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!overlap.hEvent)throw std::runtime_error("Cannot start name lookup");}
    ~Resolver(){if(pending){GetAddrInfoExCancel(&cancel);WaitForSingleObject(overlap.hEvent,INFINITE);}if(result)FreeAddrInfoExW(result);CloseHandle(overlap.hEvent);}
    int begin(){ADDRINFOEXW hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;timeval timeout{5,0};int r=GetAddrInfoExW(host.c_str(),service.c_str(),NS_ALL,nullptr,&hints,&result,&timeout,&overlap,nullptr,&cancel);pending=r==WSA_IO_PENDING;return r;}
};
void nonblocking(SOCKET s){u_long yes=1;if(ioctlsocket(s,FIONBIO,&yes))throw std::runtime_error("Cannot configure socket");BOOL one=TRUE;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&one),sizeof(one));}
std::wstring observedAddress(SOCKET socket){sockaddr_in addr{};int length=sizeof(addr);if(getpeername(socket,reinterpret_cast<sockaddr*>(&addr),&length))return {};wchar_t text[64]{};InetNtopW(AF_INET,&addr.sin_addr,text,64);return text;}
}
using namespace node_detail;
struct Node::Impl {
    enum class Action { Connect,Candidates,PrepareScopedP2p,ScopedP2pProbe,PromoteScopedP2p,CancelScopedP2p,RelayReady,RelayReceive,RelayClosed,InvalidateRoute,NetworkContext,AuthenticatedFriends,PinInitialAuthenticatedFriendIdentity,SaveContact,RemoveContact,Approve,FileTransfer,GroupAttachment,GroupAttachmentNetwork,Send,History,EraseHistory,Name,GroupAccount,AuthenticatedGroupRoster,ClearGroupRosters,BeginGroupControl,GroupCreate,GroupCreationProof,GroupRename,GroupInvite,GroupRespond,GroupLeave,GroupRemove,GroupRole,GroupSend,GroupReceiveCiphertext,GroupMessageStatus,Groups,GroupHistoryPage,MediaIceConfiguration,VoiceStart,VoiceAnswer,VoiceDecline,VoiceEnd,VoiceMute,VoiceDeafen,VoiceDevices,VoiceLevels,VoiceProcessing,VoiceDiagnostics,ScreenBackend,ScreenStart,ScreenAnswer,ScreenDecline,ScreenEnd,ScreenDiagnostics };
    struct Command{Action action;std::string peer,id,group;std::wstring text,name,source,route;std::uint16_t port=0;bool accepted=false,forceImmediate=false;std::int64_t before=INT64_MAX;std::uint64_t accountId=0,sourceHandle=0,generation=0;std::uint32_t sourceKind=0,fps=0,bitrate=0,noiseSuppressionStrength=1,automaticGainStrength=1;screen::Backend screenBackend=screen::Backend::NativeWindows;Contact contact;MediaIceConfiguration ice;Blob bytes;std::vector<AuthenticatedFriend> friends;group_keys::RosterSnapshot roster;std::shared_ptr<std::promise<std::wstring>> response;
        bool mailbox=false,archive=false;std::shared_ptr<std::promise<Blob>> bytesResponse;
    };
    std::atomic<bool> running{false};std::thread worker;std::mutex commandMutex,eventMutex;
    std::condition_variable commandChanged;
    std::deque<Command> commands;std::deque<NodeEvent> events;
    std::unique_ptr<Storage> store;std::unique_ptr<TlsCredentials> credentials;Profile profile;
    std::unique_ptr<delivery::DeliveryManager> deliveries;
    std::unique_ptr<mailbox::WindowsMailbox> mailboxApplication;
    std::unique_ptr<archive::Enrollment> archiveApplication;
    std::unique_ptr<archive::Execution> archiveExecution;
    std::unique_ptr<archive::GroupHistoryAccess> archiveGroups;
    std::unique_ptr<archive::AttachmentTransfers> archiveTransfers;
    void prepareArchiveTransfers(){
        if(archiveTransfers||!archiveApplication||!archiveExecution||!transfers)return;
        archiveTransfers=std::make_unique<archive::AttachmentTransfers>(*store,profile,archiveApplication->policy(),*transfers,
            [](const archive::Attachment& a){transfer::DurableState d;d.location=a.location;d.metadata={a.id,a.name,a.size,a.digest};return transfer::windows::reopenSource(d);},
            [](const transfer::Metadata& m,const Blob& destination){auto folder=unicode::decode(std::string(destination.begin(),destination.end()));return std::make_unique<transfer::windows::FileSink>(folder,m,nullptr,true);},
            [](const transfer::DurableState& d){protocol::Reader r(d.location);protocol::check(r.type()==protocol::FileTransfer);r.text();auto final=r.text();transfer::windows::FileSource source(final,d.metadata.name);protocol::check(source.size()==d.metadata.size);return source.resumeToken();},
            [this](const session::ReliableAuthorization& scope,std::uint64_t now){archiveExecution->hold(scope,now,networkGeneration);});
    }
    std::uint64_t archiveGeneration=0;
    bool archiveAuthenticated=false;
    std::wstring mailboxProfileDirectory;
    std::map<std::string,Contact> contacts;
    std::set<std::string> liveOutboxPeers;
    std::uint64_t demandCheckAt=0;
    std::unique_ptr<transfer::TransferManager> transfers;
    std::map<std::string,std::wstring> transferPaths;
    std::unique_ptr<download_save::Coordinator> downloadSaves;
    std::wstring snippetRoot;
    std::unique_ptr<transfer::windows::CleanupJournal> transferCleanup;
    std::unique_ptr<transfer::windows::DurableJournal> transferJournal;
    std::unique_ptr<group_attachments::windows::CacheStateStore> cacheJournal;
    std::unique_ptr<group_attachments::windows::Application> groupAttachments;
    bool groupAttachmentPersistenceReported=false;
    std::unique_ptr<transfer::ProtectedStatisticsStore> transferStatistics;
    bool transferPersistenceReported=false;
    bool groupBulkMediaActive=false;
    std::set<std::string> cataloguedTransfers;
    std::uint64_t attachmentCatalogAt=0;
    void retainCompletedAttachments(){
        auto now=nowMilliseconds();if(!transfers||now<attachmentCatalogAt)return;attachmentCatalogAt=now+1000;
        archive::AttachmentCatalog catalog(*store);
        for(auto d:transfers->completedRecords()){
            try{
                if(d.purpose!=transfer::Purpose::PeerFile||cataloguedTransfers.count(d.metadata.id)||(archiveTransfers&&archiveTransfers->wasArchiveTransfer(d.metadata.id))||!store->historicalDeviceAccount(profile.peerId)||!store->historicalDeviceAccount(d.peer))continue;
                if(!d.sending){
                    // Normalize a committed receive token into a read-only source
                    // token. No path from an archive peer is ever opened here.
                    protocol::Reader r(d.location);protocol::check(r.type()==protocol::FileTransfer);r.text();auto final=r.text();
                    d.location.clear();
                    try{auto file=std::make_unique<transfer::windows::FileSource>(final,d.metadata.name);if(file->size()==d.metadata.size)d.location=file->resumeToken();}catch(const std::exception&){}
                }
                catalog.completed(d,profile.peerId,[&](const std::string& device){return store->historicalDeviceAccount(device);});
                cataloguedTransfers.insert(d.metadata.id);
            }catch(const std::exception&){transferPersistenceFailure();}
        }
    }
    void transferPersistenceFailure(){
        if(!transferPersistenceReported){notice(NodeEvent::Kind::Error,L"File transfer progress is waiting for protected local storage; messaging is unaffected");transferPersistenceReported=true;}
    }
    Connection* transferConnection(const std::string& peer){
        if(!sessionTransports)return nullptr;
        if(archiveTransfers)if(auto scope=archiveTransfers->scopeForPeer(peer)){
            auto route=sessionTransports->snapshot(scope->continuityKey());
            if(route&&route->connected)for(auto& c:connections)if(c->attemptId==route->activeTransport.value&&c->peerId==peer&&c->online&&!c->dead&&c->archiveBound&&c->authorization.permitsArchiveControl()&&(c->remoteCapabilities&protocol::CapabilityFileTransfer))return c.get();
            return nullptr;
        }
        auto state=sessionTransports->snapshot(peer);auto contact=contacts.find(peer);
        if(!state||!state->connected||contact==contacts.end()||!contact->second.verified)return nullptr;
        for(auto& c:connections)if(c->attemptId==state->activeTransport.value&&c->peerId==peer&&c->online&&!c->dead&&c->approved&&c->authorization.permitsDirectMessage()&&(c->remoteCapabilities&protocol::CapabilityFileTransfer))return c.get();
        return nullptr;
    }
    transfer::windows::TransferTiming transferTiming;
    void serviceTransfers(){
        if(!transfers)return;
        if(downloadSaves)try{downloadSaves->tick(transfers->completedRecords());}catch(const std::exception&){transferPersistenceFailure();}
        if(groupAttachments)try{groupAttachments->tick();}catch(const std::exception&){if(!groupAttachmentPersistenceReported){notice(NodeEvent::Kind::Error,L"Group attachments are waiting for verified protected storage; messaging is unaffected","group-attachment:storage");groupAttachmentPersistenceReported=true;}}
        if(groupAttachments&&archiveGroups&&nowMilliseconds()>=groupReferenceScanAt){groupReferenceScanAt=nowMilliseconds()+1000;try{groupAttachments->visitPublicReferences([this](const Blob& wire){archiveGroups->rememberReference(wire);});}catch(...){/* Reference indexing cannot interrupt accepted transfers. */}}
        transfers->mediaActive(groupBulkMediaActive||calls.active()||screenShares.active()!=nullptr);
        std::set<std::string> peers;for(const auto& row:transfers->snapshots())if(row.purpose==transfer::Purpose::PeerFile)peers.insert(row.peer);
        for(auto& peer:peers){auto c=transferConnection(peer);transfers->capability(peer,c?c->attemptId:0,c&&!c->authorization.permitsArchiveControl()&&(c->remoteCapabilities&protocol::CapabilityTransferPipeline));}
        for(auto& peer:peers){
            auto state=sessionTransports->snapshot(peer);if(!state||!state->connected)continue;
            for(auto& c:connections)if(c->attemptId==state->activeTransport.value&&c->online&&!c->dead&&!(c->remoteCapabilities&protocol::CapabilityFileTransfer))transfers->unsupportedPeer(peer);
        }
        try{transfers->tickScoped(nowMilliseconds(),[&](const transfer::Snapshot& row){if(row.purpose==transfer::Purpose::GroupCacheCiphertext)return groupAttachments?groupAttachments->route(row):0;auto c=transferConnection(row.peer);return c?c->attemptId:0;},
            [&](const transfer::Snapshot& row,const Blob& bytes){
                if(row.purpose==transfer::Purpose::GroupCacheCiphertext){try{return groupAttachments&&groupAttachments->send(row,bytes);}catch(const std::exception&){if(!groupAttachmentPersistenceReported){notice(NodeEvent::Kind::Error,L"Group attachments are waiting for verified protected storage; messaging is unaffected","group-attachment:storage");groupAttachmentPersistenceReported=true;}return false;}}
                if(row.purpose!=transfer::Purpose::PeerFile)return false;
                const auto& peer=row.peer;
                auto c=transferConnection(peer);if(!c||c->output.size()-c->sent>MaxQueue/4)return false;
                // A revoked archive transfer may never escape its purpose by
                // falling through to a coincident ordinary friend route.
                if(archiveTransfers&&archiveTransfers->wasArchiveTransfer(transfer::packetId(bytes))&&!c->authorization.permitsArchiveControl())return false;
                if(c->relayPipe){std::lock_guard<std::mutex> lock(c->relayPipe->mutex);if(c->relayPipe->closed||c->relayPipe->queued>MaxQueue/4)return false;}
                try{auto began=transfer::windows::TransferTiming::now();packet(*c,c->authorization.permitsArchiveControl()?archiveTransfers->outgoing(c->authorization,bytes):bytes);transferTiming.sent(bytes,c->attemptId,began,transfer::windows::TransferTiming::now());return true;}
                catch(...){c->closeReason=L"File transfer observed reliable transport failure";c->dead=true;return false;}
            });retainCompletedAttachments();}catch(const transfer::PersistenceError&){transferPersistenceFailure();}
    }
    void serviceArchive(){
        if(!archiveExecution||!archiveApplication)return;
        try{prepareArchiveTransfers();if(archiveTransfers)archiveTransfers->maintain(nowMilliseconds());}catch(const std::exception&){transferPersistenceFailure();}
        auto retired=archiveExecution->tick(nowMilliseconds(),networkGeneration);
        // Pending rendezvous has no Connection yet. Still release its platform
        // pipe when the SAME manager retires it; otherwise retries collide with
        // the still-open server attachment until its pairing timeout.
        {std::lock_guard<std::mutex> lock(relayMutex);
            for(auto it=relayPipes.begin();it!=relayPipes.end();){
                if(sessionTransports->attachablePeer(sessionEpoch,{it->first})){++it;continue;}
                {std::lock_guard<std::mutex> pipeLock(it->second->mutex);it->second->closed=true;it->second->output.clear();it->second->queued=0;it->second->changed.notify_all();}
                it=relayPipes.erase(it);
            }
        }
        for(auto& c:connections)if(!c->dead&&c->authorization.permitsArchiveControl()){
            try{
                if(!archiveAuthenticated||!archive::currentSessionAuthorization(archiveApplication->policy(),c->authorization)||
                    !sessionTransports->attachablePeer(sessionEpoch,{c->attemptId})||
                    std::any_of(retired.begin(),retired.end(),[&](auto id){return id.value==c->attemptId;})){
                    c->closeReason=L"Archive work completed, paused or authorization unavailable";c->dead=true;
                    if(c->relayPipe){std::lock_guard<std::mutex> lock(c->relayPipe->mutex);c->relayPipe->output.clear();c->relayPipe->queued=0;}
                }else if(c->online&&c->archiveBound&&c->output.size()-c->sent<MaxQueue/4){
                    auto request=archiveTransfers?archiveTransfers->request(c->authorization,c->attemptId):Blob{};
                    if(request.empty())request=archiveExecution->request(c->authorization,nowMilliseconds(),c->attemptId,(c->remoteCapabilities&protocol::CapabilityArchiveDeferred)!=0,(c->remoteCapabilities&protocol::CapabilityArchiveGroups)!=0);
                    if(!request.empty())packet(*c,request);
                }
            }catch(const std::exception&){c->closeReason=L"Archive synchronization failed closed";c->dead=true;}
        }
    }
    static std::wstring transferQuote(const std::wstring& value){
        std::wstring result=L"\"";for(auto ch:value){if(ch==L'\\'||ch==L'"')result+=L'\\';if(ch<32)result+=L' ';else result+=ch;}return result+L"\"";
    }
    std::wstring transferOperation(const Command& command){
        if(!transfers)throw std::runtime_error("File transfers unavailable");
        // Legacy DM/archive actions cannot target the separate cache purpose.
        if(!command.id.empty())for(const auto& row:transfers->snapshots())if(row.metadata.id==command.id&&row.purpose!=transfer::Purpose::PeerFile)throw std::runtime_error("Use the group attachment operation for this object");
        if(command.sourceKind==11){std::wstring result;for(const auto& row:transfers->diagnostics(nowMilliseconds())){auto id=row.substr(9,32);result+=widenUtf8(row+transferTiming.diagnostics(id)+(transferJournal?transferJournal->diagnostics(id):std::string{}))+L"\n";}return result;}
        if(command.sourceKind==12||command.sourceKind==13){
            protocol::check(command.id.empty()||protocol::id(command.id,32));
            if(command.sourceKind==13){protocol::check(command.text.size()<=100);std::wistringstream input(command.text);transfer::Options value;wchar_t a=0,b=0;input>>value.chunkBytes>>a>>value.window>>b>>value.bitsPerSecond;protocol::check(input&&input.eof()&&a==L','&&b==L','&&value.valid());transfers->configure(command.id,value);}
            auto value=transfers->options(command.id);return L"{\"chunkBytes\":"+std::to_wstring(value.chunkBytes)+L",\"window\":"+std::to_wstring(value.window)+L",\"bitsPerSecond\":"+std::to_wstring(value.bitsPerSecond)+L"}";
        }
        if(command.sourceKind==14){protocol::check(command.text==L"0"||command.text==L"1");groupBulkMediaActive=command.text==L"1";return L"ok";}
        if(command.sourceKind==16)return widenUtf8(group_attachments::rpc::base64(group_attachments::windows::captureOverwriteTarget(command.text),false));
        auto saveSelection=[&](){auto encoded=group_attachments::rpc::unbase64(utf8(command.text),65536,false);protocol::Reader r(encoded);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/DownloadSelection/v1");auto target=r.text();auto approval=r.bytes();r.end();return std::make_pair(target,approval);};
        if(command.sourceKind==18){
            protocol::check(downloadSaves!=nullptr);auto records=transfers->completedRecords();auto found=std::find_if(records.begin(),records.end(),[&](const auto& d){return d.metadata.id==command.id&&!d.sending&&d.purpose==transfer::Purpose::PeerFile;});auto selected=saveSelection();
            if(found!=records.end())downloadSaves->retry(command.id,selected.first,selected.second,&*found);
            else{archive::AttachmentCatalog catalog(*store);auto a=catalog.find(command.id);protocol::check(a&&!a->location.empty()&&!transfer::windows::protectedSnippetToken(a->location));transfer::DurableState record;record.metadata={a->id,a->name,a->size,a->digest};record.location=a->location;downloadSaves->retry(command.id,selected.first,selected.second,&record,true);}return L"ok";
        }
        if(command.sourceKind==1||command.sourceKind==7){
            auto contact=contacts.find(command.peer);
            if(!protocol::id(command.peer)||contact==contacts.end()||!contact->second.verified)throw std::runtime_error("Verify the recipient device before sending a file");
            std::wstring sourcePath=command.text;std::unique_ptr<transfer::Source> source;
            if(command.sourceKind==7){
                protocol::check(!command.text.empty()&&command.text.size()<=1024*1024&&!snippetRoot.empty());
                protocol::check(command.text.find(L'\0')==std::wstring::npos);
                auto encoded=utf8(command.text); // Exact UTF-8, no newline or content normalization.
                Blob bytes(encoded.begin(),encoded.end());protocol::check(bytes.size()<=transfer::windows::ProtectedSnippetSource::MaxBytes);
                auto root=std::filesystem::path(snippetRoot);
                std::filesystem::create_directory(root);
                auto attributes=GetFileAttributesW(root.c_str());
                protocol::check(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT));
                sourcePath=(root/(command.id+".snippet")).wstring();
                source=transfer::windows::ProtectedSnippetSource::create(sourcePath,bytes);
                SecureZeroMemory(bytes.data(),bytes.size());SecureZeroMemory(encoded.data(),encoded.size());
            }
            if(!source)source=std::make_unique<transfer::windows::FileSource>(sourcePath);
            try{transfers->offer(command.peer,command.id,std::move(source),nowMilliseconds());}
            catch(...){if(command.sourceKind==7)DeleteFileW(sourcePath.c_str());throw;}
            transferPaths[command.id]=sourcePath;
            sessionTransports->demandLiveRoute(command.peer,nowMilliseconds()+2000,nowMilliseconds());
            return widenUtf8(command.id);
        }
        if(command.sourceKind>=8&&command.sourceKind<=10){
            transfer::DurableState record;auto completed=transfers->completedRecords();
            auto found=std::find_if(completed.begin(),completed.end(),[&](const auto& value){return value.metadata.id==command.id&&value.sending;});
            if(found!=completed.end())record=*found;
            else{archive::AttachmentCatalog catalog(*store);auto a=catalog.find(command.id);protocol::check(a&&(a->sender==profile.peerId||a->senderAccount==authenticatedFriends.localAccount()));record.location=a->location;record.metadata={a->id,a->name,a->size,a->digest};}
            protocol::check(transfer::windows::protectedSnippetToken(record.location));auto source=transfer::windows::reopenSource(record);protocol::check(source->size()<=transfer::windows::ProtectedSnippetSource::MaxBytes);
            if(command.sourceKind==10){
                HANDLE file=CreateFileW(command.text.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);protocol::check(file!=INVALID_HANDLE_VALUE);
                bool ok=true;try{for(std::uint64_t offset=0;offset<source->size();offset+=transfer::ChunkBytes){auto part=source->read(offset,std::min<std::uint64_t>(transfer::ChunkBytes,source->size()-offset));DWORD written=0;if(!WriteFile(file,part.data(),static_cast<DWORD>(part.size()),&written,nullptr)||written!=part.size()){ok=false;break;}}ok=ok&&FlushFileBuffers(file);}catch(...){CloseHandle(file);DeleteFileW(command.text.c_str());throw;}
                CloseHandle(file);if(!ok){DeleteFileW(command.text.c_str());throw std::runtime_error("Snippet copy failed");}return L"ok";
            }
            Blob bytes;auto limit=command.sourceKind==8?std::min<std::uint64_t>(32768,source->size()):source->size();
            for(std::uint64_t offset=0;offset<limit;offset+=transfer::ChunkBytes){auto part=source->read(offset,std::min<std::uint64_t>(transfer::ChunkBytes,limit-offset));bytes.insert(bytes.end(),part.begin(),part.end());}
            for(size_t trim=0;trim<4&&trim<=bytes.size();++trim)try{return unicode::decode(std::string(bytes.begin(),bytes.end()-trim));}catch(const std::exception&){if(command.sourceKind==9)throw;}
            throw std::runtime_error("Snippet preview could not be decoded");
        }
        if(command.sourceKind==2||command.sourceKind==15||command.sourceKind==17){
            auto snapshots=transfers->snapshots();auto found=std::find_if(snapshots.begin(),snapshots.end(),[&](const auto& s){return s.metadata.id==command.id&&!s.sending&&s.state==transfer::State::AwaitingAcceptance;});
            if(found==snapshots.end())throw std::runtime_error("File offer unavailable");
            for(auto i=transferPaths.begin();i!=transferPaths.end();)if(std::none_of(snapshots.begin(),snapshots.end(),[&](const auto& s){return s.metadata.id==i->first;}))i=transferPaths.erase(i);else ++i;
            bool automatic=command.sourceKind!=2;
            if(automatic){protocol::check(downloadSaves!=nullptr);auto selected=command.sourceKind==17?saveSelection():std::make_pair(command.text,Blob{});downloadSaves->select(command.id,selected.first,selected.second);}
            try{auto sink=std::make_unique<transfer::windows::FileSink>(automatic?downloadSaves->staging(mailboxProfileDirectory):command.text,found->metadata,nullptr,true,true);auto path=sink->path();transfers->accept(command.id,std::move(sink),nowMilliseconds());transferPaths[command.id]=path;}
            catch(...){if(automatic)downloadSaves->abandon(command.id);throw;}return L"ok";
        }
        if(command.sourceKind==3){if(archiveTransfers&&archiveTransfers->owns(command.id))archiveTransfers->cancel(command.id);else transfers->cancel(command.id);return L"ok";}
        if(command.sourceKind==4){
            std::uint64_t after=0;if(!command.id.empty()){size_t used=0;after=std::stoull(command.id,&used);protocol::check(used==command.id.size()&&after<=INT64_MAX);}
            auto through=store->attachmentMetadataWatermark();auto page=store->attachmentMetadataPage(static_cast<std::int64_t>(after),through);
            std::wostringstream out;out<<L"{\"next\":"<<page.next<<L",\"complete\":"<<(page.complete?L"true":L"false")<<L",\"records\":[";bool first=true;
            for(const auto& record:page.records){auto a=archive::decodeLocalAttachment(record.record);bool protectedSource=transfer::windows::protectedSnippetToken(a.location);std::wstring localPath;if(!a.location.empty()&&!protectedSource){try{transfer::DurableState d;d.location=a.location;auto source=transfer::windows::reopenSource(d);protocol::Reader token(a.location);protocol::check(token.type()==protocol::FileTransfer);localPath=token.text();}catch(const std::exception&){}}if(!first)out<<L",";first=false;
                out<<L"{\"id\":"<<transferQuote(widenUtf8(a.id))<<L",\"name\":"<<transferQuote(widenUtf8(a.name))<<L",\"size\":"<<a.size<<L",\"protectedSource\":"<<(protectedSource?L"true":L"false")<<L",\"senderPeer\":"<<transferQuote(widenUtf8(a.sender))<<L",\"peer\":"<<transferQuote(widenUtf8(a.conversation))<<L",\"account\":"<<(a.conversation==a.sender?a.senderAccount:a.recipientAccount)<<L",\"sending\":"<<((a.sender==profile.peerId||a.senderAccount==authenticatedFriends.localAccount())?L"true":L"false")<<L",\"time\":"<<a.utc<<L",\"path\":"<<transferQuote(localPath)<<L",\"saveState\":"<<transferQuote(downloadSaves?widenUtf8(downloadSaves->status(a.id,!a.location.empty()).state):L"")<<L",\"savePath\":"<<transferQuote(downloadSaves?downloadSaves->status(a.id,false).path:L"")<<L",\"saveError\":"<<transferQuote(downloadSaves?widenUtf8(downloadSaves->status(a.id,!a.location.empty()).error):L"")<<L",\"availableLocally\":"<<(!a.location.empty()?L"true":L"false")<<L",\"archiveAttested\":"<<(!a.proof.empty()?L"true":L"false")<<L"}";}
            out<<L"]}";return out.str();
        }
        if(command.sourceKind==5){
            protocol::check(archiveAuthenticated&&archiveExecution&&archiveExecution->scope()&&archiveExecution->scope()->archive.source==command.peer);
            prepareArchiveTransfers();protocol::check(bool(archiveTransfers));auto destination=utf8(command.text);
            return widenUtf8(archiveTransfers->download(*archiveExecution->scope(),command.id,Blob(destination.begin(),destination.end())));
        }
        if(command.sourceKind!=0)throw std::runtime_error("Invalid file operation");
        std::wostringstream out;out<<L"[";bool first=true;
        auto snapshots=transfers->snapshots(nowMilliseconds());if(archiveTransfers){auto pending=archiveTransfers->pending();snapshots.insert(snapshots.end(),pending.begin(),pending.end());}
        auto completed=transfers->completedRecords();
        for(auto& s:snapshots){
            if(s.purpose!=transfer::Purpose::PeerFile)continue;
            auto saved=downloadSaves?downloadSaves->status(s.metadata.id,!s.sending&&s.state==transfer::State::Complete):download_save::Coordinator::Status{};
            auto original=archiveTransfers?archiveTransfers->originalAttachment(s.metadata.id):std::optional<archive::Attachment>{};
            bool protectedSource=std::any_of(completed.begin(),completed.end(),[&](const auto& d){return d.metadata.id==s.metadata.id&&d.sending&&transfer::windows::protectedSnippetToken(d.location);});
            if(!first)out<<L",";
            first=false;
            out<<L"{\"id\":"<<transferQuote(widenUtf8(s.metadata.id))<<L",\"logicalId\":"<<transferQuote(widenUtf8(original?original->id:s.metadata.id))
                <<L",\"isArchive\":"<<(original?L"true":L"false")<<L",\"originalSending\":"<<(original&&(original->sender==profile.peerId||original->senderAccount==authenticatedFriends.localAccount())?L"true":L"false")
                <<L",\"conversationPeer\":"<<transferQuote(widenUtf8(original?original->conversation:s.peer))<<L",\"account\":"<<(original?(original->conversation==original->sender?original->senderAccount:original->recipientAccount):0)
                <<L",\"senderPeer\":"<<transferQuote(widenUtf8(original?original->sender:s.sending?profile.peerId:s.peer))
                <<L",\"peer\":"<<transferQuote(widenUtf8(s.peer))<<L",\"name\":"<<transferQuote(widenUtf8(s.metadata.name))
                <<L",\"time\":"<<(original?original->utc:s.created)<<L",\"size\":"<<s.metadata.size<<L",\"done\":"<<s.done<<L",\"state\":"<<static_cast<unsigned>(s.state)<<L",\"sending\":"<<(s.sending?L"true":L"false")
                <<L",\"statisticsKnown\":"<<(s.statisticsKnown?L"true":L"false")<<L",\"statisticsBytes\":"<<s.statisticsBytes<<L",\"durationMs\":"<<s.durationMilliseconds<<L",\"rateBits\":"<<s.currentBitsPerSecond<<L",\"etaReliable\":"<<(s.etaReliable?L"true":L"false")
                <<L",\"saveState\":"<<transferQuote(widenUtf8(saved.state))<<L",\"savePath\":"<<transferQuote(saved.path)<<L",\"saveError\":"<<transferQuote(widenUtf8(saved.error))
                <<L",\"protectedSource\":"<<(protectedSource?L"true":L"false")<<L",\"error\":"<<transferQuote(widenUtf8(s.error))<<L",\"path\":"<<transferQuote(s.state==transfer::State::Complete&&!protectedSource?transferPaths[s.metadata.id]:L"")<<L"}";
        }
        out<<L"]";return out.str();
    }

    reliable::AuthenticatedFriendSnapshot authenticatedFriends;
    std::map<std::string,GroupLedger> groups;
    GroupAccount groupAccount;
    std::unique_ptr<group_keys::GroupKeyManager> groupKeys;
    std::uint64_t groupRosterDiscoveryGeneration=0;
    std::map<std::string,std::int64_t> groupHistoryRefreshes;
    std::map<std::string,std::uint64_t> groupReferenceUiCursors;std::uint64_t groupReferenceScanAt=0;
    std::uint64_t groupHistoryRefreshAt=0;
    std::optional<archive::GroupHistoryContext> historyContext(const std::string& server){
        if(!groupKeys||groupAccount.accountId!=authenticatedFriends.localAccount()||groupRosterDiscoveryGeneration!=authenticatedFriends.generation())return {};
        auto roster=groupKeys->roster(server);if(!roster)return {};auto ledger=groups.find(roster->legacyGroupId);
        if(ledger==groups.end()||!group_keys::GroupKeyManager::matchesSignedLedger(*roster,ledger->second.state()))return {};
        return archive::GroupHistoryContext{*roster,ledger->second.state()};
    }
    bool groupHistoryCurrent(Connection& c){
        if(c.dead||!c.online||!c.relay||c.authorization.scope!=session::ReliableAuthorizationScope::GroupControl||c.peerId!=c.authorization.expectedRemoteDeviceId)return false;
        auto context=historyContext(c.authorization.groupId);if(!context||context->roster.generation!=c.authorization.rosterGeneration)return false;
        auto peer=std::find_if(context->roster.devices.begin(),context->roster.devices.end(),[&](const auto& d){return !d.invited&&d.peerId==c.peerId;});
        auto member=peer==context->roster.devices.end()?nullptr:context->ledger.device(peer->accountId,c.peerId);
        return member&&member->publicKey==c.tls->remotePublicKey()&&group_keys::GroupKeyManager::currentMember(context->roster,groupAccount.accountId,profile.peerId)&&archive::historyParticipationEnabled(*store,groupAccount.accountId,profile.peerId);
    }
    void ensureGroupHistory(Connection& c){
        if(c.groupHistory||!(c.remoteCapabilities&protocol::CapabilityArchiveGroups)||!groupHistoryCurrent(c))return;
        c.groupHistory=std::make_unique<archive::GroupHistoryJob>(*store,profile,groupAccount.accountId,c.authorization,
            [this](const std::string& group){return historyContext(group);},[this,&c]{return groupHistoryCurrent(c);},
            [this](const std::string& local){groupHistoryRefreshes[local]=INT64_MAX;});
    }
    void serviceGroupHistory(){
        const auto now=nowMilliseconds();
        if(now>=groupHistoryRefreshAt&&!groupHistoryRefreshes.empty()){
            groupHistoryRefreshAt=now+1000;auto row=*groupHistoryRefreshes.begin();groupHistoryRefreshes.erase(groupHistoryRefreshes.begin());
            try{emitGroupHistoryPage(row.first,row.second);}catch(...){/* Protected history remains available for explicit retry. */}
        }
        for(auto& c:connections)if(c->online&&!c->dead&&c->authorization.scope==session::ReliableAuthorizationScope::GroupControl){
            try{ensureGroupHistory(*c);if(c->groupHistory&&c->output.size()-c->sent<MaxQueue/2){auto request=c->groupHistory->request(nowMilliseconds());if(!request.empty())packet(*c,request);}}
            catch(...){c->closeReason=L"Group history authorization or verified import failed";c->dead=true;}
        }
    }
    void clearGroupAttachmentSession(){
        if(!groupAttachments)return;
        protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/AttachmentSession/v1");w.number(0);w.number(0);w.string("");groupAttachments->network(0,w.data);
    }
    static bool sameAttachmentRoster(const group_keys::RosterSnapshot& a,const group_keys::RosterSnapshot& b){
        if(a.legacyGroupId!=b.legacyGroupId||a.serverGroupId!=b.serverGroupId||a.generation!=b.generation||a.distributorPeerId!=b.distributorPeerId||a.creationProof!=b.creationProof||a.devices.size()!=b.devices.size())return false;
        for(size_t i=0;i<a.devices.size();++i){const auto& x=a.devices[i];const auto& y=b.devices[i];if(x.accountId!=y.accountId||x.peerId!=y.peerId||x.role!=y.role||x.invited!=y.invited)return false;}
        return true;
    }
    group_attachments::windows::Application::Context attachmentContext(const std::string& group,const group_keys::RosterSnapshot* historical){
        protocol::check(groupKeys&&authenticatedFriends.localAccount()>0&&authenticatedFriends.localAccount()==groupAccount.accountId);
        const auto* current=groupKeys->roster(group);if(!current)current=groupKeys->rosterForLegacy(group);
        protocol::check(current&&group_keys::GroupKeyManager::currentMember(*current,groupAccount.accountId,profile.peerId));
        auto found=groups.find(current->legacyGroupId);protocol::check(found!=groups.end()&&group_keys::GroupKeyManager::matchesSignedLedger(*current,found->second.state()));
        if(!historical)return {*current,found->second.state()};
        protocol::check(historical->serverGroupId==current->serverGroupId&&historical->legacyGroupId==current->legacyGroupId&&historical->generation>0&&historical->generation<=current->generation&&historical->creationProof==current->creationProof);
        if(historical->generation==current->generation){protocol::check(sameAttachmentRoster(*historical,*current));return {*historical,found->second.state()};}
        // Replay only events admitted by the existing verified ledger. Never
        // enable device enrollment to manufacture an historical prefix: device
        // events require their own supported protected-receipt replay boundary.
        GroupLedger prefix;size_t scanned=0,rosterEvents=0;
        for(const auto& event:found->second.events()){
            if(++scanned>4096)break;
            if(!found->second.accepted(event.eventId)||event.kind==GroupEventKind::Message)continue;
            if(event.kind==GroupEventKind::AuthorizeDevice||event.kind==GroupEventKind::RevokeDevice||++rosterEvents>256)break;
            protocol::check(prefix.append(event));
            if(group_keys::GroupKeyManager::matchesSignedLedger(*historical,prefix.state()))return {*historical,prefix.state()};
        }
        throw std::runtime_error("Verified historical group roster evidence unavailable");
    }
    std::vector<std::unique_ptr<Connection>> connections;
    std::vector<std::unique_ptr<Resolver>> resolvers;
    std::mutex relayMutex;std::map<std::uint64_t,std::shared_ptr<RelayPipe>> relayPipes;
    std::uint64_t nextAttemptId=1;
    std::uint64_t nextSessionEpoch=1;
    std::uint64_t nextRouteRevision=1;
    session::SessionEpoch sessionEpoch{};
    std::unique_ptr<session::SessionTransportManager> sessionTransports;
    calls::CallManager calls;
    screen::ScreenShareManager screenShares;
    std::unique_ptr<ScreenShareMedia> screenMedia;
    std::optional<MediaIceConfiguration> mediaIce;
    std::atomic<std::uint32_t> screenStopStage{0};
    std::atomic<std::uint64_t> screenStopStageSince{0};
    bool screenAvailable=false;
    ScreenShareMedia::Options screenOptions{};
    screen::Backend configuredScreenBackend=screen::Backend::NativeWindows;
    std::uint64_t screenMediaLifetime=0,screenMediaGeneration=0,screenNetworkGeneration=0;
    std::uint64_t screenRetryAt=0;
    std::uint64_t screenLastRestartAt=0;
    std::uint64_t screenTelemetryAt=0;
    std::uint64_t screenPriorSentBytes=0,screenPriorReceivedBytes=0;
    std::uint32_t screenReportedRoute=0,screenReportedWidth=0,screenReportedHeight=0;
    bool screenMediaFailed=false;
    std::wstring lastScreenStartupDiagnostics;
    std::wstring lastScreenFailureDiagnostics;
    // Retained long enough for the UI/diagnostics to explain a terminal share
    // failure after its disposable media engine has been stopped.
    std::wstring screenFailureDomain=L"none",screenFailureReason=L"none",screenRecoveryTrigger=L"none";
    std::uint64_t screenRecoveryGenerationFrom=0,screenRecoveryGenerationTo=0;
    std::uint64_t networkGeneration=0,voiceNetworkGeneration=0,voiceConnectionAtStart=0,voiceRequestSentOn=0,voiceRequestReceivedOn=0,voiceOfferSentOn=0,voiceOfferReceivedOn=0,voiceAnswerSentOn=0,voiceAnswerReceivedOn=0,voiceLastSignalConnection=0,voiceCandidatesSent=0,voiceCandidatesReceived=0;
    std::wstring networkFingerprint,voiceNetworkFingerprint;
    std::unique_ptr<VoiceMedia> voiceMedia;std::uint64_t voiceMediaLifetime=0,voiceMediaGeneration=0;std::wstring lastVoiceDiagnostics=L"media_state=idle\nice_dtls_srtp_state=idle\ntrack_open=false\ncodec=Opus not active\ntransport=not active\ncapture_frames=0\nencoded_frames=0\nopus_submitted=0\nsend_api_accepted=0\nrtp_transport_packets_sent=not exposed by pinned C API\nrtp_transport_bytes_sent=not exposed by pinned C API\nremote_track_callback_packets=0\npayload_delivered=0\nreceived_packets=0\ndecoded_frames=0\nplayback_frames=0\nlatest_audio_error=none\nlatest_media_error=none";
    int voiceInputDevice=-1,voiceOutputDevice=-1,voiceInputPercent=100,voiceOutputPercent=100;
    VoiceMedia::ProcessingOptions voiceProcessing;
    std::atomic<float> voiceInputLevel{0.0f},voiceOutputLevel{0.0f};
    std::atomic<unsigned> voiceTelemetryFlags{0};
    SOCKET listener=INVALID_SOCKET;
    // Runtime-only scope leases. A bound port is never persisted or emitted in
    // ordinary presence; only an authorized direct lifecycle may hold it.
    std::set<std::string> directListenerLeases;
    bool startupDirectListener=false;
    ~Impl(){stop();}
    void emit(NodeEvent e){std::lock_guard<std::mutex> lock(eventMutex);if(events.size()>=4096)events.pop_front();events.push_back(std::move(e));}
    void closeDirectListener(){
        if(listener==INVALID_SOCKET)return;
        closesocket(listener);listener=INVALID_SOCKET;
        NodeEvent event;event.kind=NodeEvent::Kind::ConnectionLifecycle;event.text=L"Temporary reliable direct listener closed";emit(std::move(event));
    }
    void ensureDirectListener(const std::string& lease,std::uint16_t requestedPort=0){
        if(!lease.empty())directListenerLeases.insert(lease);
        if(listener!=INVALID_SOCKET)return;
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(listener==INVALID_SOCKET)throw std::runtime_error("Cannot create temporary direct listener");
        try{
            nonblocking(listener);BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof(exclusive));
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(requestedPort);address.sin_addr.s_addr=htonl(INADDR_ANY);
            if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address)))throw std::runtime_error("Cannot bind temporary direct listener");
            sockaddr_in assigned{};int size=sizeof(assigned);
            if(getsockname(listener,reinterpret_cast<sockaddr*>(&assigned),&size))throw std::runtime_error("Cannot read temporary listener port");
            profile.listenPort=ntohs(assigned.sin_port);
            if(!profile.listenPort||listen(listener,16))throw std::runtime_error("Cannot start temporary direct listener");
            NodeEvent event;event.kind=NodeEvent::Kind::ConnectionLifecycle;event.peerId=lease;event.port=profile.listenPort;event.text=L"Temporary reliable direct listener opened";emit(std::move(event));
        }catch(...){closesocket(listener);listener=INVALID_SOCKET;if(!lease.empty())directListenerLeases.erase(lease);throw;}
    }
    void releaseDirectListener(const std::string& lease){if(!lease.empty())directListenerLeases.erase(lease);if(!startupDirectListener&&directListenerLeases.empty())closeDirectListener();}
    void emitGroupHistoryPage(const std::string& groupId,std::int64_t before){
        auto found=groups.find(groupId);if(found==groups.end())throw std::runtime_error("Unknown Group ID");
        const auto& state=found->second.state();
        if(!groupAccount.accountId||(!state.members.count(groupAccount.accountId)&&!state.invitations.count(groupAccount.accountId)))return;
        if(!store->groupHistoryOrderingReady(groupId)){
            groupHistoryRefreshes[groupId]=before;NodeEvent pending;pending.kind=NodeEvent::Kind::GroupHistory;pending.peerId=groupId;pending.text=L"Preparing protected group history ordering";emit(std::move(pending));return;
        }
        auto records=store->authenticatedGroupHistory(groupId,100,before);
        if(!records.empty()||(groupKeys&&groupKeys->isAuthoritativeGroup(groupId))){
            NodeEvent history;history.kind=NodeEvent::Kind::GroupHistory;history.peerId=groupId;
            for(auto& record:records){history.groupEvents.push_back(groupMessageProjection(groupId,record));SecureZeroMemory(record.plaintext.data(),record.plaintext.size());}
            if(archiveGroups){auto roster=groupKeys->rosterForLegacy(groupId);auto context=roster?historyContext(roster->serverGroupId):std::optional<archive::GroupHistoryContext>{};
                if(context){auto refs=archiveGroups->visibleReferences(context->roster.serverGroupId,groupReferenceUiCursors[groupId]);groupReferenceUiCursors[groupId]=refs.next;
                    for(const auto& row:refs.records){auto d=group_attachments::parseDescriptor(row.envelope);GroupEvent event;event.groupId=groupId;event.eventId=d.message;event.actorPeerId=d.sender;event.actorAccountId=d.account;event.kind=GroupEventKind::Message;event.deliverySummary="attachment_reference";event.message=L"Encrypted attachment reference\nPrivate metadata and original order unavailable. Downloads require existing device authorization; cached data may have expired.";history.groupEvents.push_back(std::move(event));}
                    if(!refs.complete||records.size()==100)history.text=L"More group history available";}}
            emit(std::move(history));return;
        }
        std::vector<const GroupEvent*> eligible;
        for(const auto& event:state.messages)if(event.logicalOrder<static_cast<std::uint64_t>(before))eligible.push_back(&event);
        std::sort(eligible.begin(),eligible.end(),[](const GroupEvent* left,const GroupEvent* right){return left->logicalOrder!=right->logicalOrder?left->logicalOrder<right->logicalOrder:left->eventId<right->eventId;});
        constexpr size_t PageSize=100;
        if(eligible.size()>PageSize)eligible.erase(eligible.begin(),eligible.end()-PageSize);
        NodeEvent history;history.kind=NodeEvent::Kind::GroupHistory;history.peerId=groupId;
        for(const auto* event:eligible)history.groupEvents.push_back(*event);
        emit(std::move(history));
    }
    GroupEvent groupMessageProjection(const std::string& local,const AuthenticatedGroupMessage& record){
        group_messages::Envelope envelope;protocol::check(group_messages::parse(record.envelope,envelope)==group_messages::Result::Accepted);
        auto body=group_text::decode(record.plaintext);GroupEvent event;event.groupId=local;event.eventId=envelope.messageId;event.actorPeerId=envelope.senderDevice;event.actorAccountId=envelope.senderAccount;
        event.kind=GroupEventKind::Message;event.message=std::move(body.text);event.createdMilliseconds=body.time;event.logicalOrder=body.order;event.sequence=record.sequence;event.deliverySummary=record.status;return event;
    }
    void receiveGroupMessage(const Blob& wire){
        group_messages::Envelope envelope;auto status=group_messages::parse(wire,envelope);
        if(status!=group_messages::Result::Accepted)return;
        const auto* roster=groupKeys?groupKeys->roster(envelope.groupId):nullptr;
        status=group_messages::Result::RosterUnavailable;
        NodeEvent ack;ack.kind=NodeEvent::Kind::GroupCiphertextAck;ack.expectedPeerId=envelope.groupId;ack.detail=std::wstring(envelope.messageId.begin(),envelope.messageId.end());ack.text=L"rejected";
        if(roster){
            auto found=groups.find(roster->legacyGroupId);
            if(found!=groups.end()){
                group_messages::MessageLayer layer(*store,*groupKeys,profile,groupAccount.accountId);
                auto received=layer.receive(roster->legacyGroupId,found->second.state(),wire,group_text::valid);status=received.result;
                if(status==group_messages::Result::Accepted){
                    auto body=group_text::decode(received.plaintext);store->observeLogicalOrder(body.order);
                    auto record=store->authenticatedGroupMessage(envelope.groupId,envelope.messageId);NodeEvent message;message.kind=NodeEvent::Kind::GroupMessage;message.peerId=roster->legacyGroupId;
                    auto projection=groupMessageProjection(roster->legacyGroupId,record);projection.deliverySummary="Authenticated group message";message.groupEvents.push_back(std::move(projection));emit(std::move(message));
                    SecureZeroMemory(record.plaintext.data(),record.plaintext.size());SecureZeroMemory(received.plaintext.data(),received.plaintext.size());ack.text=L"accepted";
                }else if(status==group_messages::Result::DuplicateMessage)ack.text=L"duplicate_message";
            }
        }
        ack.peerId=roster?roster->legacyGroupId:"";ack.candidateSource=std::wstring(group_messages::resultName(status),group_messages::resultName(status)+std::strlen(group_messages::resultName(status)));emit(std::move(ack));
    }
    void sendGroupMessage(const std::string& local,const std::wstring& text){
        auto found=groups.find(local);if(found==groups.end()||!groupKeys)throw std::runtime_error("Group roster unavailable; no message sent");
        const auto* roster=groupKeys->rosterForLegacy(local);if(!roster)throw std::runtime_error("Group is not server-authorized; no message sent");
        group_messages::MessageLayer layer(*store,*groupKeys,profile,groupAccount.accountId);
        group_text::Body body;body.order=store->nextLogicalOrder();body.time=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());body.text=text;
        Blob plain=group_text::encode(body);auto creation=layer.create(local,found->second.state(),plain);SecureZeroMemory(plain.data(),plain.size());
        if(creation.result!=group_messages::Result::Accepted)throw std::runtime_error(std::string("Encrypted group send failed: ")+group_messages::resultName(creation.result));
        store->setGroupMessageStatus(creation.envelope.groupId,creation.envelope.messageId,"Awaiting server acceptance (online recipients only)");
        auto records=store->authenticatedGroupHistory(local,1);NodeEvent message;message.kind=NodeEvent::Kind::GroupMessage;message.peerId=local;message.groupEvents.push_back(groupMessageProjection(local,records.front()));emit(std::move(message));SecureZeroMemory(records.front().plaintext.data(),records.front().plaintext.size());
        NodeEvent request;request.kind=NodeEvent::Kind::GroupCiphertext;request.peerId=local;request.expectedPeerId=creation.envelope.groupId;request.attemptId=creation.envelope.rosterGeneration;request.detail=group_text::base64(creation.wire);request.text=std::wstring(creation.envelope.messageId.begin(),creation.envelope.messageId.end());emit(std::move(request));
    }
    void notice(NodeEvent::Kind kind,const std::wstring& text,const std::string& peer={}){NodeEvent e;e.kind=kind;e.text=text;e.peerId=peer;emit(std::move(e));}
    static std::wstring endpoint(const std::wstring& host,std::uint16_t port){return host.empty()?L"":host+L":"+std::to_wstring(port);}
    static std::string utf8(const std::wstring& value){
        if(value.empty())return {};
        int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        if(length<=0)return {};
        std::string result(static_cast<size_t>(length),'\0');
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr)!=length)return {};
        return result;
    }
    static std::uint64_t nowMilliseconds(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
    static bool privateIpv4(const std::wstring& host){
        IN_ADDR address{};if(InetPtonW(AF_INET,host.c_str(),&address)!=1)return false;
        auto value=ntohl(address.S_un.S_addr);auto first=(value>>24)&0xff,second=(value>>16)&0xff;
        return first==10||first==127||(first==169&&second==254)||(first==172&&second>=16&&second<=31)||(first==192&&second==168);
    }
    session::RouteObservation routeObservation(const std::wstring& host,std::uint16_t port,const std::wstring& source,const std::wstring& route,bool incoming,session::RouteRevision revision,session::RouteClass routeClass=session::RouteClass::Unknown)const{
        session::RouteObservation observation;
        observation.revision=revision;
        observation.networkRevision=networkGeneration;
        observation.endpointUtf8=utf8(endpoint(host,port));
        observation.sourceUtf8=utf8(source);
        observation.routeUtf8=utf8(route);
        observation.incoming=incoming;
        if(routeClass==session::RouteClass::Unknown){
            if(route==L"same-subnet direct candidate")routeClass=session::RouteClass::DirectLan;
            else if(route==L"public direct candidate")routeClass=session::RouteClass::DirectInternet;
            else if(incoming&&!host.empty())routeClass=privateIpv4(host)?session::RouteClass::DirectLan:session::RouteClass::DirectInternet;
        }
        observation.routeClass=routeClass;
        return observation;
    }
    session::RouteObservation routeObservation(const Connection& connection,session::RouteRevision revision)const{
        auto result=routeObservation(connection.host,connection.port,connection.source,connection.route,connection.incoming,revision);
        result.kind=connection.relay?session::TransportKind::Relay:session::TransportKind::Direct;
        if(connection.relay){result.endpointUtf8="relay:"+connection.expected;result.routeClass=session::RouteClass::ServerRelay;}
        else if(result.routeClass==session::RouteClass::Unknown&&connection.socket!=INVALID_SOCKET){auto observed=observedAddress(connection.socket);if(!observed.empty())result.routeClass=privateIpv4(observed)?session::RouteClass::DirectLan:session::RouteClass::DirectInternet;}
        return result;
    }
    void attachDirectTransport(Connection& connection)noexcept{
        if(!sessionTransports||connection.directTransport)return;
        try{
            auto route=routeObservation(connection,{nextRouteRevision++});
            sessionTransports->registerAttempt(sessionEpoch,{connection.attemptId},connection.expected,route,nowMilliseconds());
            connection.directTransport.emplace(sessionEpoch,session::TransportInstanceId{connection.attemptId},std::move(route),connection.expected);
            sessionTransports->observeTransport(connection.directTransport->connecting(),nowMilliseconds());
        }catch(...){connection.directTransport.reset();}
    }
    void project(Connection& connection,const session::TransportEvent& event)noexcept{
        if(!sessionTransports||!connection.directTransport)return;
        try{sessionTransports->observeTransport(event,nowMilliseconds());}catch(...){}
    }
    NodeEvent connectionEvent(NodeEvent::Kind kind,const std::wstring& text,const Connection& connection,const std::string& peer={}){NodeEvent event;event.kind=kind;event.text=text;event.detail=(connection.incoming?L"inbound":L"outbound")+std::wstring(connection.relay?L" relay":L" direct");event.peerId=peer.empty()?connection.expected:peer;event.expectedPeerId=connection.expected;event.candidate=connection.relay?L"PeerChat server relay":endpoint(connection.host,connection.port);event.candidateSource=connection.source;event.route=connection.route;event.attemptId=connection.attemptId;if(connection.authorization.scope==session::ReliableAuthorizationScope::GroupControl){event.authorizationGroupId=connection.authorization.groupId;event.authorizationRosterGeneration=connection.authorization.rosterGeneration;}if(connection.authorization.permitsArchiveControl()){event.authorizationArchive=group_text::base64(archive::sessionBinding(connection.authorization).data);event.authorizationTargetAccountId=connection.authorization.archive.account;}return event;}
#ifdef PEERCHAT_TEST_SHORT_RECONNECT_BACKOFF
    static constexpr std::uint64_t ReconnectBaseMilliseconds=80,ReconnectCapMilliseconds=800;
#else
    static constexpr std::uint64_t ReconnectBaseMilliseconds=5000,ReconnectCapMilliseconds=300000;
#endif
    static std::wstring widenUtf8(const std::string& value){if(value.empty())return {};int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(length<=0)return {};std::wstring result(static_cast<size_t>(length),L'\0');return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),length)==length?result:std::wstring{};}
    void emitSessionSnapshot(const std::string& peer){
        if(!sessionTransports||peer.empty())return;
        auto snapshot=sessionTransports->snapshot(peer);if(!snapshot)return;
        NodeEvent event;event.kind=NodeEvent::Kind::SessionSnapshot;event.peerId=event.expectedPeerId=peer;event.sessionEpoch=snapshot->epoch.value;event.sessionRevision=snapshot->revision;event.networkGeneration=snapshot->networkRevision;event.sessionConnected=snapshot->connected;event.attemptId=snapshot->activeTransport.value;event.retryDelayMilliseconds=snapshot->retryDelayMilliseconds;event.reliableRoute=static_cast<std::uint32_t>(snapshot->activeRoute.routeClass);event.p2pAvailability=static_cast<std::uint32_t>(snapshot->p2pAvailability);
        constexpr char GroupPrefix[]="group-control:";
        if(peer.compare(0,sizeof(GroupPrefix)-1,GroupPrefix)==0&&peer.size()==sizeof(GroupPrefix)-1+32+1+64&&peer[sizeof(GroupPrefix)-1+32]==':'){
            event.authorizationGroupId=peer.substr(sizeof(GroupPrefix)-1,32);
            event.peerId=event.expectedPeerId=peer.substr(sizeof(GroupPrefix)-1+33);
            if(groupKeys){const auto* roster=groupKeys->roster(event.authorizationGroupId);if(roster)event.authorizationRosterGeneration=roster->generation;}
        }
        if(snapshot->connected){event.text=L"Ready";event.candidate=widenUtf8(snapshot->activeRoute.endpointUtf8);event.candidateSource=widenUtf8(snapshot->activeRoute.sourceUtf8);event.route=widenUtf8(snapshot->activeRoute.routeUtf8);event.detail=snapshot->activeRoute.incoming?L"inbound":L"outbound";}
        else if(!sessionTransports->liveRouteDemanded(peer,nowMilliseconds())&&!snapshot->pending)event.text=L"Idle";
        else if(snapshot->retrySuspended)event.text=L"Identity verification required";
        else if(snapshot->retryScheduled)event.text=L"Retrying with backoff";
        else if(snapshot->pending)event.text=L"Connecting";
        else event.text=L"Offline";
        emit(std::move(event));
    }
    void scheduleReconnect(const std::string& peer,const std::wstring& host,std::uint16_t port,const std::wstring& source,const std::wstring& route,bool suspend=false,std::uint64_t finishedAttempt=0){
        if(sessionTransports&&finishedAttempt)sessionTransports->closeTransport(sessionEpoch,{finishedAttempt},"connection attempt failed",true);
        if(!sessionTransports||peer.empty()||!contacts.count(peer)||host.empty()||!port)return;
        std::uint32_t random=0;BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&random),sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        auto observation=routeObservation(host,port,source.empty()?L"Saved contact":source,route.empty()?L"automatic reconnect":route,false,{nextRouteRevision++});
        auto schedule=sessionTransports->scheduleRetry(peer,observation,nowMilliseconds(),random,suspend);
        if(schedule.scheduled){NodeEvent event;event.kind=NodeEvent::Kind::ReconnectScheduled;event.peerId=event.expectedPeerId=peer;event.candidate=widenUtf8(schedule.target.route.endpointUtf8);event.candidateSource=widenUtf8(schedule.target.route.sourceUtf8);event.route=widenUtf8(schedule.target.route.routeUtf8);event.retryDelayMilliseconds=schedule.delayMilliseconds;event.text=L"Reconnect scheduled";emit(std::move(event));}
        emitSessionSnapshot(peer);
    }
    static const wchar_t* voiceStateName(calls::State state){switch(state){case calls::State::Idle:return L"Idle";case calls::State::Outgoing:return L"Outgoing / Ringing";case calls::State::Incoming:return L"Incoming / Ringing";case calls::State::Negotiating:return L"Negotiating";case calls::State::ConnectingMedia:return L"Connecting Media";case calls::State::Connected:return L"Connected";case calls::State::Ending:return L"Ending";case calls::State::Ended:return L"Ended";case calls::State::Failed:return L"Failed";}return L"Idle";}
    void voiceEvent(calls::State state,const std::wstring& reason={}){auto active=calls.active();NodeEvent event;event.kind=NodeEvent::Kind::VoiceCall;event.callState=static_cast<std::uint32_t>(state);if(active){event.peerId=calls.peer();event.detail=std::wstring(active->id.value.begin(),active->id.value.end());event.callLifetime=active->lifetime;event.callType=static_cast<std::uint32_t>(active->type);event.callMuted=active->muted;event.callDeafened=active->deafened;event.mediaState=static_cast<std::uint32_t>(active->mediaState);event.mediaGeneration=active->mediaGeneration;event.mediaRoute=static_cast<std::uint32_t>(active->mediaRoute);event.callControlAvailable=active->controlAvailable;}event.text=voiceStateName(state);if(!reason.empty())event.text+=L" · "+reason;emit(std::move(event));}
    Connection* voiceConnection(const std::string& peer){
        std::uint64_t winner=0;if(sessionTransports){auto snapshot=sessionTransports->snapshot(peer);if(snapshot&&snapshot->connected)winner=snapshot->activeTransport.value;}
        if(winner)for(auto& connection:connections)if(connection->attemptId==winner&&connection->online&&!connection->dead&&connection->peerId==peer&&connection->voiceCalls&&connection->authorization.permitsDirectMessage())return connection.get();
        for(auto& connection:connections)if(connection->online&&!connection->dead&&connection->peerId==peer&&connection->voiceCalls&&connection->authorization.permitsDirectMessage())return connection.get();
        return nullptr;
    }
    Connection* screenConnection(const std::string& peer){
        std::uint64_t winner=0;
        if(sessionTransports){auto snapshot=sessionTransports->snapshot(peer);if(snapshot&&snapshot->connected)winner=snapshot->activeTransport.value;}
        if(winner)for(auto& connection:connections)if(connection->attemptId==winner&&connection->online&&!connection->dead&&connection->peerId==peer&&connection->screenShare&&connection->authorization.permitsDirectMessage())return connection.get();
        if(sessionTransports)return nullptr;
        for(auto& connection:connections)if(connection->online&&!connection->dead&&connection->peerId==peer&&connection->screenShare&&connection->authorization.permitsDirectMessage())return connection.get();
        return nullptr;
    }
    std::wstring screenDiagnosticsSnapshot(){
        auto active=screenShares.active();
        if(!active&&!screenMedia&&!lastScreenFailureDiagnostics.empty())return lastScreenFailureDiagnostics;
        if(active&&active->state==screen::State::Recovering&&!screenMedia&&!lastScreenFailureDiagnostics.empty())return lastScreenFailureDiagnostics;
        std::wostringstream out;
        out<<L"share_id="<<(active?widenUtf8(active->id):L"none")<<L"\nexpected_peer_id="<<(active?widenUtf8(active->peer):L"none")
           <<L"\nshare_lifetime="<<(active?active->lifetime:0)<<L"\nmedia_generation="<<(active?active->generation:0)
           <<L"\nshare_state="<<(active?static_cast<unsigned>(active->state):0)<<L"\ncontrol_available="<<(active&&active->controlAvailable?L"true":L"false")
           <<L"\nnetwork_generation="<<screenNetworkGeneration<<L"\nmedia_failure_domain="<<screenFailureDomain
           <<L"\nmedia_failure_reason="<<screenFailureReason<<L"\nrecovery_trigger="<<screenRecoveryTrigger
           <<L"\nrecovery_generation_from="<<screenRecoveryGenerationFrom<<L"\nrecovery_generation_to="<<screenRecoveryGenerationTo;
        auto sent=screenPriorSentBytes,received=screenPriorReceivedBytes;
        if(screenMedia){auto stats=screenMedia->statistics();sent+=stats.encodedBytesSubmitted;received+=stats.mediaBytesReceived;out<<L"\n"<<screenMedia->diagnostics();}
        else if(!lastScreenStartupDiagnostics.empty())out<<L"\n"<<lastScreenStartupDiagnostics;
        out<<L"\nlogical_share_encoded_bytes_submitted="<<sent<<L"\nlogical_share_rtp_bytes_received="<<received;
        out<<L"\nconfigured_backend="<<(configuredScreenBackend==screen::Backend::WebRtcCompatibility?L"WebRTC Compatibility":L"Native Windows")
           <<L"\npeer_backend_requirement="<<(active&&active->backendNegotiated?(active->peerRequirement==screen::Backend::WebRtcCompatibility?L"WebRTC Compatibility":L"Native Windows"):L"Unknown")
           <<L"\nnegotiated_backend="<<(active&&active->backendNegotiated?(active->backend==screen::Backend::WebRtcCompatibility?L"WebRTC Compatibility":L"Native Windows"):L"Not negotiated")
           <<L"\nactive_backend="<<(active&&active->backendNegotiated&&screenMedia?(active->backend==screen::Backend::WebRtcCompatibility?L"WebRTC Compatibility":L"Native Windows"):L"none");
        return out.str();
    }
    void screenEvent(const std::wstring& reason={}){
        auto active=screenShares.active();NodeEvent event;event.kind=NodeEvent::Kind::ScreenShare;
        event.screenFailureDomain=screenFailureDomain;event.screenFailureReason=screenFailureReason;
        event.screenRecoveryTrigger=screenRecoveryTrigger;event.screenRecoveryGenerationFrom=screenRecoveryGenerationFrom;
        event.screenRecoveryGenerationTo=screenRecoveryGenerationTo;
        if(active){event.peerId=active->peer;event.detail=widenUtf8(active->id);event.shareLifetime=active->lifetime;event.shareGeneration=active->generation;event.shareState=static_cast<std::uint32_t>(active->state);event.callControlAvailable=active->controlAvailable;event.screenMediaFailed=screenMediaFailed;if(screenMedia){auto stats=screenMedia->statistics();auto route=screenMedia->route();event.screenRoute=static_cast<std::uint32_t>(route);event.screenWidth=stats.width;event.screenHeight=stats.height;
            if(stats.ratesAvailable){event.screenFps=static_cast<std::uint32_t>(stats.trackOpen?(active->sender?stats.transmittedFps:stats.renderedFps):0);event.screenBitrateMbps=active->sender?stats.outgoingMbps:stats.incomingMbps;}
            std::wostringstream summary;
            summary<< (stats.width&&stats.height?std::to_wstring(stats.width)+L"×"+std::to_wstring(stats.height):L"resolution —") << L" · ";
            if(stats.ratesAvailable)summary<<static_cast<unsigned>(stats.trackOpen?(active->sender?stats.transmittedFps:stats.renderedFps):0)<<L" FPS";
            else summary<<L"FPS —";
            summary<<L" · H.264 · ";
            if(stats.ratesAvailable){summary.setf(std::ios::fixed);summary.precision(1);summary<<(active->sender?stats.outgoingMbps:stats.incomingMbps)<<L" Mbps "<<(active->sender?L"submitted":L"received");}
            else summary<<L"bitrate —";
            summary<<L" · RTT — · "<<(route==ScreenShareMedia::Route::DirectHostIce?L"Direct host ICE":route==ScreenShareMedia::Route::NatTraversedIce?L"NAT-traversed Direct ICE":route==ScreenShareMedia::Route::TurnRelay?L"TURN media relay":L"route —");
            event.screenSummary=summary.str();
        }}
        event.screenDetails=screenDiagnosticsSnapshot();
        event.text=reason;emit(std::move(event));
    }
    void stopScreenMedia(){if(screenMedia){screenStopStageSince=GetTickCount64();screenStopStage=100;lastScreenStartupDiagnostics=screenMedia->startupDiagnostics();auto stats=screenMedia->statistics();screenPriorSentBytes+=stats.encodedBytesSubmitted;screenPriorReceivedBytes+=stats.mediaBytesReceived;screenMedia->stop([this](std::uint32_t stage){screenStopStageSince=GetTickCount64();screenStopStage=stage;});}screenMedia.reset();screenMediaLifetime=screenMediaGeneration=0;}
    void clearScreen(const std::wstring& reason,bool preserveFailure=false){
        auto active=screenShares.active();if(!active&&!screenMedia)return;
        auto id=active?active->id:std::string{},peer=active?active->peer:std::string{};
        auto lifetime=active?active->lifetime:0,generation=active?active->generation:0;
        if(preserveFailure)lastScreenFailureDiagnostics=screenDiagnosticsSnapshot();
        stopScreenMedia();screenShares.clear();screenRetryAt=screenLastRestartAt=screenTelemetryAt=0;screenReportedRoute=screenReportedWidth=screenReportedHeight=0;if(!preserveFailure){screenMediaFailed=false;screenFailureDomain=L"none";screenFailureReason=L"none";screenRecoveryTrigger=L"none";screenRecoveryGenerationFrom=screenRecoveryGenerationTo=0;}screenPriorSentBytes=screenPriorReceivedBytes=0;
        NodeEvent event;event.kind=NodeEvent::Kind::ScreenShare;event.peerId=peer;event.detail=widenUtf8(id);event.shareLifetime=lifetime;event.shareGeneration=generation;event.shareState=static_cast<std::uint32_t>(screen::State::Idle);event.screenMediaFailed=screenMediaFailed;event.screenFailureDomain=screenFailureDomain;event.screenFailureReason=screenFailureReason;event.screenRecoveryTrigger=screenRecoveryTrigger;event.screenRecoveryGenerationFrom=screenRecoveryGenerationFrom;event.screenRecoveryGenerationTo=screenRecoveryGenerationTo;event.screenDetails=preserveFailure?lastScreenFailureDiagnostics:L"";event.text=reason;emit(std::move(event));
    }
    bool configureScreenMedia(){
        auto active=screenShares.active();if(!active)return false;
        if(!mediaIce||!mediaIce->valid(static_cast<std::int64_t>(std::time(nullptr))))return false;
        const bool recovering=active->state==screen::State::Recovering;
        stopScreenMedia();screenStopStage=0;screenStopStageSince=0;screenOptions.ice=*mediaIce;screenOptions.backend=active->backend;screenMedia=std::make_unique<ScreenShareMedia>(screenOptions);
        screenMediaLifetime=active->lifetime;screenMediaGeneration=active->generation;
        screenNetworkGeneration=networkGeneration;screenTelemetryAt=0;screenMediaFailed=false;lastScreenStartupDiagnostics.clear();
        // Keep the reason that created this replacement generation visible to
        // diagnostics. A fresh logical share starts with no inherited fault.
        if(!recovering){screenFailureDomain=L"none";screenFailureReason=L"none";screenRecoveryTrigger=L"none";screenRecoveryGenerationFrom=screenRecoveryGenerationTo=0;lastScreenFailureDiagnostics.clear();}
        screenReportedRoute=screenReportedWidth=screenReportedHeight=0;return true;
    }
    void interruptScreen(const std::wstring& reason,const std::wstring& trigger=L"ICE/TURN/DTLS transport failure"){
        auto active=screenShares.active();if(!active)return;
        // Only a disposable, authenticated media transport failure can enter
        // Stage 7 recovery. Local capture/codec faults are terminal for this
        // logical share and must not create fresh TURN allocations.
        screenMediaFailed=true;screenFailureDomain=L"ice_turn_transport";screenFailureReason=reason;
        screenRecoveryTrigger=trigger;screenRecoveryGenerationFrom=active->generation;screenRecoveryGenerationTo=active->generation+1;
        lastScreenFailureDiagnostics=screenDiagnosticsSnapshot();
        stopScreenMedia();screenShares.transition(screen::State::Recovering,nowMilliseconds()+MediaRecoveryWindowMilliseconds);
        screenRetryAt=nowMilliseconds()+MediaRecoveryRetryMilliseconds;screenEvent(reason);
    }
    static std::wstring screenFailureDomainName(ScreenShareMedia::FailureDomain domain){
        switch(domain){
        case ScreenShareMedia::FailureDomain::LocalCapture:return L"local_capture";
        case ScreenShareMedia::FailureDomain::LocalEncoder:return L"local_encoder";
        case ScreenShareMedia::FailureDomain::IceTurnTransport:return L"ice_turn_transport";
        case ScreenShareMedia::FailureDomain::DtlsSecurity:return L"dtls_security";
        case ScreenShareMedia::FailureDomain::Rtp:return L"rtp";
        case ScreenShareMedia::FailureDomain::RemoteDecoder:return L"remote_decoder";
        case ScreenShareMedia::FailureDomain::Presentation:return L"presentation";
        default:return L"unknown";
        }
    }
    void failScreenLocally(const std::wstring& reason,ScreenShareMedia::FailureDomain domain){
        auto active=screenShares.active();if(!active)return;
        screenMediaFailed=true;screenFailureDomain=Impl::screenFailureDomainName(domain);screenFailureReason=reason;
        screenRecoveryTrigger=L"none: local media failure is not transport recoverable";
        screenRecoveryGenerationFrom=active->generation;screenRecoveryGenerationTo=active->generation;
        // Tell the peer to retire its matching ShareId; its media engine is
        // still independently authenticated and this signal does not alter
        // the healthy reliable relay.
        if(auto connection=screenConnection(active->peer)){protocol::Writer end(protocol::ScreenEnd);end.string(active->id);packet(*connection,end);}
        const auto title=domain==ScreenShareMedia::FailureDomain::LocalEncoder?L"Screen Share — Encoder Error":
            domain==ScreenShareMedia::FailureDomain::LocalCapture?L"Screen Share — Capture Error":
            domain==ScreenShareMedia::FailureDomain::RemoteDecoder?L"Screen Share — Decoder Error":
            domain==ScreenShareMedia::FailureDomain::Presentation?L"Screen Share — Presentation Error":L"Screen Share — Error";
        clearScreen(title,true);
    }
    void serviceScreenRecovery(){
        auto active=screenShares.active();if(!active||active->state!=screen::State::Recovering||screenRetryAt>nowMilliseconds())return;
        auto connection=screenConnection(active->peer);
        if(!connection){active->controlAvailable=false;screenRetryAt=nowMilliseconds()+500;return;}
        active->controlAvailable=true;
        if(active->sender){screenLastRestartAt=nowMilliseconds();screenShares.restart(nowMilliseconds()+MediaRecoveryWindowMilliseconds);if(!configureScreenMedia()){screenRetryAt=nowMilliseconds()+1000;screenEvent(L"TURN media credentials are unavailable · waiting to recover");return;}std::string error;
            if(!screenMedia->startOffer(error)){stopScreenMedia();screenRetryAt=nowMilliseconds()+1000;return;}
            screenRetryAt=nowMilliseconds()+MediaRecoveryRetryMilliseconds;
        }else{protocol::Writer request(protocol::ScreenRecoveryRequest);request.string(active->id);request.number(active->generation);packet(*connection,request);screenRetryAt=nowMilliseconds()+2000;}
    }
    void screenMediaEvents(){
        auto active=screenShares.active();if(!active||!screenMedia)return;
        const auto id=active->id,peer=active->peer;
        const auto lifetime=active->lifetime,generation=active->generation;
        if(screenMediaLifetime!=lifetime||!screenShares.matchesMedia(lifetime,id,peer,screenMediaGeneration)){stopScreenMedia();return;}
        auto connection=screenConnection(peer);
        for(auto& event:screenMedia->poll()){
            if(!screenShares.matchesMedia(lifetime,id,peer,generation))return;
            if(event.kind==ScreenShareMedia::EventKind::Description){
                if(!connection){interruptScreen(L"Share control unavailable");return;}
                protocol::Writer signal(event.detail=="offer"?protocol::ScreenOffer:protocol::ScreenAnswer);
                signal.string(id);signal.number(generation);signal.string(event.value);packet(*connection,signal);
            }else if(event.kind==ScreenShareMedia::EventKind::Candidate){
                if(connection){protocol::Writer signal(protocol::ScreenCandidate);signal.string(id);signal.number(generation);signal.string(event.value);signal.string(event.detail);packet(*connection,signal);}
            }else if(event.kind==ScreenShareMedia::EventKind::Connected){screenShares.transition(screen::State::Connected);screenEvent(L"Connected · H.264 · DTLS-SRTP");
            }else if(event.kind==ScreenShareMedia::EventKind::Failed){
                if(event.failure!=ScreenShareMedia::FailureDomain::IceTurnTransport&&event.failure!=ScreenShareMedia::FailureDomain::Rtp){failScreenLocally(widenUtf8(event.value),event.failure);return;}
                interruptScreen(widenUtf8(event.value),event.failure==ScreenShareMedia::FailureDomain::Rtp?L"RTP media transport failure":L"ICE/TURN media transport failure");return;
            }else{interruptScreen(L"Screen media disconnected",L"media transport disconnected");return;}
        }
        if(active->state==screen::State::Connected&&nowMilliseconds()>=screenTelemetryAt){
            screenTelemetryAt=nowMilliseconds()+1000;
            auto stats=screenMedia->statistics();auto route=static_cast<std::uint32_t>(screenMedia->route());
            screenReportedWidth=stats.width;screenReportedHeight=stats.height;screenReportedRoute=route;
            screenEvent(L"Connected · H.264 · DTLS-SRTP");
        }
    }
    void resetVoiceSignaling(Connection* connection=nullptr){voiceNetworkGeneration=networkGeneration;voiceNetworkFingerprint=networkFingerprint;voiceConnectionAtStart=connection?connection->attemptId:0;voiceRequestSentOn=voiceRequestReceivedOn=voiceOfferSentOn=voiceOfferReceivedOn=voiceAnswerSentOn=voiceAnswerReceivedOn=voiceLastSignalConnection=voiceCandidatesSent=voiceCandidatesReceived=0;}
    std::wstring voiceSignalingDiagnostics()const{std::wostringstream out;auto active=calls.active();out<<L"call_media_generation="<<(active?active->mediaGeneration:0)<<L"\ncall_media_state="<<(active?static_cast<unsigned>(active->mediaState):0)<<L"\ncall_control_available="<<(active&&active->controlAvailable?L"true":L"false")<<L"\nwinning_peerchat_connection_id_at_call_start="<<voiceConnectionAtStart<<L"\nvoice_network_generation="<<voiceNetworkGeneration<<L"\ncurrent_network_generation="<<networkGeneration<<L"\npeer_connection_matches_current_network="<<(voiceNetworkGeneration==networkGeneration?L"true":L"false")<<L"\nvoice_network_fingerprint="<<voiceNetworkFingerprint<<L"\ncall_request_sent_connection_id="<<voiceRequestSentOn<<L"\ncall_request_received_connection_id="<<voiceRequestReceivedOn<<L"\ncall_answer_sent_connection_id="<<voiceAnswerSentOn<<L"\ncall_answer_received_connection_id="<<voiceAnswerReceivedOn<<L"\nsdp_offer_created="<<(voiceOfferSentOn?L"true":L"false")<<L"\nsdp_offer_sent_connection_id="<<voiceOfferSentOn<<L"\nsdp_offer_received_connection_id="<<voiceOfferReceivedOn<<L"\nsdp_answer_created="<<(voiceAnswerSentOn?L"true":L"false")<<L"\nsdp_answer_sent_connection_id="<<voiceAnswerSentOn<<L"\nsdp_answer_received_connection_id="<<voiceAnswerReceivedOn<<L"\nlast_voice_signaling_connection_id="<<voiceLastSignalConnection<<L"\nlocal_ice_candidates_sent="<<voiceCandidatesSent<<L"\nremote_ice_candidates_received="<<voiceCandidatesReceived;return out.str();}
    void resetVoiceTelemetry(){voiceInputLevel=0.0f;voiceOutputLevel=0.0f;voiceTelemetryFlags=0;}
    void publishVoiceTelemetry(){if(!voiceMedia){resetVoiceTelemetry();return;}auto telemetry=voiceMedia->telemetry();voiceInputLevel=telemetry.inputLevel;voiceOutputLevel=telemetry.outputLevel;voiceTelemetryFlags=(telemetry.active?1u:0u)|(telemetry.inputAvailable?2u:0u)|(telemetry.outputAvailable?4u:0u);}
    void stopVoiceMedia(){resetVoiceTelemetry();if(voiceMedia){lastVoiceDiagnostics=voiceSignalingDiagnostics()+L"\n"+voiceMedia->diagnostics();voiceMedia->stop();}voiceMedia.reset();voiceMediaLifetime=voiceMediaGeneration=0;}
    void clearVoice(const std::wstring& reason,bool failed=false){auto active=calls.active();if(!active&&!voiceMedia)return;std::string callId=active?active->id.value:std::string{},peer=active?calls.peer():std::string{};std::uint64_t lifetime=active?active->lifetime:0,generation=active?active->mediaGeneration:0;voiceEvent(calls::State::Ending,reason);stopVoiceMedia();calls.finish(failed?calls::State::Failed:calls::State::Ended);NodeEvent event;event.kind=NodeEvent::Kind::VoiceCall;event.peerId=peer;event.detail=std::wstring(callId.begin(),callId.end());event.callLifetime=lifetime;event.callState=static_cast<std::uint32_t>(calls::State::Idle);event.callType=static_cast<std::uint32_t>(calls::CallType::Voice);event.mediaState=static_cast<std::uint32_t>(calls::MediaState::Closed);event.mediaGeneration=generation;event.text=L"Idle";if(!reason.empty())event.text+=L" · "+reason;emit(std::move(event));}
bool configureVoiceMedia(std::uint64_t generation){if(!mediaIce||!mediaIce->valid(static_cast<std::int64_t>(std::time(nullptr))))return false;auto active=calls.active();if(!active)return false;VoiceMedia::ReceiveBinding binding{calls.peer(),active->id.value,calls.lifetime(),generation};voiceMedia=std::make_unique<VoiceMedia>(*mediaIce,voiceInputDevice,voiceOutputDevice,networkGeneration,std::string(networkFingerprint.begin(),networkFingerprint.end()),voiceProcessing,std::move(binding));voiceMedia->setVolumes(voiceInputPercent/100.0f,voiceOutputPercent/100.0f);voiceMediaLifetime=calls.lifetime();voiceMediaGeneration=generation;if(auto active=calls.active())voiceMedia->setDeafened(active->deafened);voiceNetworkGeneration=networkGeneration;voiceNetworkFingerprint=networkFingerprint;return true;}
    void interruptVoiceMedia(const std::wstring& reason){auto active=calls.active();if(!active)return;if(!active->mediaRecoveryCapable){if(auto connection=voiceConnection(calls.peer())){protocol::Writer end(protocol::CallEnd);end.string(active->id.value);packet(*connection,end);}clearVoice(reason,true);return;}calls.interruptMedia(nowMilliseconds(),nowMilliseconds()+MediaRecoveryWindowMilliseconds);stopVoiceMedia();voiceEvent(active->state,L"Voice connection interrupted · Reconnecting…");}
    void serviceMediaRecovery(){auto active=calls.active();if(!active||!calls.mediaRetryDue(nowMilliseconds()))return;auto connection=voiceConnection(calls.peer());if(!connection){calls.deferMediaRetry(nowMilliseconds(),500);return;}calls.observeControl(true);if(profile.peerId<calls.peer()){
            auto generation=calls.beginMediaRecovery(nowMilliseconds(),MediaRecoveryRetryMilliseconds);if(!generation)return;if(!configureVoiceMedia(generation)){calls.deferMediaRetry(nowMilliseconds(),1000);voiceEvent(active->state,L"TURN media credentials are unavailable · waiting to recover");return;}std::wstring error;if(!voiceMedia->startOffer(error)){stopVoiceMedia();calls.deferMediaRetry(nowMilliseconds(),1000);return;}voiceEvent(active->state,L"Voice connection interrupted · Reconnecting…");
        }else{protocol::Writer request(protocol::CallMediaRecoveryRequest);request.string(active->id.value);request.number(active->mediaGeneration);packet(*connection,request);calls.deferMediaRetry(nowMilliseconds(),2000);}}
    void mediaEvents(){
        auto active=calls.active();if(!voiceMedia||!active)return;
        const auto lifetime=active->lifetime;const auto generation=voiceMediaGeneration;const auto callId=active->id.value;const auto peer=calls.peer();if(voiceMediaLifetime!=lifetime||!calls.matchesMedia(lifetime,callId,peer,generation)){stopVoiceMedia();return;}
        auto connection=voiceConnection(peer);
        for(auto& event:voiceMedia->poll()){
            if(!calls.matchesMedia(lifetime,callId,peer,generation))return;
            if(event.kind==VoiceMedia::EventKind::Description){
                if(!connection){calls.observeControl(false,nowMilliseconds()+MediaRecoveryWindowMilliseconds);calls.deferMediaRetry(nowMilliseconds(),500);stopVoiceMedia();voiceEvent(active->state,L"Call control unavailable · waiting to reconnect");return;}
                voiceLastSignalConnection=connection->attemptId;if(event.detail=="offer")voiceRequestSentOn=voiceOfferSentOn=connection->attemptId;else voiceAnswerSentOn=connection->attemptId;
                protocol::Writer signal(generation>1?(event.detail=="offer"?protocol::CallMediaRestart:protocol::CallMediaAnswer):(event.detail=="offer"?protocol::CallOffer:protocol::CallAnswer));signal.string(callId);if(generation>1)signal.number(generation);signal.string(event.value);packet(*connection,signal);
            }else if(event.kind==VoiceMedia::EventKind::Candidate){
                if(connection){voiceLastSignalConnection=connection->attemptId;++voiceCandidatesSent;protocol::Writer signal(generation>1?protocol::CallMediaCandidate:protocol::CallCandidate);signal.string(callId);if(generation>1)signal.number(generation);signal.string(event.value);signal.string(event.detail);packet(*connection,signal);}
            }else if(event.kind==VoiceMedia::EventKind::Connected){
                auto route=voiceMedia->route();auto callRoute=route==VoiceMedia::Route::DirectLanIce?calls::MediaRoute::DirectLanIce:route==VoiceMedia::Route::NatTraversedIce?calls::MediaRoute::NatTraversedIce:route==VoiceMedia::Route::TurnRelay?calls::MediaRoute::TurnRelay:calls::MediaRoute::Unknown;calls.transition(callId,calls::State::Connected);calls.transitionMedia(generation,calls::MediaState::Connected,callRoute);if(active->muted){std::wstring ignored;voiceMedia->setMuted(true,ignored);}voiceMedia->setDeafened(active->deafened);voiceEvent(calls::State::Connected,L"WebRTC · Opus · DTLS-SRTP");
                if(connection){protocol::Writer ready(protocol::CallConnected);ready.string(callId);packet(*connection,ready);}
            }else if(event.kind==VoiceMedia::EventKind::AudioError){
                notice(NodeEvent::Kind::Error,L"Voice audio device error: "+std::wstring(event.value.begin(),event.value.end()),peer);
            }else if(event.kind==VoiceMedia::EventKind::AudioProcessingError){
                notice(NodeEvent::Kind::Error,L"Voice audio processing degraded: "+std::wstring(event.value.begin(),event.value.end()),peer);
            }else {interruptVoiceMedia(event.kind==VoiceMedia::EventKind::Failed?L"Secure media connection failed":L"Media disconnected");return;}
        }
        publishVoiceTelemetry();
    }
    void tcpFailure(Connection& c,int error){
        std::wstring text=error==WSAECONNREFUSED?L"TCP connection refused: the peer is not listening on this port or Firewall blocked it.":error==WSAETIMEDOUT?L"TCP connection timeout: the peer or Firewall did not respond.":error==WSAEHOSTUNREACH||error==WSAENETUNREACH?L"Peer unreachable/offline: Windows has no network route to the peer endpoint.":L"TCP connection failed: check the peer address, listener, and Firewall.";
        emit(connectionEvent(NodeEvent::Kind::Error,text,c));
    }
    bool command(Command c){std::lock_guard<std::mutex> lock(commandMutex);if(!running||commands.size()>=512)return false;commands.push_back(std::move(c));commandChanged.notify_one();return true;}
    void publishContacts(){NodeEvent e;e.kind=NodeEvent::Kind::Contacts;for(const auto& item:contacts)e.contacts.push_back(item.second);emit(std::move(e));}
    void publishGroups(){NodeEvent event;event.kind=NodeEvent::Kind::Groups;if(groupAccount.accountId)for(const auto& item:groups){const auto& state=item.second.state();if(state.members.count(groupAccount.accountId)||state.invitations.count(groupAccount.accountId))event.groups.push_back(state);}emit(std::move(event));}
    void stop(){running=false;commandChanged.notify_all();if(worker.joinable())worker.join();std::lock_guard<std::mutex> lock(commandMutex);commands.clear();}
    bool exists(const std::string& id)const {return sessionTransports&&!id.empty()&&sessionTransports->hasRepresentation(id);}
    void resolve(const std::wstring& host,std::uint16_t port,const std::string& expected,const std::wstring& source=L"Direct",const std::wstring& route=L"manual"){
        if(host.empty()||host.size()>253||!port||(!expected.empty()&&!protocol::id(expected)))throw std::runtime_error("Invalid peer address");
        auto proposed=routeObservation(host,port,source,route,false,{nextRouteRevision});
        if(exists(expected)&&!sessionTransports->canPrepareHandoff(expected,proposed))return;
        if(connections.size()+resolvers.size()>=MaxConnections||resolvers.size()>=8)throw std::runtime_error("Too many pending connections");
        std::uint64_t attemptId=nextAttemptId++;auto observation=routeObservation(host,port,source,route,false,{nextRouteRevision++});if(sessionTransports&&!sessionTransports->registerAttempt(sessionEpoch,{attemptId},expected,observation,nowMilliseconds()))return;NodeEvent attempt;attempt.kind=NodeEvent::Kind::ConnectionAttempt;attempt.peerId=expected;attempt.expectedPeerId=expected;attempt.candidate=endpoint(host,port);attempt.candidateSource=source;attempt.route=route;attempt.attemptId=attemptId;attempt.text=L"Connection attempt started";emit(std::move(attempt));emitSessionSnapshot(expected);
        // A copied LAN IPv4 address should never depend on DNS or the async
        // resolver. This also avoids resolver differences between Windows PCs.
        IN_ADDR numeric{};
        if(InetPtonW(AF_INET,host.c_str(),&numeric)==1){
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr=numeric;
            auto connection=std::make_unique<Connection>();connection->host=host;connection->port=port;connection->expected=expected;connection->source=source;connection->route=route;connection->attemptId=attemptId;attachDirectTransport(*connection);connection->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(connection->socket==INVALID_SOCKET){emit(connectionEvent(NodeEvent::Kind::Error,L"Could not create a connection socket.",*connection));scheduleReconnect(expected,host,port,source,route,false,attemptId);return;}
            nonblocking(connection->socket);int result=::connect(connection->socket,reinterpret_cast<sockaddr*>(&address),sizeof(address));int error=result==0?0:WSAGetLastError();
            if(result==0||error==WSAEWOULDBLOCK||error==WSAEINPROGRESS){connection->connecting=result!=0;connections.push_back(std::move(connection));}
            else {tcpFailure(*connection,error);closesocket(connection->socket);connection->socket=INVALID_SOCKET;scheduleReconnect(expected,host,port,source,route,false,attemptId);}
            return;
        }
        auto r=std::make_unique<Resolver>(host,port,expected,source,route,attemptId);int status=r->begin();
        if(status!=0&&status!=WSA_IO_PENDING){Connection failed;failed.host=host;failed.port=port;failed.expected=expected;failed.source=source;failed.route=route;failed.attemptId=attemptId;emit(connectionEvent(NodeEvent::Kind::Error,L"DNS/hostname failure: could not resolve "+host+L":"+std::to_wstring(port)+L".",failed));scheduleReconnect(expected,host,port,source,route,false,attemptId);return;}
        resolvers.push_back(std::move(r));
    }
    void resolved(){
        for(auto it=resolvers.begin();it!=resolvers.end();){
            auto& r=**it;if(r.pending&&WaitForSingleObject(r.overlap.hEvent,0)!=WAIT_OBJECT_0){++it;continue;}
            int result=r.pending?GetAddrInfoExOverlappedResult(&r.overlap):0;r.pending=false;
            if(result==0&&r.result){
                auto c=std::make_unique<Connection>();c->host=r.host;c->port=r.port;c->expected=r.expected;c->source=r.source;c->route=r.route;c->attemptId=r.attemptId;attachDirectTransport(*c);c->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
                if(c->socket!=INVALID_SOCKET){nonblocking(c->socket);int n=::connect(c->socket,r.result->ai_addr,static_cast<int>(r.result->ai_addrlen));
                    if(n==0||WSAGetLastError()==WSAEWOULDBLOCK||WSAGetLastError()==WSAEINPROGRESS){c->connecting=n!=0;connections.push_back(std::move(c));}
                    else {int error=WSAGetLastError();tcpFailure(*c,error);closesocket(c->socket);c->socket=INVALID_SOCKET;scheduleReconnect(r.expected,r.host,r.port,r.source,r.route,false,r.attemptId);}
                }else {emit(connectionEvent(NodeEvent::Kind::Error,L"Could not create a connection socket.",*c));scheduleReconnect(r.expected,r.host,r.port,r.source,r.route,false,r.attemptId);}
            }else {Connection failed;failed.host=r.host;failed.port=r.port;failed.expected=r.expected;failed.source=r.source;failed.route=r.route;failed.attemptId=r.attemptId;emit(connectionEvent(NodeEvent::Kind::Error,L"DNS/hostname failure: lookup failed or timed out for "+r.host+L":"+std::to_wstring(r.port)+L".",failed));scheduleReconnect(r.expected,r.host,r.port,r.source,r.route,false,r.attemptId);}
            it=resolvers.erase(it);
        }
    }
    void collect(Connection& c){auto bytes=c.tls->takeOutput();if(bytes.empty())return;if(c.relay){auto pipe=c.relayPipe;if(!pipe)throw std::runtime_error("Relay pipe unavailable");std::lock_guard<std::mutex> lock(pipe->mutex);if(pipe->closed||pipe->queued+bytes.size()>MaxQueue)throw std::runtime_error("Relay peer is not consuming outgoing data");pipe->queued+=bytes.size();pipe->output.push_back(std::move(bytes));pipe->changed.notify_one();return;}if(c.output.size()-c.sent+bytes.size()>MaxQueue)throw std::runtime_error("Peer is not consuming outgoing data");if(c.sent){c.output.erase(c.output.begin(),c.output.begin()+c.sent);c.sent=0;}c.output.insert(c.output.end(),bytes.begin(),bytes.end());}
    void packet(Connection& c,protocol::Writer& writer){auto framed=writer.frame();c.tls->send(framed);if(c.directTransport)project(c,c.directTransport->observedSend(framed.size()));collect(c);}
    void packet(Connection& c,const Blob& value){auto framed=protocol::frame(value);c.tls->send(framed);if(c.directTransport)project(c,c.directTransport->observedSend(framed.size()));collect(c);}
    void handoffEffects(const std::vector<session::HandoffEffect>& effects){
        for(const auto& effect:effects){
            Connection* connection=nullptr;for(auto& c:connections)if(c->attemptId==effect.instance.value&&!c->dead){connection=c.get();break;}
            if(!connection)continue;
            auto& c=*connection;
            if(effect.kind==session::HandoffEffect::Kind::Send){
                if(!c.directHandoff||!c.approved||!c.remoteApproved)continue;
                protocol::Writer w(protocol::Handoff);w.number(static_cast<std::uint64_t>(effect.envelope.message));w.string(effect.envelope.transaction);w.string(effect.envelope.previous);
                try{packet(c,w);}catch(...){c.closeReason=L"handoff transport send failed";c.dead=true;}
            }else if(effect.kind==session::HandoffEffect::Kind::Retire){
                c.closeReason=widenUtf8(effect.reason);c.dead=true;
                emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,c.closeReason,c,c.peerId));
            }else{
                if(effect.kind==session::HandoffEffect::Kind::Promote)deliveries->beginDraining(effect.previous);
                deliveries->restoreTransport(effect.instance);
                if(c.directTransport)project(c,c.directTransport->activated());
                emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,widenUtf8(effect.reason),c,c.peerId));
                emitSessionSnapshot(c.peerId);outbox(c);groupOutbox(c);
            }
        }
    }
    void outbox(Connection& c){
        if(!c.online||c.dead||!c.authorization.permitsDirectMessage()||!deliveries||!sessionTransports->active({c.attemptId}))return;
        session::TransportInstanceId transport{c.attemptId};
        for(const auto& m:deliveries->pendingMessages(c.peerId,100)) {
            if(deliveries->wasSubmittedMessage(c.peerId,m.id,transport))continue;
            if(deliveries->inFlightCount(transport)>=16||c.output.size()-c.sent>MaxQueue/2)break;
            auto w=reliable::textPacket(m,c.logicalOrdering);packet(c,w);
            if(!deliveries->submittedMessage(c.peerId,m.id,transport))throw std::runtime_error("Could not record authenticated message submission");
        }
    }
    std::set<std::string> groupPeers(const GroupState& state){std::set<std::string> peers;for(const auto& member:state.members)peers.insert(member.second.peerId);for(const auto& invitation:state.invitations)peers.insert(invitation.second.peerId);peers.erase(profile.peerId);return peers;}
    void persistAccepted(GroupLedger& ledger){for(const auto& event:ledger.events())if(ledger.accepted(event.eventId))store->appendGroupEvent(event);}
    void queueGroupEvent(const GroupEvent& event,const std::set<std::string>& peers){for(const auto& peer:peers)if(protocol::id(peer)&&peer!=profile.peerId)store->queueGroupDelivery(event.eventId,peer);}
    void queueGroupSync(const GroupLedger& ledger,const std::string& peer){for(const auto& event:ledger.events())if(ledger.accepted(event.eventId))store->queueGroupDelivery(event.eventId,peer);}
    void groupOutbox(Connection& connection){
        if(!connection.online||connection.dead||!connection.authorization.permitsDirectMessage()||!connection.signedGroups||!deliveries||!sessionTransports->active({connection.attemptId}))return;
        session::TransportInstanceId transport{connection.attemptId};
        for(const auto& event:deliveries->pendingGroupEvents(connection.peerId,64)){
            if(event.kind==GroupEventKind::Message||(groupKeys&&groupKeys->isAuthoritativeGroup(event.groupId)))continue;
            if(deliveries->wasSubmittedGroupEvent(connection.peerId,event.eventId,transport))continue;
            if(deliveries->inFlightCount(transport)>=16||connection.output.size()-connection.sent>MaxQueue/2)break;
            packet(connection,encodeGroupEvent(event));
            if(!deliveries->submittedGroupEvent(connection.peerId,event.eventId,transport))throw std::runtime_error("Could not record authenticated group submission");
        }
    }
    void reconcileSignedGroupRoster(Connection& c){
        if(!c.online||c.dead||c.authorization.scope!=session::ReliableAuthorizationScope::GroupControl||!groupKeys)return;
        const auto* roster=groupKeys->roster(c.authorization.groupId);
        if(!roster||roster->generation!=c.authorization.rosterGeneration)return;
        auto found=groups.find(roster->legacyGroupId);
        if(found==groups.end()||!found->second.state().created)return;
        auto& ledger=found->second;
        if(roster->distributorPeerId==profile.peerId){
            // A server account invitation is not a signed ledger invitation.
            // Bind the newly accepted device key learned from peer TLS before
            // creating that signed event; never invent a key from discovery.
            for(const auto& member:roster->devices){
                if(member.invited||member.peerId!=c.peerId||member.accountId==groupAccount.accountId)continue;
                if(!ledger.state().members.count(member.accountId)&&!ledger.state().invitations.count(member.accountId)){
                    GroupEvent invite=localGroupEvent(ledger,GroupEventKind::Invite);
                    invite.subjectAccountId=member.accountId;invite.subjectPeerId=member.peerId;
                    invite.subjectPublicKey=c.tls->remotePublicKey();
                    invite.subjectName=std::to_wstring(member.accountId);
                    signGroupEvent(invite,profile);
                    protocol::check(ledger.append(invite));persistAccepted(ledger);publishGroups();
                }
            }
        }
        // Control events are signed and content-free. Resend the accepted
        // ledger prefix on every new scoped TLS connection; event IDs make
        // reconnect and duplicate delivery idempotent without a second queue.
        for(const auto& event:ledger.events()){
            if(!ledger.accepted(event.eventId)||event.kind==GroupEventKind::Message||!event.message.empty()||
               (roster->distributorPeerId!=profile.peerId&&event.actorPeerId!=profile.peerId)||
               !c.sentGroupRosterEvents.insert(event.eventId).second)continue;
            protocol::Writer control(protocol::GroupRosterEvent);control.bytes(encodeGroupEvent(event));packet(c,control);
        }
        if(roster->distributorPeerId!=profile.peerId||!groupKeys->ensureCurrentKey(roster->serverGroupId,ledger.state()))return;
        for(const auto& offer:deliveries->pendingGroupKeyHandoffs(c.peerId,64)){
            if(!groupKeys->maySend(offer,c.peerId)||offer.serverGroupId!=roster->serverGroupId||
               deliveries->wasSubmittedGroupKeyHandoff(c.peerId,offer.id,{c.attemptId}))continue;
            if(deliveries->inFlightCount({c.attemptId})>=16||c.output.size()-c.sent>MaxQueue/2)break;
            packet(c,group_keys::offerPacket(offer));
            if(!deliveries->submittedGroupKeyHandoff(c.peerId,offer.id,{c.attemptId}))
                throw std::runtime_error("Could not record authenticated group key offer");
            emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"Group key offer sent; awaiting ACK",c,c.peerId));
        }
    }
    void online(Connection& c){
        if(c.online||!c.approved||!c.remoteApproved)return;
        if(!sessionTransports)return;
        auto decision=sessionTransports->arbitrateWinner(sessionEpoch,{c.attemptId},nowMilliseconds());
        if(decision.disposition==session::WinnerDisposition::AwaitingHandoff){
            c.online=true;
            if(!deliveries->authorizeTransport({c.attemptId},c.peerId))throw std::runtime_error("Could not authorize candidate delivery transport");
            handoffEffects(sessionTransports->attachHandoff(sessionEpoch,{c.attemptId},Storage::newMessageId(),nowMilliseconds()));
            emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,widenUtf8(decision.reasonUtf8),c,c.peerId));return;
        }
        if(decision.disposition==session::WinnerDisposition::VerifiedStandby){
            // TLS is fully authenticated, but durable delivery remains bound
            // exclusively to the relay winner until explicit promotion.
            c.online=true;
            emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,widenUtf8(decision.reasonUtf8),c,c.peerId));
            emitSessionSnapshot(c.peerId);return;
        }
        if(!decision.activated()){
            c.closeReason=widenUtf8(decision.reasonUtf8);c.dead=true;emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,c.closeReason,c,c.peerId));emitSessionSnapshot(c.peerId);return;
        }
        if(c.authorization.scope==session::ReliableAuthorizationScope::ArchiveControl){
            protocol::check(archiveApplication&&archiveExecution&&archive::currentSessionAuthorization(archiveApplication->policy(),c.authorization));
            c.online=true;
            for(auto loser:decision.retire)for(auto& other:connections)if(other->attemptId==loser.value&&!other->dead){other->closeReason=L"Superseded archive route";other->dead=true;}
            auto bind=archive::sessionBinding(c.authorization);packet(c,bind);
            emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"Authenticated archive route; awaiting scope binding",c,c.peerId));
            return;
        }
        if(c.authorization.scope==session::ReliableAuthorizationScope::GroupControl){
            // Group-control may use the existing peer-TLS relay, but it must
            // never authorize the ordinary DM/group-message outbox or media.
            for(auto loser:decision.retire)for(auto& other:connections)if(other->attemptId==loser.value&&!other->dead){other->closeReason=L"superseded group-control relay";other->dead=true;}
            if(!deliveries||!deliveries->authorizeTransport({c.attemptId},c.peerId))throw std::runtime_error("Could not bind group-key delivery to the authenticated device");
            c.online=true;
            emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"Authenticated group-control relay ready",c,c.peerId));
            emitSessionSnapshot(c.authorization.continuityKey());
            reconcileSignedGroupRoster(c);
            return;
        }
        for(auto loser:decision.retire)for(auto& other:connections)if(other->attemptId==loser.value&&!other->dead){if(deliveries)deliveries->beginDraining(loser);other->closeReason=widenUtf8(decision.reasonUtf8);other->dead=true;emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,other->closeReason,*other,other->peerId));}
        c.online=true;if(!deliveries||!deliveries->authorizeTransport({c.attemptId},c.peerId))throw std::runtime_error("Could not bind delivery transport to authenticated peer");if(c.directTransport)project(c,c.directTransport->activated());emit(connectionEvent(NodeEvent::Kind::Connected,c.name,c,c.peerId));emitSessionSnapshot(c.peerId);if(auto active=calls.active())if(calls.peer()==c.peerId&&!active->controlAvailable){calls.observeControl(true);voiceEvent(active->state,active->mediaState==calls::MediaState::Connected?L"Call control restored":L"Call control restored · Reconnecting voice");}
        if(c.directHandoff)handoffEffects(sessionTransports->attachHandoff(sessionEpoch,{c.attemptId},Storage::newMessageId(),nowMilliseconds()));
        outbox(c);groupOutbox(c);
    }
    void approve(Connection& c){
        c.locallyTrusted();if(c.directTransport){project(c,c.directTransport->locallyTrusted());if(sessionTransports)sessionTransports->markLocallyTrusted(sessionEpoch,{c.attemptId});}protocol::Writer ready(protocol::Accepted);packet(c,ready);online(c);
    }
    void process(Connection& c,const Blob& frame){
        protocol::Reader r(frame);
        auto kind=c.enter(r,c.relay,c.tls->remotePublicKey(),profile.peerId);
        if(kind==reliable::SharedPeerRuntime::FrameKind::Hello){
            // The shared runtime parsed Hello; recover the advertised bitset for
            // existing group/route observers without parsing the frame again.
            auto capabilities=c.remoteCapabilities;
            if(c.host.empty()&&!c.relay)c.host=observedAddress(c.socket);
            if(c.directTransport){c.directTransport->updateRoute(routeObservation(c,c.directTransport->routeRevision()));project(c,c.directTransport->identityPresented(c.peerId,capabilities));}
            if(c.authorization.scope==session::ReliableAuthorizationScope::ArchiveControl){
                auto key=c.tls->remotePublicKey();
                protocol::check(c.relay&&archiveApplication&&archiveExecution&&(capabilities&protocol::CapabilityArchiveControl)&&
                    !c.identityMismatch&&c.peerId==c.authorization.expectedRemoteDeviceId&&
                    archive::currentSessionAuthorization(archiveApplication->policy(),c.authorization,&key));
                approve(c);return; // Never create or update ordinary friend trust.
            }
            if(c.authorization.scope==session::ReliableAuthorizationScope::GroupControl){
                protocol::check(c.relay&&(capabilities&protocol::CapabilityGroupKeyHandoff)!=0&&c.authorization.valid()&&c.peerId==c.authorization.expectedRemoteDeviceId);
                const auto* roster=groupKeys?groupKeys->roster(c.authorization.groupId):nullptr;
                protocol::check(roster&&roster->generation==c.authorization.rosterGeneration&&
                    group_keys::GroupKeyManager::currentMember(*roster,groupAccount.accountId,profile.peerId)&&
                    std::any_of(roster->devices.begin(),roster->devices.end(),[&](const auto& member){return !member.invited&&member.peerId==c.peerId;}));
                // TLS has already proved the presented public key hashes to this
                // expected Peer ID. A historical friend pin, if any, still wins;
                // group membership must never replace or create ordinary trust.
                auto pinned=contacts.find(c.peerId);
                protocol::check(pinned==contacts.end()||!pinned->second.verified||pinned->second.publicKey==c.tls->remotePublicKey());
                approve(c);return;
            }
            // A changed identity is a security-relevant verification event, not a silent
            // transport rejection. The UI must show the expected and presented public IDs and
            // require an explicit decision. No private key, password, or message content is
            // included. The presented ID becomes trusted only if approve() is later called.
            if(c.identityMismatch){
                // Suspend as soon as identity mismatch is known. Waiting for dead
                // socket cleanup permits a queued retry to race a rejection.
                if(sessionTransports)sessionTransports->suspendRetry(c.expected);
                NodeEvent changed=connectionEvent(NodeEvent::Kind::TrustRequired,L"",c,c.peerId);
                changed.text=L"Peer identity changed. Expected "+std::wstring(c.expected.begin(),c.expected.end())+L" but the peer presented "+std::wstring(c.peerId.begin(),c.peerId.end())+L". Verify this new device identity before trusting it.";
                changed.contacts.push_back({c.peerId,c.name,c.tls->remotePublicKey(),c.relay?std::vector<Endpoint>{}:std::vector<Endpoint>{{c.host,c.port}},false});emit(std::move(changed));return;
            }
            auto known=contacts.find(c.peerId);
            if(c.verifiedContactMatches(known==contacts.end()?nullptr:&known->second,c.tls->remotePublicKey())){
                auto contact=reliable::persistAuthenticatedContact(*store,c,&known->second,c.tls->remotePublicKey(),c.relay?std::optional<Endpoint>{}:Endpoint{c.host,c.port});
                contacts[c.peerId]=contact;publishContacts();approve(c);
            }else{NodeEvent e=connectionEvent(NodeEvent::Kind::TrustRequired,c.name,c,c.peerId);e.contacts.push_back({c.peerId,c.name,c.tls->remotePublicKey(),c.relay?std::vector<Endpoint>{}:std::vector<Endpoint>{{c.host,c.port}},false});emit(std::move(e));}
            return;
        }
        if(kind==reliable::SharedPeerRuntime::FrameKind::Accepted){if(sessionTransports)sessionTransports->markRemoteAccepted(sessionEpoch,{c.attemptId});online(c);return;}
        c.requireOnline(c.online);
        if(c.authorization.scope==session::ReliableAuthorizationScope::ArchiveControl){
            protocol::check(archiveApplication&&archiveExecution&&archive::currentSessionAuthorization(archiveApplication->policy(),c.authorization));
            if(!c.archiveBound){archive::verifySessionBinding(r,c.authorization);c.archiveBound=true;return;}
            {protocol::Reader control(frame);if(control.type()==protocol::ArchiveControl&&control.string()==archive::AttachmentDomain){
                protocol::check(control.number()==1);auto kind=control.number();if(kind>=3){prepareArchiveTransfers();protocol::check(bool(archiveTransfers));archiveTransfers->control(c.authorization,frame,nowMilliseconds());return;}
            }}
            auto utc=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            auto response=archiveExecution->control(c.authorization,frame,utc,nowMilliseconds());
            if(!response.empty()){packet(c,response);archiveExecution->sourceQueued(c.authorization,response);}
            return;
        }
        if(c.authorization.scope==session::ReliableAuthorizationScope::GroupControl){
            protocol::check(groupKeys&&c.relay&&c.peerId==c.authorization.expectedRemoteDeviceId);
            const auto* roster=groupKeys->roster(c.authorization.groupId);
            protocol::check(roster&&roster->generation==c.authorization.rosterGeneration&&
                group_keys::GroupKeyManager::currentMember(*roster,groupAccount.accountId,profile.peerId));
            if(r.type()==protocol::ArchiveControl){
                protocol::check((c.remoteCapabilities&protocol::CapabilityArchiveGroups)!=0);ensureGroupHistory(c);protocol::check(bool(c.groupHistory));
                auto response=c.groupHistory->control(frame,nowMilliseconds());if(!response.empty())packet(c,response);return;
            }
            if(r.type()==protocol::GroupRosterEvent){
                Blob signedFrame=r.bytes();r.end();
                GroupEvent event=decodeGroupEvent(signedFrame);
                protocol::check(event.groupId==roster->legacyGroupId&&event.kind!=GroupEventKind::Message&&event.message.empty());
                // Only the current distributor may replay another device's
                // signed control event. A non-distributor may submit its own.
                protocol::check(c.peerId==roster->distributorPeerId||event.actorPeerId==c.peerId);
                auto found=groups.find(event.groupId);
                if(found==groups.end()){
                    protocol::check(event.kind==GroupEventKind::Create&&c.peerId==roster->distributorPeerId);
                    if(!roster->creationProof.empty())protocol::check(encodeGroupEvent(event)==roster->creationProof);
                    else protocol::check(std::any_of(roster->devices.begin(),roster->devices.end(),[&](const auto& member){return member.accountId==event.actorAccountId&&member.peerId==event.actorPeerId&&member.role==GroupRole::Owner&&!member.invited;}));
                    found=groups.emplace(event.groupId,GroupLedger{}).first;
                }
                auto& ledger=found->second;
                protocol::check(ledger.append(event));persistAccepted(ledger);
                if(event.kind==GroupEventKind::Invite&&event.subjectAccountId==groupAccount.accountId&&
                   event.subjectPeerId==profile.peerId&&
                   group_keys::GroupKeyManager::currentMember(*roster,groupAccount.accountId,profile.peerId)&&
                   ledger.state().invitations.count(groupAccount.accountId)){
                    protocol::check(event.subjectPublicKey==profile.publicKey);
                    GroupEvent accepted=localGroupEvent(ledger,GroupEventKind::Accept);
                    accepted.subjectAccountId=groupAccount.accountId;accepted.subjectPeerId=profile.peerId;
                    signGroupEvent(accepted,profile);protocol::check(ledger.append(accepted));persistAccepted(ledger);
                }
                publishGroups();
                for(auto& active:connections)if(active->online&&!active->dead&&
                    active->authorization.scope==session::ReliableAuthorizationScope::GroupControl&&
                    active->authorization.groupId==roster->serverGroupId)
                    reconcileSignedGroupRoster(*active);
                return;
            }
            if(r.type()==protocol::GroupKeyOffer){
                auto offer=group_keys::readOffer(frame);
                auto found=groups.find(roster->legacyGroupId);
                protocol::check(found!=groups.end()&&groupKeys->accept(offer,c.peerId,found->second.state()));
                group_keys::Ack ack{offer.id,offer.serverGroupId,offer.rosterGeneration,offer.keyGeneration};
                packet(c,group_keys::ackPacket(ack));
                emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"Group key handoff acknowledged",c,c.peerId));
                return;
            }
            if(r.type()==protocol::GroupKeyAck){
                auto ack=group_keys::readAck(frame);
                protocol::check(ack.serverGroupId==roster->serverGroupId&&ack.rosterGeneration==roster->generation&&
                    ack.keyGeneration==roster->generation);
                if(deliveries->acknowledgeGroupKeyHandoff(c.peerId,{c.attemptId},ack.handoffId)){
                    emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"Group key offer acknowledged by member",c,c.peerId));
                    reconcileSignedGroupRoster(c);
                }
                return;
            }
            throw std::runtime_error("Group-control session rejected an unrelated payload");
        }
        if(r.type()==protocol::FileTransfer){
            if(!transfers)return; // Local journal failure must not retire messaging.
            protocol::check(transferConnection(c.peerId)==&c);
            auto before=transfers->snapshots().size();
            try{auto began=transfer::windows::TransferTiming::now();transfers->capability(c.peerId,c.attemptId,(c.remoteCapabilities&protocol::CapabilityTransferPipeline)!=0);transfers->receive(c.peerId,frame,nowMilliseconds());transferTiming.received(frame,c.attemptId,began,transfer::windows::TransferTiming::now());}
            catch(const transfer::CapacityError& error){protocol::Writer refusal(protocol::FileTransfer);refusal.data=error.cancellation();packet(c,refusal);return;}
            catch(const transfer::PersistenceError&){transferPersistenceFailure();return;}
            if(transfers->snapshots().size()>before){NodeEvent event;event.kind=NodeEvent::Kind::FileTransfer;event.peerId=c.peerId;event.text=L"Incoming file offer";emit(std::move(event));}
            return;
        }
        if(r.type()==protocol::ScreenBackendRequest){
            const auto id=r.string();const auto generation=r.number(),rawRequirement=r.number();r.end();
            protocol::check(c.screenShare&&c.screenBackendNegotiation&&protocol::id(id,32)&&generation==1&&rawRequirement<=1);
            const auto remoteRequirement=static_cast<screen::Backend>(rawRequirement);
            auto active=screenShares.active();
            const auto localRequirement=active&&!active->sender&&screenShares.matches(id,c.peerId)
                ? active->localRequirement : configuredScreenBackend;
            const auto selected=screen::negotiateBackend(localRequirement,remoteRequirement);
            bool accepted=ScreenShareMedia::supportsBackend(selected,false)&&
                (!active||(!active->sender&&screenShares.matches(id,c.peerId)&&active->backendNegotiated&&active->backend==selected));
            if(accepted&&!active){
                accepted=screenShares.start(id,c.peerId,false,screen::State::Incoming,nowMilliseconds()+30000,localRequirement);
                if(accepted){auto share=screenShares.active();share->peerRequirement=remoteRequirement;share->backend=selected;share->backendNegotiated=true;}
            }
            protocol::Writer response(protocol::ScreenBackendSelection);response.string(id);response.number(generation);
            response.number(static_cast<std::uint64_t>(localRequirement));
            response.number(accepted?static_cast<std::uint64_t>(selected):2);packet(c,response);
            return;
        }
        if(r.type()==protocol::ScreenBackendSelection){
            const auto id=r.string();const auto generation=r.number(),rawRequirement=r.number(),rawSelected=r.number();r.end();
            protocol::check(c.screenShare&&c.screenBackendNegotiation&&protocol::id(id,32)&&generation==1&&rawRequirement<=1&&rawSelected<=2);
            auto active=screenShares.active();
            if(!active||!active->sender||!screenShares.matchesMedia(active->lifetime,id,c.peerId,generation)||active->backendNegotiated)return;
            const auto peerRequirement=static_cast<screen::Backend>(rawRequirement);
            const auto selected=screen::negotiateBackend(active->localRequirement,peerRequirement);
            if(rawSelected==2||rawSelected!=static_cast<std::uint64_t>(selected)||!ScreenShareMedia::supportsBackend(selected,true)){
                if(rawSelected!=2){protocol::Writer decline(protocol::ScreenDecline);decline.string(id);packet(c,decline);}
                clearScreen(L"No common supported screen-share backend");return;
            }
            active->peerRequirement=peerRequirement;active->backend=selected;active->backendNegotiated=true;
            if(!configureScreenMedia()){protocol::Writer decline(protocol::ScreenDecline);decline.string(id);packet(c,decline);clearScreen(L"TURN media credentials are unavailable or expiring. Messaging remains connected.");return;}
            std::string error;if(!screenMedia->startOffer(error)){protocol::Writer decline(protocol::ScreenDecline);decline.string(id);packet(c,decline);clearScreen(widenUtf8(error));return;}
            screenEvent(L"Starting screen share");return;
        }
        if(r.type()==protocol::ScreenOffer){
            auto id=r.string();auto generation=r.number();auto sdp=r.string();r.end();
            protocol::check(c.screenShare&&protocol::id(id,32)&&generation>=1&&generation<=1000000&&!sdp.empty()&&sdp.size()<=32768);
            auto active=screenShares.active();
            if(active&&screenShares.matches(id,c.peerId)){
                if(!active->backendNegotiated)return;
                if(generation==active->generation){
                    if(!active->sender&&active->state==screen::State::Incoming&&active->remoteOffer.empty()){
                        active->remoteOffer=std::move(sdp);screenPriorSentBytes=screenPriorReceivedBytes=0;
                        screenEvent(L"Incoming screen share");
                    }
                    return;
                }
                if(!active->sender&&active->state!=screen::State::Incoming&&screenShares.acceptGeneration(generation,nowMilliseconds()+MediaRecoveryWindowMilliseconds)){
                    stopScreenMedia();if(!configureScreenMedia()){interruptScreen(L"TURN media credentials are unavailable · waiting to recover");return;}std::string error;
                    if(!screenMedia->startAnswer(sdp,error))interruptScreen(L"Could not restart secure screen media");
                    else screenEvent(L"Screen media reconnecting");
                }
                return;
            }
            // An offer without a completed backend selection cannot start media.
            protocol::Writer decline(protocol::ScreenDecline);decline.string(id);packet(c,decline);return;
        }
        if(r.type()==protocol::ScreenAnswer){
            auto id=r.string();auto generation=r.number();auto sdp=r.string();r.end();
            protocol::check(c.screenShare&&protocol::id(id,32)&&generation>=1&&generation<=1000000&&!sdp.empty()&&sdp.size()<=32768);
            auto active=screenShares.active();
            if(active&&active->sender&&screenShares.matches(id,c.peerId)&&active->generation==generation&&screenMedia){
                if(!active->backendNegotiated)return;
                if(!screenMedia->setAnswer(sdp))interruptScreen(L"Could not accept secure screen answer");
                else{screenShares.transition(screen::State::Connecting,nowMilliseconds()+20000);screenEvent(L"Connecting screen media");}
            }return;
        }
        if(r.type()==protocol::ScreenCandidate){
            auto id=r.string();auto generation=r.number();auto candidate=r.string();auto mid=r.string();r.end();
            protocol::check(c.screenShare&&protocol::id(id,32)&&generation>=1&&generation<=1000000&&candidate.size()<=4096&&mid.size()<=64);
            auto active=screenShares.active();
            if(active&&screenShares.matches(id,c.peerId)&&active->generation==generation){
                if(!active->backendNegotiated)return;
                if(screenMedia)screenMedia->addCandidate(candidate,mid);
                else screenShares.queueCandidate(std::move(candidate),std::move(mid));
            }return;
        }
        if(r.type()==protocol::ScreenRecoveryRequest){
            auto id=r.string();auto generation=r.number();r.end();
            protocol::check(c.screenShare&&protocol::id(id,32)&&generation>=1&&generation<=1000000);
            auto active=screenShares.active();
            if(active&&active->sender&&screenShares.matches(id,c.peerId)&&
                generation>=active->generation&&nowMilliseconds()>=screenLastRestartAt+1000){
                if(active->state==screen::State::Connected){interruptScreen(L"Peer requested screen-media recovery");screenRetryAt=nowMilliseconds();}
                else if(active->state==screen::State::Recovering&&!screenMedia)screenRetryAt=nowMilliseconds();
            }
            return;
        }
        if(r.type()==protocol::ScreenEnd||r.type()==protocol::ScreenDecline){
            auto type=r.type();auto id=r.string();r.end();protocol::check(c.screenShare&&protocol::id(id,32));
            if(screenShares.matches(id,c.peerId))clearScreen(type==protocol::ScreenDecline?L"Screen share declined":L"Screen share ended");
            return;
        }
        if(r.type()==protocol::Handoff){
            protocol::check(c.directHandoff&&c.approved&&c.remoteApproved);
            if(c.identityMismatch)return; // explicit device re-verification is not route continuity
            auto phase=r.number();auto transaction=r.string();auto previous=r.string();r.end();
            protocol::check(phase<=static_cast<std::uint64_t>(session::HandoffMessage::Confirm)&&transaction.size()<=64&&previous.size()<=64);
            auto known=contacts.find(c.peerId);protocol::check(known!=contacts.end()&&known->second.verified&&known->second.publicKey==c.tls->remotePublicKey());
            handoffEffects(sessionTransports->receiveHandoff(sessionEpoch,{c.attemptId},{static_cast<session::HandoffMessage>(phase),transaction,previous},nowMilliseconds()));return;
        }
        if(r.type()==protocol::CallOffer){
            auto callId=r.string();auto sdp=r.string();r.end();protocol::check(c.voiceCalls&&protocol::id(callId,32)&&!sdp.empty()&&sdp.size()<=32768);
            auto previous=calls.active()?calls.active()->id.value:std::string{};auto disposition=calls.beginIncoming({callId},calls::CallType::Voice,c.peerId,sdp,profile.peerId,nowMilliseconds()+static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(VoiceRingTimeout).count()));
            if(disposition==calls::IncomingDisposition::Replay||disposition==calls::IncomingDisposition::Duplicate)return;
            if(disposition==calls::IncomingDisposition::Busy){protocol::Writer busy(protocol::CallBusy);busy.string(callId);packet(c,busy);return;}
            if(disposition==calls::IncomingDisposition::ReplaceOutgoing){protocol::Writer end(protocol::CallEnd);end.string(previous);packet(c,end);if(voiceMedia)voiceMedia->stop();voiceMedia.reset();voiceMediaLifetime=0;}
            calls.setMediaRecoveryCapable(c.peerId,c.mediaRecovery);calls.beginInitialMedia(callId);resetVoiceSignaling(&c);voiceRequestReceivedOn=voiceOfferReceivedOn=voiceLastSignalConnection=c.attemptId;voiceEvent(calls::State::Incoming,disposition==calls::IncomingDisposition::ReplaceOutgoing?L"simultaneous call resolved":L"");return;
        }
        if(r.type()==protocol::CallAnswer){auto callId=r.string();auto sdp=r.string();r.end();protocol::check(protocol::id(callId,32)&&!sdp.empty()&&sdp.size()<=32768);auto active=calls.active();if(active&&active->state==calls::State::Outgoing&&active->direction==calls::Direction::Outgoing&&calls.peer()==c.peerId&&active->id.value==callId&&voiceMedia){voiceAnswerReceivedOn=voiceLastSignalConnection=c.attemptId;std::wstring error;if(!voiceMedia->setAnswer(sdp,error)){clearVoice(error,true);return;}calls.transition(callId,calls::State::ConnectingMedia,nowMilliseconds()+static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(VoiceConnectTimeout).count()));voiceEvent(calls::State::ConnectingMedia);}return;}
        if(r.type()==protocol::CallCandidate){auto callId=r.string();auto candidate=r.string();auto mid=r.string();r.end();protocol::check(protocol::id(callId,32)&&candidate.size()<=4096&&mid.size()<=64);auto active=calls.active();if(active&&active->id.value==callId&&active->participants.count(c.peerId)){voiceLastSignalConnection=c.attemptId;++voiceCandidatesReceived;if(voiceMedia){std::wstring error;if(!voiceMedia->addCandidate(candidate,mid,error))notice(NodeEvent::Kind::Error,error,c.peerId);}else{auto& pending=active->participants[c.peerId].candidates;if(pending.size()<64)pending.emplace_back(std::move(candidate),std::move(mid));}}return;}
        if(r.type()==protocol::CallMediaRecoveryRequest){auto callId=r.string();auto knownGeneration=r.number();r.end();protocol::check(c.mediaRecovery&&protocol::id(callId,32));auto active=calls.active();if(active&&active->id.value==callId&&calls.peer()==c.peerId&&profile.peerId<c.peerId){if(active->mediaState==calls::MediaState::Connected)interruptVoiceMedia(L"Peer requested media recovery");if(auto current=calls.active())if(current->mediaState==calls::MediaState::Interrupted&&knownGeneration>=current->mediaGeneration)calls.deferMediaRetry(nowMilliseconds(),0);}return;}
        if(r.type()==protocol::CallMediaRestart){auto callId=r.string();auto generation=r.number();auto sdp=r.string();r.end();protocol::check(c.mediaRecovery&&protocol::id(callId,32)&&generation>1&&!sdp.empty()&&sdp.size()<=32768);auto active=calls.active();if(!active||active->id.value!=callId||calls.peer()!=c.peerId||!calls.acceptMediaGeneration(generation,nowMilliseconds()+MediaRecoveryWindowMilliseconds))return;stopVoiceMedia();if(!configureVoiceMedia(generation)){calls.deferMediaRetry(nowMilliseconds(),1000);voiceEvent(active->state,L"TURN media credentials are unavailable · waiting to recover");return;}std::wstring error;if(!voiceMedia->startAnswer(sdp,error)){stopVoiceMedia();calls.deferMediaRetry(nowMilliseconds(),1000);voiceEvent(active->state,L"Voice connection interrupted · Reconnecting…");return;}voiceEvent(active->state,L"Voice connection interrupted · Reconnecting…");return;}
        if(r.type()==protocol::CallMediaAnswer){auto callId=r.string();auto generation=r.number();auto sdp=r.string();r.end();protocol::check(c.mediaRecovery&&protocol::id(callId,32)&&generation>1&&!sdp.empty()&&sdp.size()<=32768);auto active=calls.active();if(active&&active->id.value==callId&&calls.peer()==c.peerId&&voiceMedia&&calls.matchesMedia(active->lifetime,callId,c.peerId,generation)){std::wstring error;if(!voiceMedia->setAnswer(sdp,error)){interruptVoiceMedia(error);}}return;}
        if(r.type()==protocol::CallMediaCandidate){auto callId=r.string();auto generation=r.number();auto candidate=r.string();auto mid=r.string();r.end();protocol::check(c.mediaRecovery&&protocol::id(callId,32)&&generation>1&&candidate.size()<=4096&&mid.size()<=64);auto active=calls.active();if(active&&voiceMedia&&calls.matchesMedia(active->lifetime,callId,c.peerId,generation)){std::wstring error;if(!voiceMedia->addCandidate(candidate,mid,error))notice(NodeEvent::Kind::Error,error,c.peerId);}return;}
        if(r.type()==protocol::CallConnected){auto callId=r.string();r.end();protocol::check(protocol::id(callId,32));return;}
        if(r.type()==protocol::CallDecline||r.type()==protocol::CallBusy||r.type()==protocol::CallEnd){auto type=r.type();auto callId=r.string();r.end();protocol::check(protocol::id(callId,32));auto active=calls.active();if(active&&active->id.value==callId&&active->participants.count(c.peerId))clearVoice(type==protocol::CallDecline?L"Declined":type==protocol::CallBusy?L"Busy":L"Remote ended the call");return;}
        if(r.type()==protocol::Text){
            if(!deliveries)throw std::runtime_error("Delivery owner is unavailable");
            auto received=reliable::receiveText(r,c.logicalOrdering,c.peerId,profile,*store,*deliveries,groupAccount.accountId);
            if(!received.message){notice(NodeEvent::Kind::Error,L"Oversized chat message rejected",c.peerId);return;}
            if(received.inserted){NodeEvent e;e.kind=NodeEvent::Kind::Message;e.peerId=c.peerId;e.messages.push_back(*received.message);emit(std::move(e));}
            protocol::Writer ack(protocol::Ack);ack.string(received.messageId);packet(c,ack);
        }else if(r.type()==protocol::Ack){
            auto id=reliable::receiveAck(r);
            // A peer can acknowledge only an ID actually sent on this connection.
            if(deliveries&&deliveries->acknowledgeMessage(c.peerId,{c.attemptId},id)){NodeEvent e;e.kind=NodeEvent::Kind::Delivered;e.peerId=c.peerId;StoredMessage m;m.id=id;m.delivery=Delivery::Delivered;e.messages.push_back(m);emit(std::move(e));outbox(c);}
        }else if(r.type()==protocol::GroupEvent){
            bool oversizedMessage=false;auto event=decodeGroupEvent(frame,true,&oversizedMessage);protocol::check(protocol::id(event.groupId,32)&&protocol::id(event.eventId));if(oversizedMessage){notice(NodeEvent::Kind::Error,L"Oversized group message rejected",c.peerId);return;}
            if(event.kind==GroupEventKind::Message||(groupKeys&&groupKeys->isAuthoritativeGroup(event.groupId))){
                notice(NodeEvent::Kind::Error,L"Legacy group payload rejected; encrypted group text is not yet available",c.peerId);return;
            }
            auto found=groups.find(event.groupId);
            if(found==groups.end()){
                // An unknown group enters local state only through its valid signed creation
                // event. Invitations and later events may then arrive in any order.
                if(event.kind!=GroupEventKind::Create)return;
                GroupLedger candidate;if(!candidate.append(event))return;
                found=groups.emplace(event.groupId,std::move(candidate)).first;
            }
            auto& ledger=found->second;std::set<std::string> acceptedBefore,messageBefore;
            for(const auto& known:ledger.events()){if(ledger.accepted(known.eventId))acceptedBefore.insert(known.eventId);if(ledger.accepted(known.eventId)&&known.kind==GroupEventKind::Message)messageBefore.insert(known.eventId);}
            bool accepted=ledger.append(event);if(!accepted)return;
            persistAccepted(ledger);auto peers=groupPeers(ledger.state());peers.erase(c.peerId);bool acknowledgedCurrent=false;
            for(const auto& newlyAccepted:ledger.events())if(ledger.accepted(newlyAccepted.eventId)&&!acceptedBefore.count(newlyAccepted.eventId)){queueGroupEvent(newlyAccepted,peers);protocol::Writer ack(protocol::GroupAck);ack.string(newlyAccepted.eventId);packet(c,ack);if(newlyAccepted.eventId==event.eventId)acknowledgedCurrent=true;}
            if(!acknowledgedCurrent){protocol::Writer ack(protocol::GroupAck);ack.string(event.eventId);packet(c,ack);}
            publishGroups();for(const auto& messageEvent:ledger.state().messages)if(!messageBefore.count(messageEvent.eventId)){NodeEvent message;message.kind=NodeEvent::Kind::GroupMessage;message.peerId=event.groupId;message.groupEvents.push_back(messageEvent);emit(std::move(message));}
            for(auto& connection:connections)if(connection->online&&!connection->dead)groupOutbox(*connection);
        }else if(r.type()==protocol::GroupAck){
            auto eventId=r.string();r.end();protocol::check(protocol::id(eventId));if(deliveries&&deliveries->acknowledgeGroupEvent(c.peerId,{c.attemptId},eventId)){NodeEvent delivered;delivered.kind=NodeEvent::Kind::GroupDelivered;delivered.peerId=eventId;emit(std::move(delivered));groupOutbox(c);}
        }else throw std::runtime_error("Unknown peer packet type");
    }
    void receiveEncrypted(Connection& c,const Blob& encrypted){
        c.lastProgress=Clock::now();if(sessionTransports)sessionTransports->observeProgress(sessionEpoch,{c.attemptId},nowMilliseconds());
        auto capabilities=protocol::CapabilityMutualTls|protocol::CapabilityLogicalOrdering|protocol::CapabilitySignedGroups|protocol::CapabilityDirectHandoff;
        // Voice capability describes authenticated call control. The media
        // path remains independent ICE/DTLS-SRTP and is never put on relay.
        capabilities|=protocol::CapabilityVoiceCalls|protocol::CapabilityMediaRecovery;
        capabilities|=protocol::CapabilityGroupKeyHandoff|protocol::CapabilityArchiveControl|protocol::CapabilityArchiveDeferred|protocol::CapabilityArchiveGroups;if(transfers)capabilities|=protocol::CapabilityFileTransfer|protocol::CapabilityTransferPipeline;
        if(screenAvailable)capabilities|=protocol::CapabilityScreenShare|protocol::CapabilityScreenBackendNegotiation;
        if(!c.incoming&&sessionTransports->handoffCandidate({c.attemptId}))capabilities|=protocol::CapabilityHandoffCandidate;
        bool live=c.reliable::SharedPeerRuntime::receive(*c.tls,encrypted,profile,capabilities,profile.listenPort,
            [&]{return !c.dead;},[&]{collect(c);},[&](protocol::Writer& value){packet(c,value);},[&](const Blob& frame){
            if(c.directTransport)project(c,c.directTransport->observedReceive(frame.size()));
            try{process(c,frame);}catch(...){
                if(c.authorization.scope==session::ReliableAuthorizationScope::GroupControl&&frame.size()>=4)
                    emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,
                        L"Group-control packet rejected: type "+std::to_wstring(frame[3])+
                        (c.hello?L" after Hello":L" before Hello"),c,c.peerId.empty()?c.expected:c.peerId));
                throw;
            }
        });
        if(!live){c.closeReason=L"remote TLS close";c.dead=true;}
    }
    void receive(Connection& c){
        unsigned char buffer[8192];int n=recv(c.socket,reinterpret_cast<char*>(buffer),sizeof(buffer),0);
        if(n==0){c.closeReason=L"remote EOF";c.dead=true;return;}if(n<0){int error=WSAGetLastError();if(error!=WSAEWOULDBLOCK){c.closeReason=L"socket receive error "+std::to_wstring(error);c.dead=true;}return;}
        receiveEncrypted(c,Blob(buffer,buffer+n));
    }
    void flush(Connection& c){if(c.sent>=c.output.size())return;int n=::send(c.socket,reinterpret_cast<const char*>(c.output.data()+c.sent),static_cast<int>(std::min<size_t>(16384,c.output.size()-c.sent)),0);if(n>0){c.sent+=n;c.lastProgress=Clock::now();if(sessionTransports)sessionTransports->observeProgress(sessionEpoch,{c.attemptId},nowMilliseconds());if(c.sent==c.output.size()){c.output.clear();c.sent=0;}}else if(n==0){c.closeReason=L"socket send returned zero";c.dead=true;}else {int error=WSAGetLastError();if(error!=WSAEWOULDBLOCK){c.closeReason=L"socket send error "+std::to_wstring(error);c.dead=true;}}}
    GroupEvent localGroupEvent(GroupLedger& ledger,GroupEventKind kind){
        if(!groupAccount.accountId)throw std::runtime_error("Sign in before changing a group");
        GroupEvent event;event.groupId=ledger.state().groupId;event.kind=kind;event.logicalOrder=ledger.state().maxLogicalOrder+1;event.createdMilliseconds=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());event.actorAccountId=groupAccount.accountId;event.actorPeerId=profile.peerId;return event;
    }
    void commitLocalGroupEvent(GroupLedger& ledger,GroupEvent event,const std::set<std::string>& beforePeers,bool synchronizeInvite=false){
        const auto* authorized=groupKeys?groupKeys->rosterForLegacy(event.groupId):nullptr;
        signGroupEvent(event,profile);if(!ledger.append(event))throw std::runtime_error("Group change is not authorized by the current signed state");persistAccepted(ledger);
        if(!authorized){auto peers=beforePeers;auto after=groupPeers(ledger.state());peers.insert(after.begin(),after.end());queueGroupEvent(event,peers);}
        if(!authorized&&synchronizeInvite&&!event.subjectPeerId.empty())queueGroupSync(ledger,event.subjectPeerId);
        if(event.kind==GroupEventKind::Message){NodeEvent message;message.kind=NodeEvent::Kind::GroupMessage;message.peerId=event.groupId;message.groupEvents.push_back(event);emit(std::move(message));}
        publishGroups();
        if(authorized){for(auto& connection:connections)if(connection->online&&!connection->dead&&connection->authorization.groupId==authorized->serverGroupId)reconcileSignedGroupRoster(*connection);}
        else {for(auto& connection:connections)if(connection->online&&!connection->dead)groupOutbox(*connection);}
    }
    void drain(){
        std::deque<Command> batch;{std::lock_guard<std::mutex> lock(commandMutex);batch.swap(commands);}
        for(auto& command:batch)try{
            if(command.archive){
                archive::require(archiveAuthenticated&&authenticatedFriends.localAccount()>0&&authenticatedFriends.generation()>0);
                if(!archiveApplication)archiveApplication=std::make_unique<archive::Enrollment>(*store,profile,authenticatedFriends.localAccount(),nowMilliseconds);
                if(archiveGeneration!=authenticatedFriends.generation()){
                    archiveApplication->authenticated(authenticatedFriends.generation());archiveGeneration=authenticatedFriends.generation();
                }
                auto utc=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                if(!archiveExecution){
                    archiveExecution=std::make_unique<archive::Execution>(*store,profile,archiveApplication->policy(),*deliveries,*sessionTransports);
                    archiveGroups=std::make_unique<archive::GroupHistoryAccess>(*store,profile,authenticatedFriends.localAccount(),
                        [this](const std::string& server)->std::optional<archive::GroupHistoryContext>{
                            if(!groupKeys||groupAccount.accountId!=authenticatedFriends.localAccount())return {};
                            auto roster=groupKeys->roster(server);if(!roster)return {};auto ledger=groups.find(roster->legacyGroupId);
                            if(ledger==groups.end()||!group_keys::GroupKeyManager::matchesSignedLedger(*roster,ledger->second.state()))return {};
                            return archive::GroupHistoryContext{*roster,ledger->second.state()};
                        },[this](const std::string& local){groupHistoryRefreshes[local]=INT64_MAX;});
                    archiveExecution->groupHistory(*archiveGroups);
                }
                auto output=command.sourceKind>=12?archive::executeSyncOperation(*archiveApplication,*archiveExecution,command.sourceKind,command.bytes,nowMilliseconds(),networkGeneration):
                    archive::executeEnrollmentOperation(*archiveApplication,command.sourceKind,command.bytes,utc);
                if(command.bytesResponse)command.bytesResponse->set_value(std::move(output));
                continue;
            }
            if(command.mailbox){
                if(!mailboxApplication)mailboxApplication=std::make_unique<mailbox::WindowsMailbox>(mailboxProfileDirectory,profile);
                auto output=mailboxApplication->execute(command.sourceKind,command.bytes,command.id,command.accepted,*store,*deliveries,authenticatedFriends,*sessionTransports);
                if(command.sourceKind==5){protocol::Reader record(output);auto inserted=record.number();record.bytes();auto sender=record.string(),id=record.string();record.end();
                    if(inserted){auto message=store->message(id);if(message){NodeEvent event;event.kind=NodeEvent::Kind::Message;event.peerId=sender;event.messages.push_back(*message);emit(std::move(event));}}
                }else if(command.sourceKind==4){auto envelope=mailbox::parse(command.bytes);auto message=store->message(envelope.header.messageId);
                    if(message){NodeEvent event;event.kind=NodeEvent::Kind::Message;event.peerId=message->recipient;event.messages.push_back(*message);emit(std::move(event));}
                }else if(command.sourceKind==6){auto receipt=mailbox::parseReceipt(command.bytes);auto message=store->message(receipt.messageId);
                    if(message&&message->delivery==Delivery::Delivered){NodeEvent event;event.kind=NodeEvent::Kind::Delivered;event.peerId=message->recipient;event.messages.push_back(*message);emit(std::move(event));}
                }
                if(command.bytesResponse)command.bytesResponse->set_value(std::move(output));
                continue;
            }
            if(command.action==Action::Connect){
                // Explicit manual/Direct Only uses the same temporary listener.
                ensureDirectListener(command.peer.empty()?"manual-direct":command.peer);
                if(command.forceImmediate&&!command.peer.empty()){
                    const bool manual=command.route.rfind(L"manual retry",0)==0;
                    auto replacement=routeObservation(command.text,command.port,command.source,command.route,false,{nextRouteRevision++});
                    auto decision=sessionTransports->prepareImmediateRetry(sessionEpoch,command.peer,replacement,manual,nowMilliseconds());
                    auto hasId=[&](const std::vector<session::TransportInstanceId>& ids,std::uint64_t id){return std::any_of(ids.begin(),ids.end(),[&](auto value){return value.value==id;});};
                    for(auto it=connections.begin();it!=connections.end();){auto& c=**it;if(hasId(decision.updateRoute,c.attemptId)){c.source=command.source;c.route=command.route;if(c.directTransport)c.directTransport->updateRoute(replacement);}if(hasId(decision.cancel,c.attemptId)){emit(connectionEvent(NodeEvent::Kind::Disconnected,L"Retiring stale attempt for explicit retry.",c,command.peer));sessionTransports->closeTransport(sessionEpoch,{c.attemptId},"explicit retry cancelled old attempt",false);if(deliveries)deliveries->retireTransport({c.attemptId});it=connections.erase(it);}else ++it;}
                    for(auto it=resolvers.begin();it!=resolvers.end();){auto& resolver=**it;if(hasId(decision.updateRoute,resolver.attemptId)){resolver.source=command.source;resolver.route=command.route;}if(hasId(decision.cancel,resolver.attemptId)){sessionTransports->closeTransport(sessionEpoch,{resolver.attemptId},"explicit retry cancelled resolver",false);it=resolvers.erase(it);}else ++it;}
                    emitSessionSnapshot(command.peer);
                }
                if(!command.peer.empty()&&!command.forceImmediate)sessionTransports->requestImmediateRetry(command.peer,routeObservation(command.text,command.port,command.source,command.route,false,{nextRouteRevision++}),nowMilliseconds());
                resolve(command.text,command.port,command.peer,command.source,command.route);
            }
            else if(command.action==Action::Candidates){
                if(command.forceImmediate)sessionTransports->demandLiveRoute(command.peer,nowMilliseconds()+45000);
                std::vector<session::RouteObservation> routes;
                if(!command.text.empty()&&command.port){auto value=routeObservation(command.text,command.port,L"Discovery LAN candidate",L"same-subnet direct candidate",false,{nextRouteRevision++});value.networkRevision=command.accountId;routes.push_back(std::move(value));}
                auto publicPort=static_cast<std::uint16_t>(command.before);
                if(!command.name.empty()&&publicPort){auto value=routeObservation(command.name,publicPort,L"Discovery public candidate",L"public direct candidate",false,{nextRouteRevision++});value.networkRevision=command.accountId;routes.push_back(std::move(value));}
                sessionTransports->replaceCandidates(sessionEpoch,command.peer,std::move(routes),command.accepted,command.accountId,nowMilliseconds(),command.forceImmediate);emitSessionSnapshot(command.peer);
            }
            else if(command.action==Action::PrepareScopedP2p){ensureDirectListener(command.peer);}
            else if(command.action==Action::ScopedP2pProbe){
                ensureDirectListener(command.peer);
                auto route=routeObservation(command.text,command.port,L"Scoped mutual P2P consent",L"manual reliable P2P probe",false,{nextRouteRevision++});route.networkRevision=command.generation;
                auto wallNow=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                if(command.sourceHandle<=wallNow)throw std::runtime_error("Scoped P2P authorization has expired");
                auto expiry=nowMilliseconds()+(command.sourceHandle-wallNow)*1000;
                if(!sessionTransports->beginScopedP2pProbe(sessionEpoch,command.peer,command.id,expiry,route,nowMilliseconds()))throw std::runtime_error("Scoped P2P authorization is stale, unavailable, or not bound to the current relay session");
                emitSessionSnapshot(command.peer);
            }
            else if(command.action==Action::PromoteScopedP2p){handoffEffects(sessionTransports->promoteVerifiedP2P(sessionEpoch,command.peer,command.id,nowMilliseconds()));emitSessionSnapshot(command.peer);
            }
            else if(command.action==Action::CancelScopedP2p){
                auto ids=sessionTransports->cancelScopedP2p(sessionEpoch,command.peer);auto selected=[&](std::uint64_t id){return std::any_of(ids.begin(),ids.end(),[&](auto value){return value.value==id;});};
                for(auto it=resolvers.begin();it!=resolvers.end();){if(selected((*it)->attemptId)){sessionTransports->closeTransport(sessionEpoch,{(*it)->attemptId},"scoped P2P cancelled",false);it=resolvers.erase(it);}else ++it;}
                for(auto it=connections.begin();it!=connections.end();){if(selected((*it)->attemptId)){(*it)->closeReason=L"scoped P2P cancelled";sessionTransports->closeTransport(sessionEpoch,{(*it)->attemptId},"scoped P2P cancelled",false);if(deliveries)deliveries->retireTransport({(*it)->attemptId});it=connections.erase(it);}else ++it;}
                releaseDirectListener(command.peer);
                emitSessionSnapshot(command.peer);
            }
            else if(command.action==Action::RelayReady){
                std::shared_ptr<RelayPipe> pipe;{std::lock_guard<std::mutex> lock(relayMutex);auto found=relayPipes.find(command.accountId);if(found!=relayPipes.end())pipe=found->second;}
                auto expected=sessionTransports->attachablePeer(sessionEpoch,{command.accountId});
                auto authorization=sessionTransports->authorization(sessionEpoch,{command.accountId});
                if(!pipe||!expected||!authorization){if(pipe){std::lock_guard<std::mutex> lock(pipe->mutex);pipe->closed=true;pipe->changed.notify_all();}continue;}
                auto c=std::make_unique<Connection>();c->relay=true;c->relayPipe=pipe;c->incoming=command.accepted;c->expected=authorization->expectedRemoteDeviceId;c->authorization=*authorization;c->attemptId=command.accountId;c->host=L"relay";c->port=1;c->source=L"PeerChat server relay";c->route=L"end-to-end peer TLS over opaque relay";attachDirectTransport(*c);c->tls=std::make_unique<TlsSession>(*credentials,c->incoming);emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"relay rendezvous established; peer TLS begins",*c));collect(*c);connections.push_back(std::move(c));
            }
            else if(command.action==Action::RelayReceive){for(auto& c:connections)if(c->relay&&!c->dead&&c->attemptId==command.accountId){try{receiveEncrypted(*c,command.bytes);}catch(const std::exception& error){c->dead=true;std::string detail=error.what();c->closeReason=L"TLS handshake failure: "+widenUtf8(detail);emit(connectionEvent(NodeEvent::Kind::Error,c->closeReason,*c,c->peerId.empty()?c->expected:c->peerId));}break;}}
            else if(command.action==Action::RelayClosed){bool found=false;for(auto& c:connections)if(c->relay&&!c->dead&&c->attemptId==command.accountId){c->closeReason=command.text.empty()?L"relay channel closed":command.text;c->dead=true;found=true;break;}if(!found){auto authorization=sessionTransports->authorization(sessionEpoch,{command.accountId});auto close=sessionTransports->closeTransport(sessionEpoch,{command.accountId},utf8(command.text),true);std::shared_ptr<RelayPipe> pipe;{std::lock_guard<std::mutex> lock(relayMutex);auto item=relayPipes.find(command.accountId);if(item!=relayPipes.end()){pipe=item->second;relayPipes.erase(item);}}if(pipe){std::lock_guard<std::mutex> lock(pipe->mutex);pipe->closed=true;pipe->changed.notify_all();}if(authorization&&authorization->permitsArchiveControl()&&archiveApplication&&archive::currentSessionAuthorization(archiveApplication->policy(),*authorization)){sessionTransports->scheduleRelayRetry(authorization->continuityKey(),nowMilliseconds(),static_cast<std::uint32_t>(nowMilliseconds()));}else if(!close.remoteDeviceId.empty()&&(!authorization||authorization->scope==session::ReliableAuthorizationScope::Friend)){std::uint32_t random=0;BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&random),sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG);sessionTransports->scheduleRelayRetry(close.remoteDeviceId,nowMilliseconds(),random);emitSessionSnapshot(close.remoteDeviceId);}}}
            else if(command.action==Action::InvalidateRoute){
                if(command.peer.empty())continue;
                auto decision=sessionTransports->cancelRoutes(sessionEpoch,command.peer,utf8(endpoint(command.text,command.port)),command.accepted,command.accountId);
                if(decision.staleObservation){NodeEvent ignored;ignored.kind=NodeEvent::Kind::ConnectionLifecycle;ignored.peerId=ignored.expectedPeerId=command.peer;ignored.text=L"stale network invalidation ignored by session owner";emit(std::move(ignored));emitSessionSnapshot(command.peer);continue;}
                auto hasId=[&](std::uint64_t id){return std::any_of(decision.cancel.begin(),decision.cancel.end(),[&](auto value){return value.value==id;});};
                for(auto it=resolvers.begin();it!=resolvers.end();){if(hasId((*it)->attemptId)){sessionTransports->closeTransport(sessionEpoch,{(*it)->attemptId},"route invalidation cancelled resolver",false);it=resolvers.erase(it);}else ++it;}
                for(auto it=connections.begin();it!=connections.end();){auto& c=**it;if(hasId(c.attemptId)){c.source=command.accepted?L"Runtime network change":L"Authoritative candidate refresh";c.route=command.accepted?L"old-network route invalidated":L"exact stale endpoint invalidated";emit(connectionEvent(NodeEvent::Kind::Disconnected,command.accepted?L"local network generation superseded":L"local stale-route invalidation",c,command.peer));sessionTransports->closeTransport(sessionEpoch,{c.attemptId},"route invalidated",false);if(deliveries)deliveries->retireTransport({c.attemptId});it=connections.erase(it);}else{if(decision.retainedActiveWinner&&(c.expected==command.peer||c.peerId==command.peer)&&c.online)emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"authoritative endpoint changed; live verified session retained",c,command.peer));++it;}}
                auto contact=contacts.find(command.peer);if(contact!=contacts.end()&&!command.text.empty()&&command.port){auto& endpoints=contact->second.endpoints;endpoints.erase(std::remove_if(endpoints.begin(),endpoints.end(),[&](const Endpoint& endpoint){return endpoint.host==command.text&&endpoint.port==command.port;}),endpoints.end());store->saveContact(contact->second);}auto activeCall=calls.active();if(decision.activeWinnerCancelled&&activeCall&&calls.peer()==command.peer){calls.observeControl(false,nowMilliseconds()+MediaRecoveryWindowMilliseconds);voiceEvent(activeCall->state,L"Call control unavailable · waiting to reconnect");}emitSessionSnapshot(command.peer);
            }
            else if(command.action==Action::NetworkContext){bool changed=networkGeneration&&command.accountId>networkGeneration;bool screenNetworkChanged=changed&&networkFingerprint!=command.text;networkGeneration=command.accountId;networkFingerprint=command.text;if(sessionTransports)sessionTransports->observeNetwork(sessionEpoch,networkGeneration);if(changed&&calls.active()&&calls.active()->state==calls::State::Connected)interruptVoiceMedia(L"Network changed");if(screenNetworkChanged&&screenShares.active())interruptScreen(L"Network changed · reconnecting screen media");}
            else if(command.action==Action::MediaIceConfiguration){
                if(!command.ice.valid(static_cast<std::int64_t>(std::time(nullptr)))){
                    notice(NodeEvent::Kind::Error,L"TURN media configuration was rejected because it is invalid or expiring.");
                    continue;
                }
                mediaIce=std::move(command.ice);
            }
            else if(command.action==Action::AuthenticatedFriends){
                if(command.accountId!=authenticatedFriends.localAccount())clearGroupAttachmentSession();
                if(archiveApplication&&(command.accountId!=authenticatedFriends.localAccount()||
                   command.sourceHandle!=authenticatedFriends.generation()))archiveApplication->invalidate();
                archiveAuthenticated=command.accountId>0;
                std::vector<reliable::FriendIdentity> snapshot;
                for(const auto& value:command.friends){
                    snapshot.push_back({value.accountId,value.expectedPeerId,value.knownPeerIds});
                }
                auto validatedFriends=authenticatedFriends;
                validatedFriends.replace(command.accountId,command.sourceHandle,snapshot);
                if(command.accountId){
                    store->rememberAuthenticatedDeviceAccount(profile.peerId,command.accountId);
                    for(const auto& value:snapshot){
                        store->rememberAuthenticatedDeviceAccount(value.expectedPeerId,value.accountId);
                        for(const auto& device:value.knownPeerIds)store->rememberAuthenticatedDeviceAccount(device,value.accountId);
                    }
                }
                authenticatedFriends=std::move(validatedFriends);
            }
            else if(command.action==Action::PinInitialAuthenticatedFriendIdentity){
                Connection* connection=nullptr;
                for(auto& value:connections)if(!value->dead&&value->attemptId==command.sourceHandle){connection=value.get();break;}
                auto reject=[&](const std::wstring& reason){
                    if(!connection)return;
                    NodeEvent event=connectionEvent(NodeEvent::Kind::TrustRequired,reason,*connection,connection->peerId);
                    event.contacts.push_back({connection->peerId,connection->name,connection->tls?connection->tls->remotePublicKey():Blob{},connection->relay?std::vector<Endpoint>{}:std::vector<Endpoint>{{connection->host,connection->port}},false});
                    emit(std::move(event));
                };
                auto tlsKey=connection&&connection->tls?connection->tls->remotePublicKey():Blob{};
                auto decision=authenticatedFriends.initialPin(connection,connection&&!connection->dead,
                    command.peer,command.generation,connection&&connection->tls?&tlsKey:nullptr,contacts);
                if(decision==reliable::InitialPinDecision::StaleIdentity){
                    reject(L"Initial automatic device pinning was rejected because the connection identity is stale or mismatched. Verify this device explicitly.");
                    continue;
                }
                if(decision==reliable::InitialPinDecision::NoCurrentFriendship){
                    reject(L"Initial automatic device pinning requires a current authenticated friendship. Verify this device explicitly.");
                    continue;
                }
                auto known=contacts.find(connection->peerId);
                if(decision==reliable::InitialPinDecision::HistoricalPin||decision==reliable::InitialPinDecision::StoredKeyMismatch){
                    reject(decision==reliable::InitialPinDecision::HistoricalPin?L"A prior device identity is already pinned for this friend. Verify the changed device explicitly.":L"The stored device key does not match the presented Peer ID. Verify this device explicitly.");
                    continue;
                }
                auto contact=reliable::persistAuthenticatedContact(*store,*connection,known==contacts.end()?nullptr:&known->second,connection->tls->remotePublicKey(),connection->relay?std::optional<Endpoint>{}:Endpoint{connection->host,connection->port});
                contacts[connection->peerId]=contact;publishContacts();approve(*connection);
                emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"Initial authenticated friend device identity pinned automatically",*connection,connection->peerId));
            }
            else if(command.action==Action::SaveContact){
                auto& c=command.contact;protocol::check(protocol::id(c.peerId)&&!c.name.empty()&&c.name.size()<=64&&!c.endpoints.empty());
                for(const auto& e:c.endpoints)protocol::check(!e.host.empty()&&e.host.size()<=253&&e.port);
                store->saveContact(c);contacts[c.peerId]=c;publishContacts();
                const auto& e=c.endpoints.front();resolve(e.host,e.port,c.peerId,L"Saved contact",L"automatic reconnect");
            }
            else if(command.action==Action::RemoveContact){
                store->removeContact(command.peer);contacts.erase(command.peer);if(sessionTransports)sessionTransports->removePeer(command.peer);publishContacts();
            }
            else if(command.action==Action::Approve){for(auto& c:connections)if(!c->dead&&c->hello&&!c->approved&&c->peerId==command.peer){
                if(!command.accepted){c->closeReason=L"local trust rejection";c->dead=true;continue;}
                auto known=contacts.find(c->peerId);
                auto contact=reliable::persistAuthenticatedContact(*store,*c,known==contacts.end()?nullptr:&known->second,c->tls->remotePublicKey(),c->relay?std::optional<Endpoint>{}:Endpoint{c->host,c->port});
                contacts[c->peerId]=contact;publishContacts();approve(*c);
            }}
            else if(command.action==Action::FileTransfer){if(command.response)command.response->set_value(transferOperation(command));}
            else if(command.action==Action::GroupAttachment){
                if(!groupAttachments){if(command.sourceKind==0){if(command.response)command.response->set_value(L"{\"supported\":false,\"reason\":\"Protected group attachment storage is unavailable\",\"items\":[]}");continue;}throw std::runtime_error("Protected group attachment storage unavailable");}
                if(command.response)command.response->set_value(groupAttachments->command(command.sourceKind,command.group,command.id,command.text));
                if(command.sourceKind==7&&!command.text.empty())SecureZeroMemory(command.text.data(),command.text.size()*sizeof(wchar_t));
            }
            else if(command.action==Action::GroupAttachmentNetwork){
                protocol::check(groupAttachments!=nullptr);
                if(command.sourceKind==0){protocol::Reader r(command.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/AttachmentSession/v1");auto account=r.number();r.number();r.string();r.end();protocol::check(!account||(account==authenticatedFriends.localAccount()&&account==groupAccount.accountId));}
                auto output=groupAttachments->network(command.sourceKind,command.bytes);if(command.bytesResponse)command.bytesResponse->set_value(std::move(output));
            }
            else if(command.action==Action::Send){
                const auto requested=command.peer;
                command.peer=reliable::queueRecipient(requested,store->contacts(),authenticatedFriends);
                auto known=contacts.find(command.peer);
                auto m=reliable::queueText(*store,profile,known==contacts.end()?nullptr:&known->second,command.peer,command.id,command.text,
                    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),&authenticatedFriends);
                // The caller may clear its draft only after queueText committed
                // the existing history/outbox transaction, never on command admission.
                if(command.response)command.response->set_value(std::wstring(m.id.begin(),m.id.end()));
                liveOutboxPeers.insert(command.peer);
                sessionTransports->demandLiveRoute(command.peer,nowMilliseconds()+45000);
                if(requested!=command.peer)sessionTransports->demandLiveRoute(requested,nowMilliseconds()+45000);
                NodeEvent e;e.kind=NodeEvent::Kind::Message;e.peerId=command.peer;e.messages.push_back(m);emit(std::move(e));
                for(auto& c:connections)if(c->online&&!c->dead&&c->peerId==command.peer)outbox(*c);
            }else if(command.action==Action::History){NodeEvent e;e.kind=NodeEvent::Kind::History;e.peerId=command.peer;e.messages=store->history(command.peer,100,command.before);for(const auto& message:e.messages){auto account=store->historicalDeviceAccount(message.sender);if(message.sender==profile.peerId||(account&&authenticatedFriends.localAccount()>0&&*account==authenticatedFriends.localAccount()))e.localMessageAuthors.insert(message.sender);}emit(std::move(e));}
            else if(command.action==Action::EraseHistory){store->eraseHistory(command.peer);NodeEvent e;e.kind=NodeEvent::Kind::History;e.peerId=command.peer;e.text=L"history_cleared";emit(std::move(e));}
            else if(command.action==Action::ScreenBackend){configuredScreenBackend=command.screenBackend;}
            else if(command.action==Action::ScreenStart){
                if(screenShares.active())throw std::runtime_error("A screen share is already active");
                lastScreenFailureDiagnostics.clear();
                auto contact=contacts.find(command.peer);
                auto connection=screenConnection(command.peer);
                if(contact==contacts.end()||!contact->second.verified||!connection)throw std::runtime_error("A verified screen-share capable peer is required");
                if(!connection->screenBackendNegotiation)throw std::runtime_error("The peer must update before screen-share backend negotiation is available");
                if(command.sourceKind!=PC_SCREEN_MONITOR&&command.sourceKind!=PC_SCREEN_WINDOW)throw std::runtime_error("Invalid screen source");
                configuredScreenBackend=command.screenBackend;
                if(!screenShares.start(command.id,command.peer,true,screen::State::Offering,nowMilliseconds()+30000,command.screenBackend))throw std::runtime_error("Could not start screen share");
                screenPriorSentBytes=screenPriorReceivedBytes=0;
                screenOptions={};screenOptions.sender=true;screenOptions.sourceKind=command.sourceKind;screenOptions.sourceHandle=command.sourceHandle;
                screenOptions.fps=command.fps;screenOptions.bitrate=command.bitrate;screenOptions.captureCursor=command.accepted;
                protocol::Writer request(protocol::ScreenBackendRequest);request.string(command.id);request.number(1);
                request.number(static_cast<std::uint64_t>(command.screenBackend));packet(*connection,request);
                screenEvent(L"Negotiating screen-share backend");
            }else if(command.action==Action::ScreenAnswer){
                auto active=screenShares.active();if(!active||active->id!=command.id||active->state!=screen::State::Incoming)continue;
                if(!active->backendNegotiated||active->backend!=screen::negotiateBackend(active->peerRequirement,active->localRequirement)){
                    clearScreen(L"Screen-share backend negotiation did not complete");continue;
                }
                if(!screenConnection(active->peer)){clearScreen(L"Peer unavailable for screen-share control");continue;}
                screenOptions={};screenOptions.sender=false;screenOptions.presentationWindow=command.sourceHandle;
                screenOptions.width=1280;screenOptions.height=720;screenOptions.fps=60;
                if(!configureScreenMedia()){clearScreen(L"TURN media credentials are unavailable or expiring. Messaging remains connected.");continue;}std::string error;
                if(!screenMedia->startAnswer(active->remoteOffer,error)){clearScreen(widenUtf8(error));continue;}
                for(const auto& candidate:active->candidates)screenMedia->addCandidate(candidate.first,candidate.second);
                active->candidates.clear();screenShares.transition(screen::State::Connecting,nowMilliseconds()+20000);screenEvent(L"Connecting screen media");
            }else if(command.action==Action::ScreenDecline||command.action==Action::ScreenEnd){
                auto active=screenShares.active();if(!active||active->id!=command.id)continue;
                if(auto connection=screenConnection(active->peer)){protocol::Writer end(command.action==Action::ScreenDecline?protocol::ScreenDecline:protocol::ScreenEnd);end.string(active->id);packet(*connection,end);}
                clearScreen(command.action==Action::ScreenDecline?L"Screen share declined":L"Screen share ended");
            }else if(command.action==Action::ScreenDiagnostics){
                if(command.response)command.response->set_value(screenDiagnosticsSnapshot());
            }
            else if(command.action==Action::VoiceStart){
                if(!calls.idle())throw std::runtime_error("A voice call is already active");
                auto contact=contacts.find(command.peer);
                if(contact==contacts.end()||!contact->second.verified)throw std::runtime_error("Verify this peer before calling");
                auto connection=voiceConnection(command.peer);
                if(!connection)throw std::runtime_error("The verified peer is unavailable or does not advertise voice call control");
                if(!calls.beginOutgoing({command.id},calls::CallType::Voice,{command.peer},nowMilliseconds()+static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(VoiceRingTimeout).count())))throw std::runtime_error("A voice call is already active");
                calls.setMediaRecoveryCapable(command.peer,connection->mediaRecovery);calls.beginInitialMedia(command.id);resetVoiceSignaling(connection);if(!configureVoiceMedia(calls.active()->mediaGeneration)){clearVoice(L"TURN media credentials are unavailable or expiring. Messaging remains connected.",true);continue;}std::wstring error;
                if(!voiceMedia->startOffer(error)){clearVoice(error,true);continue;}voiceEvent(calls::State::Outgoing);
            }else if(command.action==Action::VoiceAnswer){
                auto active=calls.active();if(!active||active->state!=calls::State::Incoming||active->direction!=calls::Direction::Incoming||active->id.value!=command.id)throw std::runtime_error("That incoming call is no longer available");
                auto peer=calls.peer();auto connection=voiceConnection(peer);if(!connection){clearVoice(L"Peer unavailable for call control");continue;}
                calls.transition(command.id,calls::State::ConnectingMedia,nowMilliseconds()+static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(VoiceConnectTimeout).count()));voiceEvent(calls::State::ConnectingMedia);if(!configureVoiceMedia(active->mediaGeneration)){clearVoice(L"TURN media credentials are unavailable or expiring. Messaging remains connected.",true);continue;}std::wstring error;
                auto& participant=active->participants[peer];if(!voiceMedia->startAnswer(participant.remoteDescription,error)){clearVoice(error,true);continue;}for(const auto& candidate:participant.candidates)voiceMedia->addCandidate(candidate.first,candidate.second,error);participant.candidates.clear();
            }else if(command.action==Action::VoiceDecline){
                auto active=calls.active();if(!active||active->state!=calls::State::Incoming||active->direction!=calls::Direction::Incoming||active->id.value!=command.id)continue;
                if(auto connection=voiceConnection(calls.peer())){protocol::Writer decline(protocol::CallDecline);decline.string(active->id.value);packet(*connection,decline);}
                clearVoice(L"Declined");
            }else if(command.action==Action::VoiceEnd){
                auto active=calls.active();if(!active||active->id.value!=command.id)continue;
                if(auto connection=voiceConnection(calls.peer())){protocol::Writer end(protocol::CallEnd);end.string(active->id.value);packet(*connection,end);}
                clearVoice(L"Call ended");
            }else if(command.action==Action::VoiceMute){std::wstring error;if(!voiceMedia||!voiceMedia->setMuted(command.accepted,error))notice(NodeEvent::Kind::Error,error.empty()?L"No connected voice call.":L"Voice audio device error: "+error,calls.peer());else{calls.setMuted(command.accepted);voiceEvent(calls.state());}
            }else if(command.action==Action::VoiceDeafen){if(voiceMedia){voiceMedia->setDeafened(command.accepted);calls.setDeafened(command.accepted);voiceEvent(calls.state());}
            }else if(command.action==Action::VoiceDevices){if(calls.idle()){voiceInputDevice=static_cast<int>(command.port)-1;voiceOutputDevice=static_cast<int>(command.before);}
            }else if(command.action==Action::VoiceLevels){voiceInputPercent=static_cast<int>(command.port);voiceOutputPercent=static_cast<int>(command.before);if(voiceMedia)voiceMedia->setVolumes(voiceInputPercent/100.0f,voiceOutputPercent/100.0f);
            }else if(command.action==Action::VoiceProcessing){voiceProcessing.enabled=(command.port&1u)!=0;voiceProcessing.noiseSuppression=(command.port&2u)!=0;voiceProcessing.automaticGain=(command.port&4u)!=0;voiceProcessing.echoCancellation=(command.port&8u)!=0;voiceProcessing.noiseSuppressionStrength=static_cast<NoiseSuppressionStrength>(command.noiseSuppressionStrength);voiceProcessing.automaticGainStrength=static_cast<AutomaticGainStrength>(command.automaticGainStrength);
            }else if(command.action==Action::VoiceDiagnostics){if(command.response)command.response->set_value(voiceMedia?voiceSignalingDiagnostics()+L"\n"+voiceMedia->diagnostics():lastVoiceDiagnostics);
            }
            else if(command.action==Action::Name){profile.name=command.text;store->saveProfile(profile);}
            else if(command.action==Action::GroupAccount){if(command.accountId==0||command.name.empty()||command.name.size()>64)throw std::runtime_error("Invalid group account binding");if(groupAccount.accountId&&groupAccount.accountId!=command.accountId)throw std::runtime_error("This profile is already bound to another group account");groupAccount={command.accountId,command.name};store->saveGroupAccount(groupAccount);publishGroups();}
            else if(command.action==Action::AuthenticatedGroupRoster){
                if(!groupAccount.accountId||command.accountId!=groupAccount.accountId||!command.generation||command.generation<groupRosterDiscoveryGeneration)
                    throw std::runtime_error("Authenticated group roster belongs to a stale account session");
                if(!groupKeys)groupKeys=std::make_unique<group_keys::GroupKeyManager>(*store,groupAccount.accountId,profile.peerId);
                auto applicationLedger=groups.find(command.roster.legacyGroupId);
                group_devices::Replica::applyAuthenticated(*store,*groupKeys,std::move(command.roster),applicationLedger==groups.end()?nullptr:&applicationLedger->second);
                groupRosterDiscoveryGeneration=command.generation;
                const auto* current=groupKeys->roster(command.group);
                if(current){
                    auto retired=sessionTransports->invalidateGroupControl(sessionEpoch,command.group,current->generation);
                    for(auto& connection:connections)for(auto id:retired)if(connection->attemptId==id.value){connection->closeReason=L"group roster generation superseded";connection->dead=true;}
                    auto ledger=groups.find(current->legacyGroupId);
                    if(ledger!=groups.end()&&current->distributorPeerId==profile.peerId&&
                       groupKeys->ensureCurrentKey(current->serverGroupId,ledger->second.state())){
                        NodeEvent keyReady;keyReady.kind=NodeEvent::Kind::ConnectionLifecycle;
                        keyReady.peerId=profile.peerId;keyReady.authorizationGroupId=current->serverGroupId;
                        keyReady.authorizationRosterGeneration=current->generation;
                        keyReady.text=L"Current group key generation ready locally";emit(std::move(keyReady));
                    }
                }
            }
            else if(command.action==Action::ClearGroupRosters){
                for(auto& connection:connections)if(connection->authorization.scope==session::ReliableAuthorizationScope::GroupControl){connection->closeReason=L"authenticated group authorization cleared";connection->dead=true;}
                groupKeys.reset();groupRosterDiscoveryGeneration=0;
            }
            else if(command.action==Action::BeginGroupControl){
                const auto* roster=groupKeys?groupKeys->roster(command.group):nullptr;
                if(!roster||command.generation!=groupRosterDiscoveryGeneration||!groupAccount.accountId||
                   !group_keys::GroupKeyManager::currentMember(*roster,groupAccount.accountId,profile.peerId)||
                   !group_keys::GroupKeyManager::currentMember(*roster,command.accountId,command.peer)||
                   command.peer==profile.peerId)throw std::runtime_error("Group-control relay is not authorized by the current roster");
                session::ReliableAuthorization authorization{session::ReliableAuthorizationScope::GroupControl,command.group,roster->generation,command.peer};
                const auto attempt=nextAttemptId++;
                session::RouteObservation route{{nextRouteRevision++},"relay:"+authorization.continuityKey(),"PeerChat server relay","group key control over peer TLS",false,networkGeneration,session::TransportKind::Relay,session::RouteClass::ServerRelay};
                if(!sessionTransports->registerGroupControlAttempt(sessionEpoch,{attempt},authorization,route,nowMilliseconds()))throw std::runtime_error("Group-control relay registration was rejected");
                auto pipe=std::make_shared<RelayPipe>();{std::lock_guard<std::mutex> lock(relayMutex);relayPipes[attempt]=pipe;}
                NodeEvent request;request.kind=NodeEvent::Kind::RelayRequested;request.peerId=request.expectedPeerId=command.peer;request.attemptId=attempt;request.networkGeneration=networkGeneration;request.authorizationGroupId=command.group;request.authorizationRosterGeneration=roster->generation;request.authorizationTargetAccountId=command.accountId;request.candidate=L"PeerChat server relay";request.candidateSource=L"Authenticated group roster";request.route=L"group key control over end-to-end peer TLS";request.text=L"Group-control relay requested";emit(std::move(request));emitSessionSnapshot(authorization.continuityKey());
            }
            else if(command.action==Action::GroupCreate){
                if(!groupAccount.accountId||command.group.empty()||command.text.empty()||command.text.size()>80)throw std::runtime_error("Invalid group creation");
                GroupEvent event;event.groupId=command.group;event.kind=GroupEventKind::Create;event.logicalOrder=1;event.createdMilliseconds=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());event.actorAccountId=event.subjectAccountId=groupAccount.accountId;event.actorPeerId=event.subjectPeerId=profile.peerId;event.subjectName=groupAccount.name;event.subjectPublicKey=profile.publicKey;event.value=command.text;auto& ledger=groups[event.groupId];commitLocalGroupEvent(ledger,std::move(event),{});
            }else if(command.action==Action::GroupCreationProof){
                std::wstring encoded;auto found=groups.find(command.group);
                if(found!=groups.end()&&found->second.state().ownerAccountId==groupAccount.accountId){
                    for(const auto& event:found->second.events())if(event.kind==GroupEventKind::Create&&event.actorAccountId==groupAccount.accountId&&event.actorPeerId==profile.peerId&&found->second.accepted(event.eventId)){
                        Blob frame=encodeGroupEvent(event);DWORD length=0;
                        if(CryptBinaryToStringW(frame.data(),static_cast<DWORD>(frame.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&length)&&length){
                            std::wstring buffer(length,L'\0');if(CryptBinaryToStringW(frame.data(),static_cast<DWORD>(frame.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,&buffer[0],&length)){buffer.resize(wcslen(buffer.c_str()));encoded=std::move(buffer);}
                        }
                        break;
                    }
                }
                if(command.response)command.response->set_value(std::move(encoded));
            }else if(command.action==Action::Groups){publishGroups();
            }else if(command.action==Action::GroupReceiveCiphertext){receiveGroupMessage(command.bytes);
            }else if(command.action==Action::GroupMessageStatus){
                auto roster=groupKeys?groupKeys->roster(command.group):nullptr;
                if(roster){
                    auto record=store->authenticatedGroupMessage(command.group,command.id);group_messages::Envelope envelope;
                    const bool own=group_messages::parse(record.envelope,envelope)==group_messages::Result::Accepted&&envelope.senderDevice==profile.peerId;
                    SecureZeroMemory(record.plaintext.data(),record.plaintext.size());
                    if(own&&store->setGroupMessageStatus(command.group,command.id,utf8(command.text),command.sourceKind)){NodeEvent event;event.kind=NodeEvent::Kind::GroupDelivered;event.peerId=roster->legacyGroupId;event.detail=std::wstring(command.id.begin(),command.id.end());event.text=command.text;emit(std::move(event));}
                }
            }else if(command.action==Action::GroupHistoryPage){emitGroupHistoryPage(command.group,command.before);
            }
            else {
                auto found=groups.find(command.group);if(found==groups.end())throw std::runtime_error("Unknown Group ID");auto& ledger=found->second;auto peers=groupPeers(ledger.state());GroupEvent event;
                if(command.action==Action::GroupRename){event=localGroupEvent(ledger,GroupEventKind::Rename);event.value=command.text;}
                else if(command.action==Action::GroupInvite){auto contact=contacts.find(command.peer);if(contact==contacts.end()||!contact->second.verified||contact->second.publicKey.empty())throw std::runtime_error("Verify this friend before inviting them");event=localGroupEvent(ledger,GroupEventKind::Invite);event.subjectAccountId=command.accountId;event.subjectPeerId=command.peer;event.subjectName=command.name;event.subjectPublicKey=contact->second.publicKey;}
                else if(command.action==Action::GroupRespond){event=localGroupEvent(ledger,command.accepted?GroupEventKind::Accept:GroupEventKind::Decline);event.subjectAccountId=groupAccount.accountId;event.subjectPeerId=profile.peerId;}
                else if(command.action==Action::GroupLeave){event=localGroupEvent(ledger,GroupEventKind::Leave);}
                else if(command.action==Action::GroupRemove){event=localGroupEvent(ledger,GroupEventKind::Remove);event.subjectAccountId=command.accountId;}
                else if(command.action==Action::GroupRole){event=localGroupEvent(ledger,GroupEventKind::SetRole);event.subjectAccountId=command.accountId;event.value=command.accepted?L"admin":L"member";}
                else if(command.action==Action::GroupSend){sendGroupMessage(command.group,command.text);continue;}
                else throw std::runtime_error("Unsupported group action");
                commitLocalGroupEvent(ledger,std::move(event),peers,command.action==Action::GroupInvite);
            }
        }catch(const std::exception& error){if(command.bytesResponse)try{command.bytesResponse->set_exception(std::current_exception());}catch(const std::future_error&){}if(command.response)try{command.response->set_exception(std::current_exception());}catch(const std::future_error&){}if(command.archive){std::string reason=error.what();notice(NodeEvent::Kind::Error,L"Archive operation rejected: "+std::wstring(reason.begin(),reason.end()),"archive-control:operation");continue;}if(command.mailbox){std::string reason=error.what();notice(NodeEvent::Kind::Error,L"Mailbox operation "+std::to_wstring(command.sourceKind)+L" rejected: "+std::wstring(reason.begin(),reason.end()),"mailbox-control:operation");continue;}if(command.action==Action::GroupAttachment&&command.sourceKind==7&&!command.text.empty())SecureZeroMemory(command.text.data(),command.text.size()*sizeof(wchar_t));if(command.action==Action::FileTransfer||command.action==Action::GroupAttachment||command.action==Action::GroupAttachmentNetwork)continue;std::string reason=error.what();notice(NodeEvent::Kind::Error,std::wstring(reason.begin(),reason.end()),command.peer);}
    }
    void reapDeadConnections(){
        for(auto it=connections.begin();it!=connections.end();)if((*it)->dead){
            auto& connection=**it;std::string id=!connection.expected.empty()?connection.expected:connection.peerId;bool mismatch=connection.identityMismatch,wasRelay=connection.relay;
            bool groupControl=connection.authorization.scope!=session::ReliableAuthorizationScope::Friend;
            const bool archiveControl=connection.authorization.permitsArchiveControl();
            if(archiveControl&&archiveExecution)archiveExecution->recordTransport(connection.attemptId,connection.authorization,
                connection.online?(connection.archiveBound?"route_closed_after_binding":"route_closed_before_binding"):
                connection.hello?"route_closed_after_hello":connection.tls&&connection.tls->ready()?"route_closed_after_tls":"route_closed_before_tls");
            std::string groupScopeKey=groupControl?connection.authorization.continuityKey():std::string{};
            if(connection.authorization.permitsArchiveControl()&&archiveExecution)archiveExecution->disconnected(connection.authorization,connection.attemptId);
            std::wstring host=connection.host,source=connection.source,route=connection.route;std::uint16_t port=connection.port;
            std::wstring reason=mismatch?L"peer identity mismatch; automatic retry paused":connection.closeReason.empty()?L"connection closed without a recorded origin":connection.closeReason;
            if(connection.directTransport)connection.directTransport->closed(utf8(reason),!connection.online);
            auto close=sessionTransports->closeTransport(sessionEpoch,{connection.attemptId},utf8(reason),!connection.online);
            auto disconnected=connectionEvent(NodeEvent::Kind::Disconnected,reason,connection,id);
            if(deliveries)deliveries->retireTransport({connection.attemptId});
            emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"transport closed: "+reason,connection,id));
            if(wasRelay&&connection.relayPipe){std::lock_guard<std::mutex> lock(connection.relayPipe->mutex);connection.relayPipe->closed=true;connection.relayPipe->changed.notify_all();}
            {std::lock_guard<std::mutex> lock(relayMutex);relayPipes.erase(connection.attemptId);}it=connections.erase(it);
            if(!id.empty()&&!close.hasActiveWinner&&!close.hasPendingAttempt){
                auto activeCall=calls.active();if(close.wasActiveWinner&&!close.hasActiveWinner&&activeCall&&calls.peer()==id){calls.observeControl(false,nowMilliseconds()+MediaRecoveryWindowMilliseconds);voiceEvent(activeCall->state,L"Call control unavailable · waiting to reconnect");}
                auto activeShare=screenShares.active();if(close.wasActiveWinner&&!close.hasActiveWinner&&activeShare&&activeShare->peer==id){activeShare->controlAvailable=false;screenEvent(L"Share control unavailable · waiting to reconnect");}
                if(!groupControl)emit(std::move(disconnected));
                if(wasRelay&&!groupControl){std::uint32_t random=0;BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&random),sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG);sessionTransports->scheduleRelayRetry(id,nowMilliseconds(),random);emitSessionSnapshot(id);}
                else if(groupControl){if(archiveControl)sessionTransports->scheduleRelayRetry(groupScopeKey,nowMilliseconds(),static_cast<std::uint32_t>(nowMilliseconds()));emitSessionSnapshot(groupScopeKey);}
                else scheduleReconnect(id,host,port,source,route,mismatch);
            }else if(groupControl)emitSessionSnapshot(groupScopeKey);
            else if(!id.empty())emitSessionSnapshot(id);
        }else ++it;
    }
    void run(const std::wstring& directory,std::uint16_t listeningPortOverride,const std::wstring& displayNameOverride,bool startDirectListener){
        snippetRoot=(std::filesystem::path(directory)/L"attachment-snippets").wstring();
        WSADATA data{};bool winsock=false;
        try {
            if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Windows sockets unavailable");
            winsock=true;
            archiveTransfers.reset();archiveExecution.reset();archiveGroups.reset();archiveApplication.reset();archiveGeneration=0;archiveAuthenticated=false;groupReferenceUiCursors.clear();groupReferenceScanAt=0;
            store=std::make_unique<Storage>(directory);profile=store->profile();
            mailboxApplication.reset();mailboxProfileDirectory=directory;
            deliveries=std::make_unique<delivery::DeliveryManager>(*store,profile.peerId);
            downloadSaves.reset();groupAttachments.reset();transfers.reset();cacheJournal.reset();transferJournal.reset();transferCleanup.reset();transferPaths.clear();transferPersistenceReported=false;groupAttachmentPersistenceReported=false;cataloguedTransfers.clear();attachmentCatalogAt=0;
            try{
                transferCleanup=std::make_unique<transfer::windows::CleanupJournal>(directory);transferCleanup->recover();
                transferJournal=std::make_unique<transfer::windows::DurableJournal>(directory,profile.peerId);
                transferStatistics=std::make_unique<transfer::ProtectedStatisticsStore>(*store,profile.peerId);
                try{
                    cacheJournal=std::make_unique<group_attachments::windows::CacheStateStore>(directory,profile.peerId,*transferJournal,[this](const transfer::DurableState& d){protocol::check(groupAttachments!=nullptr);return groupAttachments->reopen(d);});
                    cacheJournal->load(); // Application metadata must be verified before reopening cache sources.
                    transfers=std::make_unique<transfer::TransferManager>([]{return std::make_unique<transfer::windows::Sha256>();},cacheJournal.get(),transferStatistics.get());
                    groupAttachments=std::make_unique<group_attachments::windows::Application>(*store,profile,*transfers,*cacheJournal,directory,
                        [this](const std::string& group,const group_keys::RosterSnapshot* historical){return attachmentContext(group,historical);},
                        [this](const auto& operation){if(!mailboxApplication)mailboxApplication=std::make_unique<mailbox::WindowsMailbox>(mailboxProfileDirectory,profile);mailboxApplication->attachmentKeys(operation);});
                    transfers->restore(nowMilliseconds());
                }catch(const std::exception&){
                    // A cache-only recovery failure never resets protected cache
                    // data or disables otherwise valid direct-file transfers.
                    groupAttachments.reset();transfers.reset();cacheJournal.reset();
                    transfers=std::make_unique<transfer::TransferManager>([]{return std::make_unique<transfer::windows::Sha256>();},transferJournal.get(),transferStatistics.get());
                    transfers->restore(nowMilliseconds());
                    notice(NodeEvent::Kind::Error,L"Group attachments unavailable: protected recovery metadata requires attention","group-attachment:storage");
                }
                for(auto& d:transferJournal->load())if(!d.location.empty()){
                    protocol::Reader location(d.location);auto source=location.text();transferPaths[d.metadata.id]=d.sending?source:location.text();
                }
                try{downloadSaves=std::make_unique<download_save::Coordinator>(directory,profile.peerId);}catch(const std::exception&){notice(NodeEvent::Kind::Error,L"Automatic saving unavailable: protected save choices require attention; verified downloads are retained");}
            }
            catch(...){groupAttachments.reset();transfers.reset();cacheJournal.reset();notice(NodeEvent::Kind::Error,L"File transfers unavailable: protected restart metadata requires attention");}
            sessionEpoch={nextSessionEpoch++};
            auto profileScope=utf8(std::filesystem::path(directory).filename().wstring());
            sessionTransports=std::make_unique<session::SessionTransportManager>(std::move(profileScope),profile.peerId,sessionEpoch,static_cast<std::uint32_t>(ReconnectBaseMilliseconds),static_cast<std::uint32_t>(ReconnectCapMilliseconds));
            sessionTransports->setDemandDriven(true);
#ifdef PEERCHAT_TEST_MEDIA_DIRECT
            // Existing deterministic core fixtures do not contact discovery. Their
            // explicit build flag is the only place direct test ICE is permitted.
            mediaIce=MediaIceConfiguration::directForTests();
#endif
            if(listeningPortOverride)profile.listenPort=listeningPortOverride;
            if(!displayNameOverride.empty())profile.name=displayNameOverride;
            if(listeningPortOverride||!displayNameOverride.empty())store->saveProfile(profile);
            credentials=std::make_unique<TlsCredentials>(profile);
            contacts.clear();for(auto& c:store->contacts())contacts[c.peerId]=c;
            liveOutboxPeers.clear();for(const auto& c:contacts)if(!store->pending(c.first,1).empty())liveOutboxPeers.insert(c.first);
            groupAccount=store->groupAccount();groupKeys.reset();groupRosterDiscoveryGeneration=0;groups.clear();for(const auto& groupId:store->groupIds()){auto ledger=group_devices::Replica::restoreStored(*store,groupId);if(ledger.state().created)groups.emplace(groupId,std::move(ledger));}
            screenAvailable=ScreenShareMedia::available();
            startupDirectListener=startDirectListener;
            if(startupDirectListener)ensureDirectListener("explicit-direct-only",listeningPortOverride);
            NodeEvent ready;ready.kind=NodeEvent::Kind::Ready;ready.peerId=profile.peerId;ready.text=profile.name;ready.detail=widenUtf8(credentials->certificateDiagnostics());ready.port=listener==INVALID_SOCKET?0:profile.listenPort;emit(std::move(ready));publishContacts();publishGroups();
            // Pending friend IDs are intentionally reconnectable too. The first
            // successful TLS handshake supplies the public key that turns the
            // saved fingerprint reference into a verified contact.
            // Persisted contacts may contain historical direct endpoints from older builds. They
            // are identity data, not an authorization to probe. Server-first startup deliberately
            // seeds no direct retry; only relay policy or a fresh scoped P2P authorization can
            // create a new route attempt.
            transferTiming.clear();
            while(running){
                drain();
                if(nowMilliseconds()>=demandCheckAt){
                    const auto now=nowMilliseconds();demandCheckAt=now+1000;
                    for(auto it=liveOutboxPeers.begin();it!=liveOutboxPeers.end();){
                        auto pending=deliveries->pendingMessages(*it,1000);
                        if(std::none_of(pending.begin(),pending.end(),[](const auto& message){return message.delivery!=Delivery::Stored;})){it=liveOutboxPeers.erase(it);continue;}
                        auto contact=contacts.find(*it);
                        if(contact==contacts.end()||!contact->second.verified){++it;continue;} // Verification has a bounded demand window.
                        sessionTransports->demandLiveRoute(*it,now+2000,now);++it;
                    }
                    if(transfers){std::set<std::string> directPeers;for(const auto& row:transfers->snapshots())if(row.purpose==transfer::Purpose::PeerFile)directPeers.insert(row.peer);for(const auto& peer:transfers->demandedPeers())if(directPeers.count(peer)){auto contact=contacts.find(peer);if(contact!=contacts.end()&&contact->second.verified)sessionTransports->demandLiveRoute(peer,now+2000,now);}}
                    if(calls.active())sessionTransports->demandLiveRoute(calls.peer(),now+2000);
                    if(screenShares.active())sessionTransports->demandLiveRoute(screenShares.active()->peer,now+2000);
                    auto retired=sessionTransports->retireIdleRoutes(now);
                    for(auto& c:connections)if(std::any_of(retired.begin(),retired.end(),[&](auto id){return id.value==c->attemptId;})){
                        c->closeReason=L"No live route demand — relay idle";c->dead=true;
                    }
                }
                // Relay-only idle iterations skip the direct-socket select below, so retire
                // relay EOFs here before that early sleep/continue can bypass manager cleanup.
                serviceArchive();serviceGroupHistory();reapDeadConnections();resolved();serviceTransfers();handoffEffects(sessionTransports->handoffTick(sessionEpoch,nowMilliseconds()));mediaEvents();serviceMediaRecovery();screenMediaEvents();serviceScreenRecovery();
                if(screenShares.expired(nowMilliseconds())){auto active=screenShares.active();if(active){if(auto connection=screenConnection(active->peer)){protocol::Writer end(protocol::ScreenEnd);end.string(active->id);packet(*connection,end);}}clearScreen(L"Screen share timed out");}
                if(calls.timedOut(nowMilliseconds())){auto active=calls.active();if(active){if(auto connection=voiceConnection(calls.peer())){protocol::Writer end(protocol::CallEnd);end.string(active->id.value);packet(*connection,end);}}clearVoice(L"Call timed out",true);}
                if(calls.mediaTimedOut(nowMilliseconds())||calls.controlAndMediaTimedOut(nowMilliseconds())){auto active=calls.active();if(active){if(auto connection=voiceConnection(calls.peer())){protocol::Writer end(protocol::CallEnd);end.string(active->id.value);packet(*connection,end);}}clearVoice(L"Voice connection could not be restored",true);}
                for(const auto& retry:sessionTransports->takeDueRetries(nowMilliseconds())){auto split=retry.route.endpointUtf8.rfind(':');if(split==std::string::npos)continue;auto host=widenUtf8(retry.route.endpointUtf8.substr(0,split));std::uint16_t port=static_cast<std::uint16_t>(std::stoul(retry.route.endpointUtf8.substr(split+1)));auto source=widenUtf8(retry.route.sourceUtf8),route=widenUtf8(retry.route.routeUtf8);try{resolve(host,port,retry.remoteDeviceId,source,route.empty()?L"automatic reconnect":route);}catch(...){scheduleReconnect(retry.remoteDeviceId,host,port,source,route);}}
                for(const auto& relay:sessionTransports->takeDueRelayAttempts(nowMilliseconds())){
                    if(relay.networkRevision<networkGeneration)continue;
                    auto attempt=nextAttemptId++;
                    session::RouteObservation route{{nextRouteRevision++},"relay:"+relay.remoteDeviceId,"PeerChat server relay","relay fallback",false,relay.networkRevision,session::TransportKind::Relay,session::RouteClass::ServerRelay};
                    const bool archiveRoute=relay.authorization.permitsArchiveControl();
                    if(archiveRoute){if(!archiveApplication||!archive::currentSessionAuthorization(archiveApplication->policy(),relay.authorization)||
                        !sessionTransports->registerArchiveControlAttempt(sessionEpoch,{attempt},relay.authorization,route,nowMilliseconds()))continue;}
                    else if(!sessionTransports->registerAttempt(sessionEpoch,{attempt},relay.remoteDeviceId,route,nowMilliseconds()))continue;
                    auto pipe=std::make_shared<RelayPipe>();{std::lock_guard<std::mutex> lock(relayMutex);relayPipes[attempt]=pipe;}
                    NodeEvent request;request.kind=NodeEvent::Kind::RelayRequested;request.peerId=request.expectedPeerId=relay.remoteDeviceId;request.attemptId=attempt;request.networkGeneration=relay.networkRevision;request.candidate=L"PeerChat server relay";request.candidateSource=L"Authenticated discovery service";request.route=L"manager-owned bounded direct fallback";request.text=L"Relay transport requested";
                    if(archiveRoute){request.authorizationArchive=group_text::base64(archive::sessionBinding(relay.authorization).data);request.authorizationTargetAccountId=relay.authorization.archive.account;}
                    emit(std::move(request));emitSessionSnapshot(archiveRoute?relay.authorization.continuityKey():relay.remoteDeviceId);
                }
                for(auto& c:connections)if(!c->dead&&c->relay&&!c->online&&Clock::now()-c->began>std::chrono::seconds(20)){c->closeReason=L"relay peer authentication timeout";c->dead=true;}
                reapDeadConnections();
                // Winsock rejects select() with no sockets. Relay-only idle
                // profiles intentionally have neither a direct listener nor a
                // direct connection, so yield briefly and return to the
                // manager-owned relay/control work above.
                bool hasDirectSocket=listener!=INVALID_SOCKET;
                for(const auto& connection:connections)if(!connection->dead&&!connection->relay){hasDirectSocket=true;break;}
                if(!hasDirectSocket){
                    // Hash/revalidate one bounded chunk per pass, servicing
                    // commands/timers between passes instead of sleeping per chunk.
                    if(transfers&&(transfers->hasPreparationWork(nowMilliseconds())||transfers->hasTransmissionWork()))continue;
                    // Preserve the idle/timer budget, but received relay bytes
                    // and commands must not incur another fixed polling delay.
                    std::unique_lock<std::mutex> lock(commandMutex);
                    commandChanged.wait_for(lock,std::chrono::milliseconds(20),[&]{return !running||!commands.empty();});
                    continue;
                }
                fd_set reads,writes,errors;FD_ZERO(&reads);FD_ZERO(&writes);FD_ZERO(&errors);if(listener!=INVALID_SOCKET)FD_SET(listener,&reads);
                for(auto& c:connections)if(!c->dead&&!c->relay){if(!c->connecting)FD_SET(c->socket,&reads);if(c->connecting||c->sent<c->output.size())FD_SET(c->socket,&writes);FD_SET(c->socket,&errors);}
                timeval timeout{0,transfers&&(transfers->hasPreparationWork(nowMilliseconds())||transfers->hasTransmissionWork())?0:20000};if(select(0,&reads,&writes,&errors,&timeout)==SOCKET_ERROR)throw std::runtime_error("Network polling failed");
                if(listener!=INVALID_SOCKET&&FD_ISSET(listener,&reads))for(int i=0;i<8;i++){SOCKET socket=accept(listener,nullptr,nullptr);if(socket==INVALID_SOCKET)break;if(connections.size()>=MaxConnections){closesocket(socket);continue;}auto c=std::make_unique<Connection>();c->socket=socket;c->incoming=true;c->attemptId=nextAttemptId++;c->source=L"Inbound direct connection";c->route=L"incoming authenticated route";attachDirectTransport(*c);nonblocking(socket);c->tls=std::make_unique<TlsSession>(*credentials,true);connections.push_back(std::move(c));}
                for(auto& c:connections)if(!c->dead&&!c->relay)try{
                    if(FD_ISSET(c->socket,&errors)){int error=0,length=sizeof(error);getsockopt(c->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length);error=error?error:WSAECONNRESET;tcpFailure(*c,error);c->closeReason=L"socket error "+std::to_wstring(error);c->dead=true;}
                    if(c->connecting&&FD_ISSET(c->socket,&writes)){int error=0,length=sizeof(error);if(getsockopt(c->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)||error){error=error?error:WSAECONNRESET;tcpFailure(*c,error);c->closeReason=L"connect completion error "+std::to_wstring(error);c->dead=true;}c->connecting=false;}
                    if(!c->dead&&!c->connecting&&!c->tls){c->tls=std::make_unique<TlsSession>(*credentials,false);collect(*c);}
                    if(!c->dead&&!c->connecting&&FD_ISSET(c->socket,&reads))receive(*c);
                    if(!c->dead&&!c->connecting&&FD_ISSET(c->socket,&writes))flush(*c);
                    auto age=Clock::now()-c->began;if(!c->online&&age>std::chrono::seconds(c->hello?120:10)){emit(connectionEvent(NodeEvent::Kind::Error,L"TCP connection timeout: the peer did not respond in time.",*c,c->peerId.empty()?c->expected:c->peerId));c->closeReason=L"local authentication timeout";c->dead=true;}
                    if(c->output.size()>c->sent&&Clock::now()-c->lastProgress>std::chrono::seconds(15)){c->closeReason=L"local send timeout";c->dead=true;}
                }catch(const std::exception& error){c->dead=true;std::string detail=error.what();std::wstring safeDetail(detail.begin(),detail.end());std::wstring stage=!c->hello?L"TLS handshake failure: ":L"application protocol/identity failure: ";c->closeReason=stage+safeDetail;emit(connectionEvent(NodeEvent::Kind::Error,c->closeReason,*c,c->peerId.empty()?c->expected:c->peerId));}
                reapDeadConnections();
            }
        }catch(const std::exception& error){std::string reason=error.what();notice(NodeEvent::Kind::Error,std::wstring(reason.begin(),reason.end()));}
        archiveTransfers.reset();archiveExecution.reset();archiveGroups.reset();archiveApplication.reset();archiveGeneration=0;archiveAuthenticated=false;
        clearVoice(L"Profile closed");calls.reset();clearScreen(L"Profile closed");running=false;resolvers.clear();connections.clear();{std::lock_guard<std::mutex> relayLock(relayMutex);for(auto& item:relayPipes){std::lock_guard<std::mutex> pipeLock(item.second->mutex);item.second->closed=true;item.second->changed.notify_all();}relayPipes.clear();}downloadSaves.reset();groupAttachments.reset();transfers.reset();cacheJournal.reset();transferJournal.reset();transferCleanup.reset();transferPaths.clear();mailboxApplication.reset();sessionTransports.reset();deliveries.reset();groupKeys.reset();groupRosterDiscoveryGeneration=0;directListenerLeases.clear();closeDirectListener();credentials.reset();store.reset();groups.clear();groupAccount={};SecureZeroMemory(profile.privateKey.data(),profile.privateKey.size());profile={};if(winsock)WSACleanup();
    }
};
Node::Node():impl_(std::make_unique<Impl>()){}Node::~Node()=default;
void Node::start(const std::wstring& directory,std::uint16_t port,const std::wstring& name,bool startDirectListener){impl_->stop();{std::lock_guard<std::mutex> lock(impl_->eventMutex);impl_->events.clear();}impl_->running=true;impl_->worker=std::thread([this,directory,port,name,startDirectListener]{impl_->run(directory,port,name,startDirectListener);});}
void Node::stop(){impl_->stop();}
Blob Node::mailbox(unsigned operation,const Blob& input,const std::string& messageId,bool storageAllowed){
    if(operation>9||input.size()>(operation==9?1024u:mailbox::MaxWire)||(!messageId.empty()&&!protocol::id(messageId,32))||(operation==9&&(!messageId.empty()||storageAllowed)))throw std::invalid_argument("Invalid native mailbox command");
    auto response=std::make_shared<std::promise<Blob>>();auto future=response->get_future();Impl::Command command{};
    command.mailbox=true;command.sourceKind=operation;command.bytes=input;command.id=messageId;command.accepted=storageAllowed;command.bytesResponse=response;
    if(!impl_->command(std::move(command)))throw std::runtime_error("Native profile unavailable");
    if(future.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Native mailbox operation timed out");
    return future.get();
}
Blob Node::archive(unsigned operation,const Blob& input){
    if(operation<1||operation>23||input.size()>archive::MaxApplicationBytes)throw std::invalid_argument("Invalid native archive command");
    auto response=std::make_shared<std::promise<Blob>>();auto future=response->get_future();Impl::Command command{};
    command.archive=true;command.sourceKind=operation;command.bytes=input;command.bytesResponse=response;
    if(!impl_->command(std::move(command)))throw std::runtime_error("Native profile unavailable");
    if(future.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Native archive operation timed out");
    return future.get();
}
void Node::connect(const std::wstring& host,std::uint16_t port,const std::string& expected,const std::wstring& source,const std::wstring& route,bool forceImmediate){Impl::Command c{};c.action=Impl::Action::Connect;c.text=host;c.port=port;c.peer=expected;c.source=source;c.route=route;c.forceImmediate=forceImmediate;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::invalidateRoute(const std::string& expected,const std::wstring& staleHost,std::uint16_t stalePort,bool allRoutes,std::uint64_t expectedNetworkGeneration){if(expected.empty())return;Impl::Command c{};c.action=Impl::Action::InvalidateRoute;c.peer=expected;c.text=staleHost;c.port=stalePort;c.accepted=allRoutes;c.accountId=expectedNetworkGeneration;impl_->command(std::move(c));}
void Node::setNetworkContext(std::uint64_t generation,const std::wstring& fingerprint){Impl::Command c{};c.action=Impl::Action::NetworkContext;c.accountId=generation;c.text=fingerprint;impl_->command(std::move(c));}
void Node::updateCandidates(const std::string& peer,const std::wstring& lanHost,std::uint16_t lanPort,const std::wstring& publicHost,std::uint16_t publicPort,bool relayAvailable,std::uint64_t generation,bool forceImmediate){if(peer.empty())return;Impl::Command c{};c.action=Impl::Action::Candidates;c.peer=peer;c.text=lanHost;c.port=lanPort;c.name=publicHost;c.before=publicPort;c.accepted=relayAvailable;c.accountId=generation;c.forceImmediate=forceImmediate;impl_->command(std::move(c));}
bool Node::beginScopedP2pProbe(const std::string& peer,const std::string& authorization,const std::wstring& host,std::uint16_t port,std::uint64_t expiry,std::uint64_t generation){
    auto token=[](const std::string& value){if(value.size()!=43)return false;for(char c:value)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_')return false;return true;};
    if(!protocol::id(peer)||!token(authorization)||host.empty()||!port||!expiry||!generation)return false;
    Impl::Command c{};c.action=Impl::Action::ScopedP2pProbe;c.peer=peer;c.id=authorization;c.text=host;c.port=port;c.sourceHandle=expiry;c.generation=generation;return impl_->command(std::move(c));
}
bool Node::prepareScopedP2pListener(const std::string& peer){if(!protocol::id(peer))return false;Impl::Command c{};c.action=Impl::Action::PrepareScopedP2p;c.peer=peer;return impl_->command(std::move(c));}
bool Node::promoteVerifiedP2P(const std::string& peer,const std::string& authorization){if(!protocol::id(peer)||authorization.size()!=43)return false;Impl::Command c{};c.action=Impl::Action::PromoteScopedP2p;c.peer=peer;c.id=authorization;return impl_->command(std::move(c));}
void Node::cancelScopedP2p(const std::string& peer){if(!protocol::id(peer))return;Impl::Command c{};c.action=Impl::Action::CancelScopedP2p;c.peer=peer;impl_->command(std::move(c));}
void Node::replaceAuthenticatedFriends(std::uint64_t localAccountId,std::uint64_t snapshotGeneration,std::vector<AuthenticatedFriend> friends){Impl::Command c{};c.action=Impl::Action::AuthenticatedFriends;c.accountId=localAccountId;c.sourceHandle=snapshotGeneration;c.friends=std::move(friends);impl_->command(std::move(c));}
bool Node::pinInitialAuthenticatedFriendIdentity(const std::string& peer,std::uint64_t attemptId,std::uint64_t friendSnapshotGeneration){if(!protocol::id(peer)||!attemptId||!friendSnapshotGeneration)return false;Impl::Command c{};c.action=Impl::Action::PinInitialAuthenticatedFriendIdentity;c.peer=peer;c.sourceHandle=attemptId;c.generation=friendSnapshotGeneration;return impl_->command(std::move(c));}
bool Node::relayReady(std::uint64_t attempt,bool incoming){Impl::Command c{};c.action=Impl::Action::RelayReady;c.accountId=attempt;c.accepted=incoming;return impl_->command(std::move(c));}
bool Node::relayReceive(std::uint64_t attempt,const unsigned char* bytes,std::size_t length){if(!bytes||!length||length>64*1024)return false;Impl::Command c{};c.action=Impl::Action::RelayReceive;c.accountId=attempt;c.bytes.assign(bytes,bytes+length);return impl_->command(std::move(c));}
void Node::relayClosed(std::uint64_t attempt,const std::wstring& reason){Impl::Command c{};c.action=Impl::Action::RelayClosed;c.accountId=attempt;c.text=reason;impl_->command(std::move(c));}
int Node::takeRelayOutput(std::uint64_t attempt,unsigned char* bytes,std::size_t capacity,std::uint32_t waitMilliseconds){if(!bytes||!capacity)return -1;std::shared_ptr<node_detail::RelayPipe> pipe;{std::lock_guard<std::mutex> lock(impl_->relayMutex);auto found=impl_->relayPipes.find(attempt);if(found==impl_->relayPipes.end())return -1;pipe=found->second;}std::unique_lock<std::mutex> lock(pipe->mutex);pipe->changed.wait_for(lock,std::chrono::milliseconds(waitMilliseconds),[&]{return pipe->closed||!pipe->output.empty();});if(pipe->output.empty())return pipe->closed?-1:0;auto& front=pipe->output.front();auto count=std::min(capacity,front.size());std::copy_n(front.begin(),count,bytes);front.erase(front.begin(),front.begin()+static_cast<std::ptrdiff_t>(count));pipe->queued-=count;if(front.empty())pipe->output.pop_front();return static_cast<int>(count);}
void Node::saveContact(const Contact& contact){Impl::Command c{};c.action=Impl::Action::SaveContact;c.contact=contact;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::removeContact(const std::string& peer){Impl::Command c{};c.action=Impl::Action::RemoveContact;c.peer=peer;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::approve(const std::string& peer,bool accepted){Impl::Command c{};c.action=Impl::Action::Approve;c.peer=peer;c.accepted=accepted;impl_->command(std::move(c));}
std::wstring Node::fileTransfer(unsigned operation,const std::string& peer,const std::string& id,const std::wstring& path){
    auto response=std::make_shared<std::promise<std::wstring>>();auto result=response->get_future();
    Impl::Command c{};c.action=Impl::Action::FileTransfer;c.sourceKind=operation;c.peer=peer;c.id=(operation==1||operation==7)?Storage::newMessageId():id;c.text=path;c.response=response;
    if(!impl_->command(std::move(c)))throw std::runtime_error("Peer node unavailable");
    response.reset();return result.get(); // Local bounded operation; desktop calls off its dispatcher.
}
std::wstring Node::groupAttachment(unsigned operation,const std::string& group,const std::string& id,const std::wstring& path){
    protocol::check(operation<=11&&(group.empty()?operation==0:protocol::id(group,32)));
    if(operation==7)protocol::check(transfer::safeName(id)&&path.size()<=((16*1024*1024+2)/3)*4);
    else if(operation==8)protocol::check(id.size()<=19&&std::all_of(id.begin(),id.end(),[](char c){return c>='0'&&c<='9';})&&path.empty());
    else protocol::check((id.empty()||protocol::id(id,32))&&path.size()<=32768);
    auto response=std::make_shared<std::promise<std::wstring>>();auto result=response->get_future();
    Impl::Command c{};c.action=Impl::Action::GroupAttachment;c.sourceKind=operation;c.group=group;c.id=id;c.text=path;c.response=response;
    if(!impl_->command(std::move(c)))throw std::runtime_error("Native profile unavailable");
    response.reset();return result.get();
}
Blob Node::groupAttachmentNetwork(unsigned operation,const Blob& input){
    protocol::check(operation<=3&&input.size()<=100000);
    auto response=std::make_shared<std::promise<Blob>>();auto result=response->get_future();
    Impl::Command c{};c.action=Impl::Action::GroupAttachmentNetwork;c.sourceKind=operation;c.bytes=input;c.bytesResponse=response;
    if(!impl_->command(std::move(c)))throw std::runtime_error("Native profile unavailable");
    response.reset();return result.get();
}
std::string Node::send(const std::string& peer,const std::wstring& text){
    if(!protocol::id(peer)||text.empty()||text.size()>protocol::MaxChatCharacters)return {};
    auto response=std::make_shared<std::promise<std::wstring>>();auto result=response->get_future();
    Impl::Command c{};c.action=Impl::Action::Send;c.peer=peer;c.text=text;c.id=Storage::newMessageId();c.response=response;
    if(!impl_->command(std::move(c)))return {};
    response.reset(); // A discarded command breaks its promise instead of stranding the caller.
    // No network wait, and no timeout that could report failure then commit a
    // message later. The desktop awaits this local persistence result off-thread.
    try{auto id=result.get();return std::string(id.begin(),id.end());}catch(const std::exception&){return {};}
}
void Node::loadHistory(const std::string& peer,std::int64_t before){Impl::Command c{};c.action=Impl::Action::History;c.peer=peer;c.before=before;impl_->command(std::move(c));}
bool Node::eraseHistory(const std::string& peer){if(!protocol::id(peer))return false;Impl::Command c{};c.action=Impl::Action::EraseHistory;c.peer=peer;return impl_->command(std::move(c));}
void Node::setName(const std::wstring& name){if(name.empty()||name.size()>64)return;Impl::Command c{};c.action=Impl::Action::Name;c.text=name;impl_->command(std::move(c));}
void Node::setGroupAccount(std::uint64_t accountId,const std::wstring& accountName){if(!accountId||accountName.empty()||accountName.size()>64)return;Impl::Command c{};c.action=Impl::Action::GroupAccount;c.accountId=accountId;c.name=accountName;impl_->command(std::move(c));}
bool Node::setAuthenticatedGroupRoster(std::uint64_t localAccountId,std::uint64_t discoveryGeneration,group_keys::RosterSnapshot roster){if(!localAccountId||!discoveryGeneration||!protocol::id(roster.serverGroupId,32)||!protocol::id(roster.legacyGroupId,32))return false;Impl::Command c{};c.action=Impl::Action::AuthenticatedGroupRoster;c.accountId=localAccountId;c.generation=discoveryGeneration;c.group=roster.serverGroupId;c.roster=std::move(roster);return impl_->command(std::move(c));}
void Node::clearAuthenticatedGroupRosters(){Impl::Command c{};c.action=Impl::Action::ClearGroupRosters;impl_->command(std::move(c));}
bool Node::beginGroupControl(const std::string& serverGroupId,std::uint64_t targetAccountId,const std::string& targetPeerId,std::uint64_t discoveryGeneration){if(!protocol::id(serverGroupId,32)||!targetAccountId||!protocol::id(targetPeerId)||!discoveryGeneration)return false;Impl::Command c{};c.action=Impl::Action::BeginGroupControl;c.group=serverGroupId;c.accountId=targetAccountId;c.peer=targetPeerId;c.generation=discoveryGeneration;return impl_->command(std::move(c));}
std::string Node::createGroup(const std::wstring& name){if(name.empty()||name.size()>80)return {};Impl::Command c{};c.action=Impl::Action::GroupCreate;c.group=newGroupId();c.text=name;auto id=c.group;return impl_->command(std::move(c))?id:std::string{};}
std::wstring Node::groupCreationProof(const std::string& groupId){if(!protocol::id(groupId,32))return {};auto response=std::make_shared<std::promise<std::wstring>>();auto result=response->get_future();Impl::Command c{};c.action=Impl::Action::GroupCreationProof;c.group=groupId;c.response=response;if(!impl_->command(std::move(c))||result.wait_for(std::chrono::seconds(2))!=std::future_status::ready)return {};return result.get();}
bool Node::renameGroup(const std::string& groupId,const std::wstring& name){if(!protocol::id(groupId,32)||name.empty()||name.size()>80)return false;Impl::Command c{};c.action=Impl::Action::GroupRename;c.group=groupId;c.text=name;return impl_->command(std::move(c));}
bool Node::inviteToGroup(const std::string& groupId,std::uint64_t accountId,const std::string& peerId,const std::wstring& name){if(!protocol::id(groupId,32)||!accountId||!protocol::id(peerId)||name.empty()||name.size()>64)return false;Impl::Command c{};c.action=Impl::Action::GroupInvite;c.group=groupId;c.accountId=accountId;c.peer=peerId;c.name=name;return impl_->command(std::move(c));}
bool Node::respondToGroupInvite(const std::string& groupId,bool accept){if(!protocol::id(groupId,32))return false;Impl::Command c{};c.action=Impl::Action::GroupRespond;c.group=groupId;c.accepted=accept;return impl_->command(std::move(c));}
bool Node::leaveGroup(const std::string& groupId){if(!protocol::id(groupId,32))return false;Impl::Command c{};c.action=Impl::Action::GroupLeave;c.group=groupId;return impl_->command(std::move(c));}
bool Node::removeGroupMember(const std::string& groupId,std::uint64_t accountId){if(!protocol::id(groupId,32)||!accountId)return false;Impl::Command c{};c.action=Impl::Action::GroupRemove;c.group=groupId;c.accountId=accountId;return impl_->command(std::move(c));}
bool Node::setGroupAdmin(const std::string& groupId,std::uint64_t accountId,bool admin){if(!protocol::id(groupId,32)||!accountId)return false;Impl::Command c{};c.action=Impl::Action::GroupRole;c.group=groupId;c.accountId=accountId;c.accepted=admin;return impl_->command(std::move(c));}
bool Node::sendGroup(const std::string& groupId,const std::wstring& text){if(!protocol::id(groupId,32)||text.empty()||text.size()>protocol::MaxChatCharacters)return false;Impl::Command c{};c.action=Impl::Action::GroupSend;c.group=groupId;c.text=text;return impl_->command(std::move(c));}
bool Node::receiveGroupCiphertext(const Blob& wire){if(wire.size()<254||wire.size()>group_messages::MaxEnvelopeBytes)return false;Impl::Command c{};c.action=Impl::Action::GroupReceiveCiphertext;c.bytes=wire;return impl_->command(std::move(c));}
bool Node::groupMessageStatus(const std::string& group,const std::string& message,const std::wstring& status,std::uint32_t stage){if(!protocol::id(group,32)||!protocol::id(message,32)||status.empty()||status.size()>160||stage>2)return false;Impl::Command c{};c.action=Impl::Action::GroupMessageStatus;c.group=group;c.id=message;c.text=status;c.sourceKind=stage;return impl_->command(std::move(c));}
void Node::loadGroups(){Impl::Command c{};c.action=Impl::Action::Groups;impl_->command(std::move(c));}
void Node::loadGroupHistory(const std::string& group,std::int64_t before){if(!protocol::id(group,32))return;Impl::Command c{};c.action=Impl::Action::GroupHistoryPage;c.group=group;c.before=before;impl_->command(std::move(c));}
std::string Node::startVoiceCall(const std::string& peerId){if(!protocol::id(peerId))return {};Impl::Command command{};command.action=Impl::Action::VoiceStart;command.peer=peerId;command.id=Storage::newMessageId();auto id=command.id;return impl_->command(std::move(command))?id:std::string{};}
std::string Node::startScreenShare(const std::string& peerId,std::uint32_t sourceKind,std::uint64_t sourceHandle,std::uint32_t fps,std::uint32_t bitrate,bool captureCursor,screen::Backend backend){
    if(!protocol::id(peerId)||!sourceHandle||(sourceKind!=PC_SCREEN_MONITOR&&sourceKind!=PC_SCREEN_WINDOW)||fps<5||fps>60||bitrate<250000||bitrate>50000000)return {};
    Impl::Command command{};command.action=Impl::Action::ScreenStart;command.peer=peerId;command.id=Storage::newMessageId();command.sourceKind=sourceKind;command.sourceHandle=sourceHandle;command.fps=fps;command.bitrate=bitrate;command.accepted=captureCursor;command.screenBackend=backend;
    auto id=command.id;return impl_->command(std::move(command))?id:std::string{};
}
bool Node::setScreenShareBackend(screen::Backend backend){if(backend!=screen::Backend::NativeWindows&&backend!=screen::Backend::WebRtcCompatibility)return false;Impl::Command command{};command.action=Impl::Action::ScreenBackend;command.screenBackend=backend;return impl_->command(std::move(command));}
bool Node::answerScreenShare(const std::string& shareId,std::uint64_t presentationWindow,screen::Backend backend){if(!protocol::id(shareId,32)||!presentationWindow)return false;Impl::Command command{};command.action=Impl::Action::ScreenAnswer;command.id=shareId;command.sourceHandle=presentationWindow;command.screenBackend=backend;return impl_->command(std::move(command));}
bool Node::declineScreenShare(const std::string& shareId){if(!protocol::id(shareId,32))return false;Impl::Command command{};command.action=Impl::Action::ScreenDecline;command.id=shareId;return impl_->command(std::move(command));}
bool Node::endScreenShare(const std::string& shareId){if(!protocol::id(shareId,32))return false;Impl::Command command{};command.action=Impl::Action::ScreenEnd;command.id=shareId;return impl_->command(std::move(command));}
std::wstring Node::screenShareDiagnostics(){
    auto stage=impl_->screenStopStage.load();
    auto stageSince=impl_->screenStopStageSince.load();
    auto stopDetail=[&](bool timedOut){
        const wchar_t* name=L"not stopping";
        switch(stage){
        case 100:name=L"reading media counters before stop";break;
        case 1:name=L"retiring media callbacks";break;
        case 2:name=L"detaching peer-connection callbacks";break;
        case 3:name=L"detaching track callbacks";break;
        case 4:name=L"draining RTP callback";break;
        case 5:name=L"closing capture and encoder";break;
        case 6:name=L"closing decoder and presentation";break;
        case 7:name=L"deleting tracks";break;
        case 8:name=L"closing ICE/media connection";break;
        case 9:name=L"unloading media adapter";break;
        case 10:name=L"completed";break;
        }
        const auto now=GetTickCount64();
        return std::wstring(L"\nscreen_stop_stage=")+std::to_wstring(stage)+
            L"\nscreen_stop_stage_name="+name+
            L"\nscreen_stop_stage_elapsed_ms="+std::to_wstring(stageSince&&now>=stageSince?now-stageSince:0)+
            L"\nscreen_diagnostics_worker_timeout="+(timedOut?L"true":L"false");
    };
    auto response=std::make_shared<std::promise<std::wstring>>();auto result=response->get_future();
    Impl::Command command{};command.action=Impl::Action::ScreenDiagnostics;command.response=response;
    if(!impl_->command(std::move(command))||result.wait_for(std::chrono::seconds(2))!=std::future_status::ready){
        stage=impl_->screenStopStage.load();stageSince=impl_->screenStopStageSince.load();
        return L"screen_media_state=unavailable"+stopDetail(true);
    }
    stage=impl_->screenStopStage.load();stageSince=impl_->screenStopStageSince.load();
    return result.get()+stopDetail(false);
}
bool Node::answerVoiceCall(const std::string& callId){if(!protocol::id(callId,32))return false;Impl::Command command{};command.action=Impl::Action::VoiceAnswer;command.id=callId;return impl_->command(std::move(command));}
bool Node::declineVoiceCall(const std::string& callId){if(!protocol::id(callId,32))return false;Impl::Command command{};command.action=Impl::Action::VoiceDecline;command.id=callId;return impl_->command(std::move(command));}
bool Node::endVoiceCall(const std::string& callId){if(!protocol::id(callId,32))return false;Impl::Command command{};command.action=Impl::Action::VoiceEnd;command.id=callId;return impl_->command(std::move(command));}
bool Node::setVoiceMuted(bool muted){Impl::Command command{};command.action=Impl::Action::VoiceMute;command.accepted=muted;return impl_->command(std::move(command));}
bool Node::setVoiceDeafened(bool deafened){Impl::Command command{};command.action=Impl::Action::VoiceDeafen;command.accepted=deafened;return impl_->command(std::move(command));}
bool Node::setVoiceDevices(int inputDevice,int outputDevice){if(inputDevice < -1||outputDevice < -1)return false;Impl::Command command{};command.action=Impl::Action::VoiceDevices;command.port=static_cast<std::uint16_t>(inputDevice+1);command.before=outputDevice;return impl_->command(std::move(command));}
bool Node::setVoiceVolumes(int inputPercent,int outputPercent){if(inputPercent<0||inputPercent>200||outputPercent<0||outputPercent>200)return false;Impl::Command command{};command.action=Impl::Action::VoiceLevels;command.port=static_cast<std::uint16_t>(inputPercent);command.before=outputPercent;return impl_->command(std::move(command));}
bool Node::setVoiceProcessing(bool enabled,bool noiseSuppression,bool automaticGain,std::uint32_t noiseSuppressionStrength,std::uint32_t automaticGainStrength,bool echoCancellation){if(noiseSuppressionStrength>PC_APM_NS_HIGH||automaticGainStrength>PC_APM_AGC_STRONG)return false;Impl::Command command{};command.action=Impl::Action::VoiceProcessing;command.port=static_cast<std::uint16_t>((enabled?1u:0u)|(noiseSuppression?2u:0u)|(automaticGain?4u:0u)|(echoCancellation?8u:0u));command.noiseSuppressionStrength=noiseSuppressionStrength;command.automaticGainStrength=automaticGainStrength;return impl_->command(std::move(command));}
bool Node::setMediaIceConfiguration(MediaIceConfiguration configuration){
    if(!configuration.valid(static_cast<std::int64_t>(std::time(nullptr)))
#ifdef PEERCHAT_TEST_MEDIA_DIRECT
       && configuration.policy!=MediaIceConfiguration::Policy::DirectTestOnly
#endif
    )return false;
    Impl::Command command{};command.action=Impl::Action::MediaIceConfiguration;command.ice=std::move(configuration);
    return impl_->command(std::move(command));
}
std::wstring Node::voiceDiagnostics(){auto response=std::make_shared<std::promise<std::wstring>>();auto result=response->get_future();Impl::Command command{};command.action=Impl::Action::VoiceDiagnostics;command.response=response;if(!impl_->command(std::move(command))||result.wait_for(std::chrono::seconds(2))!=std::future_status::ready)return L"media_state=unavailable\nlatest_audio_error=diagnostic request timed out";return result.get();}
MediaTelemetry Node::voiceTelemetry() const {MediaTelemetry result;result.inputLevel=impl_->voiceInputLevel.load();result.outputLevel=impl_->voiceOutputLevel.load();auto flags=impl_->voiceTelemetryFlags.load();result.active=(flags&1u)!=0;result.inputAvailable=(flags&2u)!=0;result.outputAvailable=(flags&4u)!=0;return result;}
std::vector<NodeEvent> Node::poll(){std::lock_guard<std::mutex> lock(impl_->eventMutex);std::vector<NodeEvent> events;while(!impl_->events.empty()){events.push_back(std::move(impl_->events.front()));impl_->events.pop_front();}return events;}
}
