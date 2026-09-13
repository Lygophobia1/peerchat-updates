#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define FD_SETSIZE 256
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include "node.hpp"
#include "direct_transport_adapter.hpp"
#include "session_transport_manager.hpp"
#include "tls.hpp"
#include "protocol.hpp"
#include "voice_media.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
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
struct Connection {
    SOCKET socket=INVALID_SOCKET;
    bool incoming=false,connecting=false,dead=false,hello=false,approved=false,remoteApproved=false,online=false,logicalOrdering=false,signedGroups=false,voiceCalls=false,identityMismatch=false;
    std::wstring host,name,source,route,closeReason;std::uint16_t port=0;std::uint64_t attemptId=0;
    std::string expected,peerId;
    std::unique_ptr<TlsSession> tls;
    Blob output,input;size_t sent=0;
    Clock::time_point began=Clock::now(),lastProgress=Clock::now();
    std::set<std::string> inflight;
    std::set<std::string> groupInflight;
    std::optional<session::DirectTransportAdapter> directTransport;
    ~Connection(){if(socket!=INVALID_SOCKET)closesocket(socket);}
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
    enum class Action { Connect,InvalidateRoute,NetworkContext,SaveContact,RemoveContact,Approve,Send,History,Name,GroupAccount,GroupCreate,GroupRename,GroupInvite,GroupRespond,GroupLeave,GroupRemove,GroupRole,GroupSend,Groups,GroupHistoryPage,VoiceStart,VoiceAnswer,VoiceDecline,VoiceEnd,VoiceMute,VoiceDeafen,VoiceDevices,VoiceDiagnostics };
    struct Command{Action action;std::string peer,id,group;std::wstring text,name,source,route;std::uint16_t port=0;bool accepted=false,forceImmediate=false;std::int64_t before=INT64_MAX;std::uint64_t accountId=0;Contact contact;std::shared_ptr<std::promise<std::wstring>> response;};
    std::atomic<bool> running{false};std::thread worker;std::mutex commandMutex,eventMutex;
    std::deque<Command> commands;std::deque<NodeEvent> events;
    std::unique_ptr<Storage> store;std::unique_ptr<TlsCredentials> credentials;Profile profile;
    std::map<std::string,Contact> contacts;
    std::map<std::string,GroupLedger> groups;
    GroupAccount groupAccount;
    std::vector<std::unique_ptr<Connection>> connections;
    std::vector<std::unique_ptr<Resolver>> resolvers;
    struct RetryState {Clock::time_point due{};std::uint32_t failures=0;std::wstring host,source,route;std::uint16_t port=0;bool suspended=false;};
    std::map<std::string,RetryState> reconnect;
    std::uint64_t nextAttemptId=1;
    std::uint64_t nextSessionEpoch=1;
    std::uint64_t nextRouteRevision=1;
    session::SessionEpoch sessionEpoch{};
    std::unique_ptr<session::SessionTransportManager> sessionTransports;
    enum class VoiceState { Idle,Outgoing,Ringing,Connecting,Connected,Ending };
    VoiceState voiceState=VoiceState::Idle;std::string voiceCallId,voicePeerId,voiceOffer;Clock::time_point voiceDeadline{};bool voiceOutgoing=false;
    std::uint64_t networkGeneration=0,voiceNetworkGeneration=0,voiceConnectionAtStart=0,voiceRequestSentOn=0,voiceRequestReceivedOn=0,voiceOfferSentOn=0,voiceOfferReceivedOn=0,voiceAnswerSentOn=0,voiceAnswerReceivedOn=0,voiceLastSignalConnection=0,voiceCandidatesSent=0,voiceCandidatesReceived=0;
    std::wstring networkFingerprint,voiceNetworkFingerprint;
    std::unique_ptr<VoiceMedia> voiceMedia;std::vector<std::pair<std::string,std::string>> voiceCandidates;std::wstring lastVoiceDiagnostics=L"media_state=idle\nice_dtls_srtp_state=idle\ntrack_open=false\ncodec=Opus not active\ntransport=not active\ncapture_frames=0\nencoded_frames=0\nopus_submitted=0\nsend_api_accepted=0\nrtp_transport_packets_sent=not exposed by pinned C API\nrtp_transport_bytes_sent=not exposed by pinned C API\nremote_track_callback_packets=0\npayload_delivered=0\nreceived_packets=0\ndecoded_frames=0\nplayback_frames=0\nlatest_audio_error=none\nlatest_media_error=none";
    int voiceInputDevice=-1,voiceOutputDevice=-1;
    std::atomic<float> voiceInputLevel{0.0f},voiceOutputLevel{0.0f};
    std::atomic<unsigned> voiceTelemetryFlags{0};
    std::deque<std::string> finishedVoiceCalls;
    SOCKET listener=INVALID_SOCKET;
    ~Impl(){stop();}
    void emit(NodeEvent e){std::lock_guard<std::mutex> lock(eventMutex);if(events.size()>=4096)events.pop_front();events.push_back(std::move(e));}
    void emitGroupHistoryPage(const std::string& groupId,std::int64_t before){
        auto found=groups.find(groupId);if(found==groups.end())throw std::runtime_error("Unknown Group ID");
        const auto& state=found->second.state();
        if(!groupAccount.accountId||(!state.members.count(groupAccount.accountId)&&!state.invitations.count(groupAccount.accountId)))return;
        std::vector<const GroupEvent*> eligible;
        for(const auto& event:state.messages)if(event.logicalOrder<static_cast<std::uint64_t>(before))eligible.push_back(&event);
        std::sort(eligible.begin(),eligible.end(),[](const GroupEvent* left,const GroupEvent* right){return left->logicalOrder!=right->logicalOrder?left->logicalOrder<right->logicalOrder:left->eventId<right->eventId;});
        constexpr size_t PageSize=100;
        if(eligible.size()>PageSize)eligible.erase(eligible.begin(),eligible.end()-PageSize);
        NodeEvent history;history.kind=NodeEvent::Kind::GroupHistory;history.peerId=groupId;
        for(const auto* event:eligible)history.groupEvents.push_back(*event);
        emit(std::move(history));
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
    session::RouteObservation routeObservation(const Connection& connection,session::RouteRevision revision)const{
        session::RouteObservation route;
        route.revision=revision;
        route.endpointUtf8=utf8(endpoint(connection.host,connection.port));
        route.sourceUtf8=utf8(connection.source);
        route.routeUtf8=utf8(connection.route);
        route.incoming=connection.incoming;
        return route;
    }
    void attachDirectTransport(Connection& connection)noexcept{
        if(!sessionTransports||connection.directTransport)return;
        try{
            connection.directTransport.emplace(sessionEpoch,session::TransportInstanceId{connection.attemptId},routeObservation(connection,{nextRouteRevision++}),connection.expected);
            sessionTransports->observeLegacyDecision(connection.directTransport->connecting());
        }catch(...){connection.directTransport.reset();}
    }
    void project(Connection& connection,const session::TransportEvent& event)noexcept{
        if(!sessionTransports||!connection.directTransport)return;
        try{sessionTransports->observeLegacyDecision(event);}catch(...){}
    }
    NodeEvent connectionEvent(NodeEvent::Kind kind,const std::wstring& text,const Connection& connection,const std::string& peer={}){NodeEvent event;event.kind=kind;event.text=text;event.detail=connection.incoming?L"inbound":L"outbound";event.peerId=peer.empty()?connection.expected:peer;event.expectedPeerId=connection.expected;event.candidate=endpoint(connection.host,connection.port);event.candidateSource=connection.source;event.route=connection.route;event.attemptId=connection.attemptId;return event;}
#ifdef PEERCHAT_TEST_SHORT_RECONNECT_BACKOFF
    static constexpr std::uint64_t ReconnectBaseMilliseconds=80,ReconnectCapMilliseconds=800;
#else
    static constexpr std::uint64_t ReconnectBaseMilliseconds=5000,ReconnectCapMilliseconds=300000;
#endif
    std::uint32_t backoffDelay(std::uint32_t failures){std::uint64_t base=ReconnectBaseMilliseconds;for(std::uint32_t i=1;i<failures&&base<ReconnectCapMilliseconds;i++)base=std::min<std::uint64_t>(base*2,ReconnectCapMilliseconds);std::uint32_t random=0;BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&random),sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG);std::uint64_t varied=base*(80+(random%41))/100;return static_cast<std::uint32_t>(std::min<std::uint64_t>(varied,ReconnectCapMilliseconds));}
    void scheduleReconnect(const std::string& peer,const std::wstring& host,std::uint16_t port,const std::wstring& source,const std::wstring& route,bool suspend=false){if(peer.empty()||!contacts.count(peer)||host.empty()||!port)return;auto& state=reconnect[peer];state.host=host;state.port=port;state.source=source.empty()?L"Saved contact":source;state.route=route.empty()?L"automatic reconnect":route;if(suspend){state.suspended=true;state.due=Clock::time_point::max();return;}state.suspended=false;state.failures=std::min<std::uint32_t>(state.failures+1,31);auto delay=backoffDelay(state.failures);state.due=Clock::now()+std::chrono::milliseconds(delay);NodeEvent event;event.kind=NodeEvent::Kind::ReconnectScheduled;event.peerId=peer;event.expectedPeerId=peer;event.candidate=endpoint(host,port);event.candidateSource=state.source;event.route=state.route;event.retryDelayMilliseconds=delay;event.text=L"Reconnect scheduled";emit(std::move(event));}
    static const wchar_t* voiceStateName(VoiceState state){switch(state){case VoiceState::Idle:return L"Idle";case VoiceState::Outgoing:return L"Outgoing / Ringing";case VoiceState::Ringing:return L"Incoming / Ringing";case VoiceState::Connecting:return L"Connecting";case VoiceState::Connected:return L"Connected";case VoiceState::Ending:return L"Ending";}return L"Idle";}
    void voiceEvent(VoiceState state,const std::wstring& reason={}){voiceState=state;NodeEvent event;event.kind=NodeEvent::Kind::VoiceCall;event.peerId=voicePeerId;event.detail=std::wstring(voiceCallId.begin(),voiceCallId.end());event.text=voiceStateName(state);if(!reason.empty())event.text+=L" · "+reason;emit(std::move(event));}
    Connection* voiceConnection(const std::string& peer){for(auto& connection:connections)if(connection->online&&!connection->dead&&connection->peerId==peer&&connection->voiceCalls)return connection.get();return nullptr;}
    void resetVoiceSignaling(Connection* connection=nullptr){voiceNetworkGeneration=networkGeneration;voiceNetworkFingerprint=networkFingerprint;voiceConnectionAtStart=connection?connection->attemptId:0;voiceRequestSentOn=voiceRequestReceivedOn=voiceOfferSentOn=voiceOfferReceivedOn=voiceAnswerSentOn=voiceAnswerReceivedOn=voiceLastSignalConnection=voiceCandidatesSent=voiceCandidatesReceived=0;}
    std::wstring voiceSignalingDiagnostics()const{std::wostringstream out;out<<L"winning_peerchat_connection_id_at_call_start="<<voiceConnectionAtStart<<L"\nvoice_network_generation="<<voiceNetworkGeneration<<L"\ncurrent_network_generation="<<networkGeneration<<L"\npeer_connection_matches_current_network="<<(voiceNetworkGeneration==networkGeneration?L"true":L"false")<<L"\nvoice_network_fingerprint="<<voiceNetworkFingerprint<<L"\ncall_request_sent_connection_id="<<voiceRequestSentOn<<L"\ncall_request_received_connection_id="<<voiceRequestReceivedOn<<L"\ncall_answer_sent_connection_id="<<voiceAnswerSentOn<<L"\ncall_answer_received_connection_id="<<voiceAnswerReceivedOn<<L"\nsdp_offer_created="<<(voiceOfferSentOn?L"true":L"false")<<L"\nsdp_offer_sent_connection_id="<<voiceOfferSentOn<<L"\nsdp_offer_received_connection_id="<<voiceOfferReceivedOn<<L"\nsdp_answer_created="<<(voiceAnswerSentOn?L"true":L"false")<<L"\nsdp_answer_sent_connection_id="<<voiceAnswerSentOn<<L"\nsdp_answer_received_connection_id="<<voiceAnswerReceivedOn<<L"\nlast_voice_signaling_connection_id="<<voiceLastSignalConnection<<L"\nlocal_ice_candidates_sent="<<voiceCandidatesSent<<L"\nremote_ice_candidates_received="<<voiceCandidatesReceived;return out.str();}
    void rememberVoiceCall(const std::string& id){if(id.empty())return;finishedVoiceCalls.push_back(id);while(finishedVoiceCalls.size()>64)finishedVoiceCalls.pop_front();}
    bool finishedVoiceCall(const std::string& id)const{return std::find(finishedVoiceCalls.begin(),finishedVoiceCalls.end(),id)!=finishedVoiceCalls.end();}
    void resetVoiceTelemetry(){voiceInputLevel=0.0f;voiceOutputLevel=0.0f;voiceTelemetryFlags=0;}
    void publishVoiceTelemetry(){if(!voiceMedia){resetVoiceTelemetry();return;}auto telemetry=voiceMedia->telemetry();voiceInputLevel=telemetry.inputLevel;voiceOutputLevel=telemetry.outputLevel;voiceTelemetryFlags=(telemetry.active?1u:0u)|(telemetry.inputAvailable?2u:0u)|(telemetry.outputAvailable?4u:0u);}
    void clearVoice(const std::wstring& reason){if(voiceState==VoiceState::Idle&&!voiceMedia&&voiceCallId.empty())return;voiceEvent(VoiceState::Ending,reason);resetVoiceTelemetry();lastVoiceDiagnostics=voiceSignalingDiagnostics();if(voiceMedia){lastVoiceDiagnostics+=L"\n"+voiceMedia->diagnostics();voiceMedia->stop();}voiceMedia.reset();rememberVoiceCall(voiceCallId);voiceCallId.clear();voicePeerId.clear();voiceOffer.clear();voiceCandidates.clear();voiceOutgoing=false;voiceDeadline={};voiceEvent(VoiceState::Idle,reason);}
    void mediaEvents(){
        if(!voiceMedia||voiceState==VoiceState::Idle)return;
        auto connection=voiceConnection(voicePeerId);
        for(auto& event:voiceMedia->poll()){
            if(event.kind==VoiceMedia::EventKind::Description){
                if(!connection){clearVoice(L"Peer disconnected");return;}
                voiceLastSignalConnection=connection->attemptId;if(event.detail=="offer")voiceRequestSentOn=voiceOfferSentOn=connection->attemptId;else voiceAnswerSentOn=connection->attemptId;
                protocol::Writer signal(event.detail=="offer"?protocol::CallOffer:protocol::CallAnswer);signal.string(voiceCallId);signal.string(event.value);packet(*connection,signal);
            }else if(event.kind==VoiceMedia::EventKind::Candidate){
                if(connection){voiceLastSignalConnection=connection->attemptId;++voiceCandidatesSent;protocol::Writer signal(protocol::CallCandidate);signal.string(voiceCallId);signal.string(event.value);signal.string(event.detail);packet(*connection,signal);}
            }else if(event.kind==VoiceMedia::EventKind::Connected){
                voiceDeadline={};voiceEvent(VoiceState::Connected,L"Direct WebRTC · Opus · DTLS-SRTP");
                if(connection){protocol::Writer ready(protocol::CallConnected);ready.string(voiceCallId);packet(*connection,ready);}
            }else if(event.kind==VoiceMedia::EventKind::AudioError){
                notice(NodeEvent::Kind::Error,L"Voice audio device error: "+std::wstring(event.value.begin(),event.value.end()),voicePeerId);
            }else {if(connection){protocol::Writer end(protocol::CallEnd);end.string(voiceCallId);packet(*connection,end);}clearVoice(event.kind==VoiceMedia::EventKind::Failed?L"Secure media connection failed":L"Media disconnected");return;}
        }
        publishVoiceTelemetry();
    }
    void tcpFailure(Connection& c,int error){
        std::wstring text=error==WSAECONNREFUSED?L"TCP connection refused: the peer is not listening on this port or Firewall blocked it.":error==WSAETIMEDOUT?L"TCP connection timeout: the peer or Firewall did not respond.":error==WSAEHOSTUNREACH||error==WSAENETUNREACH?L"Peer unreachable/offline: Windows has no network route to the peer endpoint.":L"TCP connection failed: check the peer address, listener, and Firewall.";
        emit(connectionEvent(NodeEvent::Kind::Error,text,c));
    }
    bool command(Command c){std::lock_guard<std::mutex> lock(commandMutex);if(!running||commands.size()>=512)return false;commands.push_back(std::move(c));return true;}
    void publishContacts(){NodeEvent e;e.kind=NodeEvent::Kind::Contacts;for(const auto& item:contacts)e.contacts.push_back(item.second);emit(std::move(e));}
    void publishGroups(){NodeEvent event;event.kind=NodeEvent::Kind::Groups;if(groupAccount.accountId)for(const auto& item:groups){const auto& state=item.second.state();if(state.members.count(groupAccount.accountId)||state.invitations.count(groupAccount.accountId))event.groups.push_back(state);}emit(std::move(event));}
    void stop(){running=false;if(worker.joinable())worker.join();std::lock_guard<std::mutex> lock(commandMutex);commands.clear();}
    bool exists(const std::string& id)const {for(const auto& c:connections)if(!c->dead&&((!id.empty()&&c->peerId==id)||(!id.empty()&&c->expected==id)))return true;for(const auto& r:resolvers)if(!id.empty()&&r->expected==id)return true;return false;}
    void resolve(const std::wstring& host,std::uint16_t port,const std::string& expected,const std::wstring& source=L"Direct",const std::wstring& route=L"manual"){
        if(host.empty()||host.size()>253||!port||(!expected.empty()&&!protocol::id(expected)))throw std::runtime_error("Invalid peer address");
        if(exists(expected))return;
        if(connections.size()+resolvers.size()>=MaxConnections||resolvers.size()>=8)throw std::runtime_error("Too many pending connections");
        std::uint64_t attemptId=nextAttemptId++;NodeEvent attempt;attempt.kind=NodeEvent::Kind::ConnectionAttempt;attempt.peerId=expected;attempt.expectedPeerId=expected;attempt.candidate=endpoint(host,port);attempt.candidateSource=source;attempt.route=route;attempt.attemptId=attemptId;attempt.text=L"Connection attempt started";emit(std::move(attempt));
        // A copied LAN IPv4 address should never depend on DNS or the async
        // resolver. This also avoids resolver differences between Windows PCs.
        IN_ADDR numeric{};
        if(InetPtonW(AF_INET,host.c_str(),&numeric)==1){
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr=numeric;
            auto connection=std::make_unique<Connection>();connection->host=host;connection->port=port;connection->expected=expected;connection->source=source;connection->route=route;connection->attemptId=attemptId;attachDirectTransport(*connection);connection->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(connection->socket==INVALID_SOCKET){emit(connectionEvent(NodeEvent::Kind::Error,L"Could not create a connection socket.",*connection));scheduleReconnect(expected,host,port,source,route);return;}
            nonblocking(connection->socket);int result=::connect(connection->socket,reinterpret_cast<sockaddr*>(&address),sizeof(address));int error=result==0?0:WSAGetLastError();
            if(result==0||error==WSAEWOULDBLOCK||error==WSAEINPROGRESS){connection->connecting=result!=0;connections.push_back(std::move(connection));}
            else {tcpFailure(*connection,error);closesocket(connection->socket);connection->socket=INVALID_SOCKET;scheduleReconnect(expected,host,port,source,route);}
            return;
        }
        auto r=std::make_unique<Resolver>(host,port,expected,source,route,attemptId);int status=r->begin();
        if(status!=0&&status!=WSA_IO_PENDING){Connection failed;failed.host=host;failed.port=port;failed.expected=expected;failed.source=source;failed.route=route;failed.attemptId=attemptId;emit(connectionEvent(NodeEvent::Kind::Error,L"DNS/hostname failure: could not resolve "+host+L":"+std::to_wstring(port)+L".",failed));scheduleReconnect(expected,host,port,source,route);return;}
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
                    else {int error=WSAGetLastError();tcpFailure(*c,error);closesocket(c->socket);c->socket=INVALID_SOCKET;scheduleReconnect(r.expected,r.host,r.port,r.source,r.route);}
                }else {emit(connectionEvent(NodeEvent::Kind::Error,L"Could not create a connection socket.",*c));scheduleReconnect(r.expected,r.host,r.port,r.source,r.route);}
            }else {Connection failed;failed.host=r.host;failed.port=r.port;failed.expected=r.expected;failed.source=r.source;failed.route=r.route;failed.attemptId=r.attemptId;emit(connectionEvent(NodeEvent::Kind::Error,L"DNS/hostname failure: lookup failed or timed out for "+r.host+L":"+std::to_wstring(r.port)+L".",failed));scheduleReconnect(r.expected,r.host,r.port,r.source,r.route);}
            it=resolvers.erase(it);
        }
    }
    void collect(Connection& c){auto bytes=c.tls->takeOutput();if(c.output.size()-c.sent+bytes.size()>MaxQueue)throw std::runtime_error("Peer is not consuming outgoing data");if(c.sent){c.output.erase(c.output.begin(),c.output.begin()+c.sent);c.sent=0;}c.output.insert(c.output.end(),bytes.begin(),bytes.end());}
    void packet(Connection& c,protocol::Writer& writer){auto framed=writer.frame();c.tls->send(framed);if(c.directTransport)project(c,c.directTransport->observedSend(framed.size()));collect(c);}
    void packet(Connection& c,const Blob& value){auto framed=protocol::frame(value);c.tls->send(framed);if(c.directTransport)project(c,c.directTransport->observedSend(framed.size()));collect(c);}
    void outbox(Connection& c){
        if(!c.online)return;
        for(const auto& m:store->pending(c.peerId,100)) {
            if(c.inflight.count(m.id))continue;
            if(c.inflight.size()>=16||c.output.size()-c.sent>MaxQueue/2)break;
            protocol::Writer w(protocol::Text);w.string(m.id);if(c.logicalOrdering)w.number(m.logicalOrder);w.number(m.utcMilliseconds);w.text(m.text);packet(c,w);c.inflight.insert(m.id);
        }
    }
    std::set<std::string> groupPeers(const GroupState& state){std::set<std::string> peers;for(const auto& member:state.members)peers.insert(member.second.peerId);for(const auto& invitation:state.invitations)peers.insert(invitation.second.peerId);peers.erase(profile.peerId);return peers;}
    void persistAccepted(GroupLedger& ledger){for(const auto& event:ledger.events())if(ledger.accepted(event.eventId))store->appendGroupEvent(event);}
    void queueGroupEvent(const GroupEvent& event,const std::set<std::string>& peers){for(const auto& peer:peers)if(protocol::id(peer)&&peer!=profile.peerId)store->queueGroupDelivery(event.eventId,peer);}
    void queueGroupSync(const GroupLedger& ledger,const std::string& peer){for(const auto& event:ledger.events())if(ledger.accepted(event.eventId))store->queueGroupDelivery(event.eventId,peer);}
    void groupOutbox(Connection& connection){
        if(!connection.online||!connection.signedGroups)return;
        for(const auto& event:store->pendingGroupEvents(connection.peerId,64)){
            if(connection.groupInflight.count(event.eventId))continue;
            if(connection.inflight.size()+connection.groupInflight.size()>=16||connection.output.size()-connection.sent>MaxQueue/2)break;
            packet(connection,encodeGroupEvent(event));connection.groupInflight.insert(event.eventId);
        }
    }
    void online(Connection& c){
        if(c.online||!c.approved||!c.remoteApproved)return;
        for(auto& other:connections)if(other.get()!=&c&&!other->dead&&other->online&&other->peerId==c.peerId){
            bool existingStale=Clock::now()-other->lastProgress>std::chrono::seconds(2);
            if(!existingStale&&c.incoming==(profile.peerId<c.peerId)){c.closeReason=L"duplicate connection loser (deterministic direction)";c.dead=true;emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,c.closeReason,c,c.peerId));return;}
            other->closeReason=existingStale?L"duplicate connection superseded stale authenticated session":L"duplicate connection loser (deterministic direction)";other->dead=true;emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,other->closeReason,*other,other->peerId));
        }
        c.online=true;if(c.directTransport)project(c,c.directTransport->activated());reconnect.erase(c.peerId);if(!c.expected.empty())reconnect.erase(c.expected);emit(connectionEvent(NodeEvent::Kind::Connected,c.name,c,c.peerId));outbox(c);groupOutbox(c);
    }
    void approve(Connection& c){
        c.approved=true;if(c.directTransport)project(c,c.directTransport->locallyTrusted());protocol::Writer ready(protocol::Accepted);packet(c,ready);online(c);
    }
    void process(Connection& c,const Blob& frame){
        protocol::Reader r(frame);
        if(!c.hello){
            protocol::check(r.type()==protocol::Hello);auto capabilities=r.number();auto port=r.number();c.name=r.text();r.end();
            protocol::check((capabilities&protocol::CapabilityMutualTls)!=0&&port>0&&port<=65535&&c.name.size()<=64);c.logicalOrdering=(capabilities&protocol::CapabilityLogicalOrdering)!=0;c.signedGroups=(capabilities&protocol::CapabilitySignedGroups)!=0;c.voiceCalls=(capabilities&protocol::CapabilityVoiceCalls)!=0;c.hello=true;
            c.peerId=Storage::publicKeyId(c.tls->remotePublicKey());protocol::check(c.peerId!=profile.peerId);
            c.port=static_cast<std::uint16_t>(port);if(c.host.empty())c.host=observedAddress(c.socket);
            if(c.directTransport){c.directTransport->updateRoute(routeObservation(c,c.directTransport->routeRevision()));project(c,c.directTransport->identityPresented(c.peerId,capabilities));}
            // A changed identity is a security-relevant verification event, not a silent
            // transport rejection. The UI must show the expected and presented public IDs and
            // require an explicit decision. No private key, password, or message content is
            // included. The presented ID becomes trusted only if approve() is later called.
            if(!c.expected.empty()&&c.expected!=c.peerId){
                // Suspend as soon as identity mismatch is known. Waiting for dead
                // socket cleanup permits a queued retry to race a rejection.
                auto& retry=reconnect[c.expected];retry.suspended=true;retry.due=Clock::time_point::max();
                c.identityMismatch=true;NodeEvent changed=connectionEvent(NodeEvent::Kind::TrustRequired,L"",c,c.peerId);
                changed.text=L"Peer identity changed. Expected "+std::wstring(c.expected.begin(),c.expected.end())+L" but the peer presented "+std::wstring(c.peerId.begin(),c.peerId.end())+L". Verify this new device identity before trusting it.";
                changed.contacts.push_back({c.peerId,c.name,c.tls->remotePublicKey(),{{c.host,c.port}},false});emit(std::move(changed));return;
            }
            auto known=contacts.find(c.peerId);
            if(known!=contacts.end()&&known->second.verified){
                protocol::check(known->second.publicKey==c.tls->remotePublicKey());
                Contact contact=known->second;contact.name=c.name;
                auto endpoint=std::find_if(contact.endpoints.begin(),contact.endpoints.end(),[&](const Endpoint& e){return e.host==c.host&&e.port==c.port;});
                if(endpoint==contact.endpoints.end()&&contact.endpoints.size()<128)contact.endpoints.push_back({c.host,c.port});
                store->saveContact(contact);contacts[c.peerId]=contact;publishContacts();approve(c);
            }else{NodeEvent e=connectionEvent(NodeEvent::Kind::TrustRequired,c.name,c,c.peerId);e.contacts.push_back({c.peerId,c.name,c.tls->remotePublicKey(),{{c.host,c.port}},false});emit(std::move(e));}
            return;
        }
        if(r.type()==protocol::Accepted){r.end();protocol::check(!c.remoteApproved);c.remoteApproved=true;online(c);return;}
        protocol::check(c.online);
        if(r.type()==protocol::CallOffer){
            auto callId=r.string();auto sdp=r.string();r.end();protocol::check(c.voiceCalls&&protocol::id(callId,32)&&!sdp.empty()&&sdp.size()<=32768);if(finishedVoiceCall(callId))return;
            if(voiceState==VoiceState::Idle){resetVoiceSignaling(&c);voiceRequestReceivedOn=voiceOfferReceivedOn=voiceLastSignalConnection=c.attemptId;voiceCallId=callId;voicePeerId=c.peerId;voiceOffer=sdp;voiceOutgoing=false;voiceDeadline=Clock::now()+VoiceRingTimeout;voiceEvent(VoiceState::Ringing);return;}
            if(voiceCallId==callId&&voicePeerId==c.peerId)return;
            if(voiceState==VoiceState::Outgoing&&voicePeerId==c.peerId&&profile.peerId>c.peerId){auto abandoned=voiceCallId;protocol::Writer end(protocol::CallEnd);end.string(abandoned);packet(c,end);if(voiceMedia)voiceMedia->stop();voiceMedia.reset();rememberVoiceCall(abandoned);resetVoiceSignaling(&c);voiceRequestReceivedOn=voiceOfferReceivedOn=voiceLastSignalConnection=c.attemptId;voiceCallId=callId;voiceOffer=sdp;voiceOutgoing=false;voiceDeadline=Clock::now()+VoiceRingTimeout;voiceEvent(VoiceState::Ringing,L"simultaneous call resolved");return;}
            protocol::Writer busy(protocol::CallBusy);busy.string(callId);packet(c,busy);return;
        }
        if(r.type()==protocol::CallAnswer){auto callId=r.string();auto sdp=r.string();r.end();protocol::check(protocol::id(callId,32)&&!sdp.empty()&&sdp.size()<=32768);if(voiceState==VoiceState::Outgoing&&voiceOutgoing&&voicePeerId==c.peerId&&voiceCallId==callId&&voiceMedia){voiceAnswerReceivedOn=voiceLastSignalConnection=c.attemptId;std::wstring error;if(!voiceMedia->setAnswer(sdp,error)){clearVoice(error);return;}voiceDeadline=Clock::now()+VoiceConnectTimeout;voiceEvent(VoiceState::Connecting);}return;}
        if(r.type()==protocol::CallCandidate){auto callId=r.string();auto candidate=r.string();auto mid=r.string();r.end();protocol::check(protocol::id(callId,32)&&candidate.size()<=4096&&mid.size()<=64);if(voiceCallId==callId&&voicePeerId==c.peerId){voiceLastSignalConnection=c.attemptId;++voiceCandidatesReceived;if(voiceMedia){std::wstring error;if(!voiceMedia->addCandidate(candidate,mid,error))notice(NodeEvent::Kind::Error,error,voicePeerId);}else if(voiceCandidates.size()<64)voiceCandidates.emplace_back(std::move(candidate),std::move(mid));}return;}
        if(r.type()==protocol::CallConnected){auto callId=r.string();r.end();protocol::check(protocol::id(callId,32));return;}
        if(r.type()==protocol::CallDecline||r.type()==protocol::CallBusy||r.type()==protocol::CallEnd){auto type=r.type();auto callId=r.string();r.end();protocol::check(protocol::id(callId,32));if(voiceCallId==callId&&voicePeerId==c.peerId){clearVoice(type==protocol::CallDecline?L"Declined":type==protocol::CallBusy?L"Busy":L"Remote ended the call");}else rememberVoiceCall(callId);return;}
        if(r.type()==protocol::Text){
            StoredMessage m;m.id=r.string();if(c.logicalOrdering)m.logicalOrder=r.number();m.utcMilliseconds=r.number();m.text=r.text();r.end();protocol::check(protocol::id(m.id,32));m.sender=c.peerId;m.recipient=profile.peerId;m.delivery=Delivery::Delivered;store->observeLogicalOrder(m.logicalOrder);
            if(store->append(c.peerId,m)){NodeEvent e;e.kind=NodeEvent::Kind::Message;e.peerId=c.peerId;e.messages.push_back(m);emit(std::move(e));}
            protocol::Writer ack(protocol::Ack);ack.string(m.id);packet(c,ack);
        }else if(r.type()==protocol::Ack){
            auto id=r.string();r.end();protocol::check(protocol::id(id,32));
            // A peer can acknowledge only an ID actually sent on this connection.
            if(c.inflight.erase(id)){store->acknowledge(id,Delivery::Delivered);NodeEvent e;e.kind=NodeEvent::Kind::Delivered;e.peerId=c.peerId;StoredMessage m;m.id=id;m.delivery=Delivery::Delivered;e.messages.push_back(m);emit(std::move(e));outbox(c);}
        }else if(r.type()==protocol::GroupEvent){
            auto event=decodeGroupEvent(frame);protocol::check(protocol::id(event.groupId,32)&&protocol::id(event.eventId));
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
            auto eventId=r.string();r.end();protocol::check(protocol::id(eventId));if(c.groupInflight.erase(eventId)){store->acknowledgeGroupDelivery(eventId,c.peerId);NodeEvent delivered;delivered.kind=NodeEvent::Kind::GroupDelivered;delivered.peerId=eventId;emit(std::move(delivered));groupOutbox(c);}
        }else throw std::runtime_error("Unknown peer packet type");
    }
    void receive(Connection& c){
        unsigned char buffer[8192];int n=recv(c.socket,reinterpret_cast<char*>(buffer),sizeof(buffer),0);
        if(n==0){c.closeReason=L"remote EOF";c.dead=true;return;}if(n<0){int error=WSAGetLastError();if(error!=WSAEWOULDBLOCK){c.closeReason=L"socket receive error "+std::to_wstring(error);c.dead=true;}return;}
        c.lastProgress=Clock::now();c.tls->receive(Blob(buffer,buffer+n));collect(c);if(c.tls->closed()){c.closeReason=L"remote TLS close";c.dead=true;return;}
        if(c.tls->ready()&&!c.hello&&c.peerId.empty()) {
            c.peerId=Storage::publicKeyId(c.tls->remotePublicKey());
            protocol::Writer hello(protocol::Hello);hello.number(protocol::CapabilityMutualTls|protocol::CapabilityLogicalOrdering|protocol::CapabilitySignedGroups|protocol::CapabilityVoiceCalls);hello.number(profile.listenPort);hello.text(profile.name);packet(c,hello);
        }
        auto plain=c.tls->takePlaintext();protocol::check(c.input.size()+plain.size()<=256*1024);c.input.insert(c.input.end(),plain.begin(),plain.end());
        while(c.input.size()>=4&&!c.dead){size_t length=(size_t(c.input[0])<<24)|(size_t(c.input[1])<<16)|(size_t(c.input[2])<<8)|c.input[3];protocol::check(length>=4&&length<=protocol::MaxPacket);if(c.input.size()<length+4)break;
            Blob frame(c.input.begin()+4,c.input.begin()+4+length);c.input.erase(c.input.begin(),c.input.begin()+4+length);if(c.directTransport)project(c,c.directTransport->observedReceive(frame.size()));process(c,frame);}
    }
    void flush(Connection& c){if(c.sent>=c.output.size())return;int n=::send(c.socket,reinterpret_cast<const char*>(c.output.data()+c.sent),static_cast<int>(std::min<size_t>(16384,c.output.size()-c.sent)),0);if(n>0){c.sent+=n;c.lastProgress=Clock::now();if(c.sent==c.output.size()){c.output.clear();c.sent=0;}}else if(n==0){c.closeReason=L"socket send returned zero";c.dead=true;}else {int error=WSAGetLastError();if(error!=WSAEWOULDBLOCK){c.closeReason=L"socket send error "+std::to_wstring(error);c.dead=true;}}}
    GroupEvent localGroupEvent(GroupLedger& ledger,GroupEventKind kind){
        if(!groupAccount.accountId)throw std::runtime_error("Sign in before changing a group");
        GroupEvent event;event.groupId=ledger.state().groupId;event.kind=kind;event.logicalOrder=ledger.state().maxLogicalOrder+1;event.createdMilliseconds=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());event.actorAccountId=groupAccount.accountId;event.actorPeerId=profile.peerId;return event;
    }
    void commitLocalGroupEvent(GroupLedger& ledger,GroupEvent event,const std::set<std::string>& beforePeers,bool synchronizeInvite=false){
        signGroupEvent(event,profile);if(!ledger.append(event))throw std::runtime_error("Group change is not authorized by the current signed state");persistAccepted(ledger);auto peers=beforePeers;auto after=groupPeers(ledger.state());peers.insert(after.begin(),after.end());queueGroupEvent(event,peers);
        if(synchronizeInvite&&!event.subjectPeerId.empty())queueGroupSync(ledger,event.subjectPeerId);
        if(event.kind==GroupEventKind::Message){NodeEvent message;message.kind=NodeEvent::Kind::GroupMessage;message.peerId=event.groupId;message.groupEvents.push_back(event);emit(std::move(message));}
        publishGroups();for(auto& connection:connections)if(connection->online&&!connection->dead)groupOutbox(*connection);
    }
    void drain(){
        std::deque<Command> batch;{std::lock_guard<std::mutex> lock(commandMutex);batch.swap(commands);}
        for(auto& command:batch)try{
            if(command.action==Action::Connect){
                if(command.forceImmediate&&!command.peer.empty()){
                    // Retire only this peer's stale attempts. A healthy session remains usable.
                    for(auto it=connections.begin();it!=connections.end();){
                        auto& c=**it;
                        bool manual=command.route.rfind(L"manual retry",0)==0,sameEndpoint=!c.incoming&&c.host==command.text&&c.port==command.port;
                        if(!manual&&sameEndpoint&&(c.expected==command.peer||c.peerId==command.peer)&&!c.online){c.source=command.source;c.route=command.route;}
                        bool replace=manual||(!c.incoming&&!sameEndpoint);
                        if(replace&&(c.expected==command.peer||c.peerId==command.peer)&&(!c.online||c.dead)){
                            emit(connectionEvent(NodeEvent::Kind::Disconnected,L"Retiring stale attempt for explicit retry.",c,command.peer));
                            it=connections.erase(it);
                        }else ++it;
                    }
                    for(auto it=resolvers.begin();it!=resolvers.end();){auto& resolver=**it;bool manual=command.route.rfind(L"manual retry",0)==0,sameEndpoint=resolver.host==command.text&&resolver.port==command.port;if(resolver.expected==command.peer&&!manual&&sameEndpoint){resolver.source=command.source;resolver.route=command.route;}if(resolver.expected==command.peer&&(manual||!sameEndpoint))it=resolvers.erase(it);else ++it;}
                }
                if(!command.peer.empty()){auto& retry=reconnect[command.peer];retry.host=command.text;retry.port=command.port;retry.source=command.source;retry.route=command.route;retry.suspended=false;retry.due=Clock::time_point::max();}
                resolve(command.text,command.port,command.peer,command.source,command.route);
            }
            else if(command.action==Action::InvalidateRoute){
                if(command.peer.empty())continue;
                auto routeMatches=[&](const std::wstring& host,std::uint16_t port){return command.accepted||(host==command.text&&port==command.port);};
                resolvers.erase(std::remove_if(resolvers.begin(),resolvers.end(),[&](const std::unique_ptr<Resolver>& resolver){return resolver->expected==command.peer&&routeMatches(resolver->host,resolver->port);}),resolvers.end());
                bool removed=false;for(auto it=connections.begin();it!=connections.end();){auto& c=**it;if((c.expected==command.peer||c.peerId==command.peer)&&routeMatches(c.host,c.port)){if(!command.accepted&&c.online&&!c.dead){emit(connectionEvent(NodeEvent::Kind::ConnectionLifecycle,L"authoritative endpoint changed; live verified session retained",c,command.peer));++it;continue;}c.source=command.accepted?L"Runtime network change":L"Authoritative candidate refresh";c.route=command.accepted?L"all old-network routes invalidated":L"exact stale endpoint invalidated";emit(connectionEvent(NodeEvent::Kind::Disconnected,command.accepted?L"local network generation superseded":L"local stale-route invalidation",c,command.peer));it=connections.erase(it);removed=true;}else ++it;}
                auto retry=reconnect.find(command.peer);if(retry!=reconnect.end()&&routeMatches(retry->second.host,retry->second.port))reconnect.erase(retry);auto contact=contacts.find(command.peer);if(contact!=contacts.end()&&!command.text.empty()&&command.port){auto& endpoints=contact->second.endpoints;endpoints.erase(std::remove_if(endpoints.begin(),endpoints.end(),[&](const Endpoint& endpoint){return endpoint.host==command.text&&endpoint.port==command.port;}),endpoints.end());store->saveContact(contact->second);}if(removed&&voiceState!=VoiceState::Idle&&voicePeerId==command.peer)clearVoice(L"Network route changed");
            }
            else if(command.action==Action::NetworkContext){networkGeneration=command.accountId;networkFingerprint=command.text;}
            else if(command.action==Action::SaveContact){
                auto& c=command.contact;protocol::check(protocol::id(c.peerId)&&!c.name.empty()&&c.name.size()<=64&&!c.endpoints.empty());
                for(const auto& e:c.endpoints)protocol::check(!e.host.empty()&&e.host.size()<=253&&e.port);
                store->saveContact(c);contacts[c.peerId]=c;publishContacts();
                const auto& e=c.endpoints.front();resolve(e.host,e.port,c.peerId,L"Saved contact",L"automatic reconnect");
            }
            else if(command.action==Action::RemoveContact){
                store->removeContact(command.peer);contacts.erase(command.peer);reconnect.erase(command.peer);publishContacts();
            }
            else if(command.action==Action::Approve){for(auto& c:connections)if(!c->dead&&c->hello&&!c->approved&&c->peerId==command.peer){if(!command.accepted){c->closeReason=L"local trust rejection";c->dead=true;continue;}Contact contact{c->peerId,c->name,c->tls->remotePublicKey(),{{c->host,c->port}},true};store->saveContact(contact);contacts[c->peerId]=contact;publishContacts();approve(*c);}}
            else if(command.action==Action::Send){
                if(!contacts.count(command.peer)||!contacts[command.peer].verified)throw std::runtime_error("Verify this peer before sending");
                StoredMessage m{command.id,profile.peerId,command.peer,command.text,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),Delivery::Queued,0,store->nextLogicalOrder()};
                store->append(command.peer,m);NodeEvent e;e.kind=NodeEvent::Kind::Message;e.peerId=command.peer;e.messages.push_back(m);emit(std::move(e));
                for(auto& c:connections)if(c->online&&!c->dead&&c->peerId==command.peer)outbox(*c);
            }else if(command.action==Action::History){NodeEvent e;e.kind=NodeEvent::Kind::History;e.peerId=command.peer;e.messages=store->history(command.peer,100,command.before);emit(std::move(e));}
            else if(command.action==Action::VoiceStart){
                if(voiceState!=VoiceState::Idle)throw std::runtime_error("A voice call is already active");
                auto contact=contacts.find(command.peer);
                if(contact==contacts.end()||!contact->second.verified)throw std::runtime_error("Verify this peer before calling");
                auto connection=voiceConnection(command.peer);
                if(!connection)throw std::runtime_error("The verified peer is not connected or does not support voice calls");
                resetVoiceSignaling(connection);voiceCallId=command.id;voicePeerId=command.peer;voiceOutgoing=true;voiceDeadline=Clock::now()+VoiceRingTimeout;voiceMedia=std::make_unique<VoiceMedia>(voiceInputDevice,voiceOutputDevice,voiceNetworkGeneration,std::string(voiceNetworkFingerprint.begin(),voiceNetworkFingerprint.end()));std::wstring error;
                if(!voiceMedia->startOffer(error)){clearVoice(error);continue;}voiceEvent(VoiceState::Outgoing);
            }else if(command.action==Action::VoiceAnswer){
                if(voiceState!=VoiceState::Ringing||voiceOutgoing||voiceCallId!=command.id)throw std::runtime_error("That incoming call is no longer available");
                auto connection=voiceConnection(voicePeerId);if(!connection){clearVoice(L"Peer disconnected");continue;}
                voiceDeadline=Clock::now()+VoiceConnectTimeout;voiceEvent(VoiceState::Connecting);voiceMedia=std::make_unique<VoiceMedia>(voiceInputDevice,voiceOutputDevice,voiceNetworkGeneration,std::string(voiceNetworkFingerprint.begin(),voiceNetworkFingerprint.end()));std::wstring error;
                if(!voiceMedia->startAnswer(voiceOffer,error)){clearVoice(error);continue;}for(const auto& candidate:voiceCandidates)voiceMedia->addCandidate(candidate.first,candidate.second,error);voiceCandidates.clear();
            }else if(command.action==Action::VoiceDecline){
                if(voiceState!=VoiceState::Ringing||voiceOutgoing||voiceCallId!=command.id)continue;
                if(auto connection=voiceConnection(voicePeerId)){protocol::Writer decline(protocol::CallDecline);decline.string(voiceCallId);packet(*connection,decline);}
                clearVoice(L"Declined");
            }else if(command.action==Action::VoiceEnd){
                if(voiceState==VoiceState::Idle||voiceCallId!=command.id)continue;
                if(auto connection=voiceConnection(voicePeerId)){protocol::Writer end(protocol::CallEnd);end.string(voiceCallId);packet(*connection,end);}
                clearVoice(L"Call ended");
            }else if(command.action==Action::VoiceMute){std::wstring error;if(!voiceMedia||!voiceMedia->setMuted(command.accepted,error))notice(NodeEvent::Kind::Error,error.empty()?L"No connected voice call.":L"Voice audio device error: "+error,voicePeerId);
            }else if(command.action==Action::VoiceDeafen){if(voiceMedia)voiceMedia->setDeafened(command.accepted);
            }else if(command.action==Action::VoiceDevices){if(voiceState==VoiceState::Idle){voiceInputDevice=static_cast<int>(command.port)-1;voiceOutputDevice=static_cast<int>(command.before);}
            }else if(command.action==Action::VoiceDiagnostics){if(command.response)command.response->set_value(voiceMedia?voiceSignalingDiagnostics()+L"\n"+voiceMedia->diagnostics():lastVoiceDiagnostics);
            }
            else if(command.action==Action::Name){profile.name=command.text;store->saveProfile(profile);}
            else if(command.action==Action::GroupAccount){if(command.accountId==0||command.name.empty()||command.name.size()>64)throw std::runtime_error("Invalid group account binding");if(groupAccount.accountId&&groupAccount.accountId!=command.accountId)throw std::runtime_error("This profile is already bound to another group account");groupAccount={command.accountId,command.name};store->saveGroupAccount(groupAccount);publishGroups();}
            else if(command.action==Action::GroupCreate){
                if(!groupAccount.accountId||command.group.empty()||command.text.empty()||command.text.size()>80)throw std::runtime_error("Invalid group creation");
                GroupEvent event;event.groupId=command.group;event.kind=GroupEventKind::Create;event.logicalOrder=1;event.createdMilliseconds=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());event.actorAccountId=event.subjectAccountId=groupAccount.accountId;event.actorPeerId=event.subjectPeerId=profile.peerId;event.subjectName=groupAccount.name;event.subjectPublicKey=profile.publicKey;event.value=command.text;auto& ledger=groups[event.groupId];commitLocalGroupEvent(ledger,std::move(event),{});
            }else if(command.action==Action::Groups){publishGroups();
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
                else if(command.action==Action::GroupSend){event=localGroupEvent(ledger,GroupEventKind::Message);event.message=command.text;}
                else throw std::runtime_error("Unsupported group action");
                commitLocalGroupEvent(ledger,std::move(event),peers,command.action==Action::GroupInvite);
            }
        }catch(const std::exception& error){std::string reason=error.what();notice(NodeEvent::Kind::Error,std::wstring(reason.begin(),reason.end()),command.peer);}
    }
    void run(const std::wstring& directory,std::uint16_t listeningPortOverride,const std::wstring& displayNameOverride){
        WSADATA data{};bool winsock=false;
        try {
            if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Windows sockets unavailable");
            winsock=true;
            store=std::make_unique<Storage>(directory);profile=store->profile();
            sessionEpoch={nextSessionEpoch++};
            auto profileScope=utf8(std::filesystem::path(directory).filename().wstring());
            sessionTransports=std::make_unique<session::SessionTransportManager>(std::move(profileScope),profile.peerId,sessionEpoch);
            if(listeningPortOverride)profile.listenPort=listeningPortOverride;
            if(!displayNameOverride.empty())profile.name=displayNameOverride;
            if(listeningPortOverride||!displayNameOverride.empty())store->saveProfile(profile);
            credentials=std::make_unique<TlsCredentials>(profile);
            contacts.clear();for(auto& c:store->contacts())contacts[c.peerId]=c;
            groupAccount=store->groupAccount();groups.clear();for(const auto& groupId:store->groupIds()){GroupLedger ledger;for(const auto& event:store->groupEvents(groupId))ledger.append(event);if(ledger.state().created)groups.emplace(groupId,std::move(ledger));}
            auto createListener=[&]{listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==INVALID_SOCKET)throw std::runtime_error("Cannot create listener");nonblocking(listener);BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof(exclusive));};
            createListener();sockaddr_in bindAddress{};bindAddress.sin_family=AF_INET;bindAddress.sin_port=htons(profile.listenPort);bindAddress.sin_addr.s_addr=htonl(INADDR_ANY);
            if(bind(listener,reinterpret_cast<sockaddr*>(&bindAddress),sizeof(bindAddress))){int error=WSAGetLastError();if(listeningPortOverride||(error!=WSAEADDRINUSE&&error!=WSAEACCES))throw std::runtime_error("Cannot listen on configured port; choose a free port in your profile");closesocket(listener);listener=INVALID_SOCKET;createListener();bindAddress.sin_port=0;if(bind(listener,reinterpret_cast<sockaddr*>(&bindAddress),sizeof(bindAddress)))throw std::runtime_error("Cannot allocate a free listener port for this profile");sockaddr_in assigned{};int assignedSize=sizeof(assigned);if(getsockname(listener,reinterpret_cast<sockaddr*>(&assigned),&assignedSize))throw std::runtime_error("Cannot read the allocated listener port");profile.listenPort=ntohs(assigned.sin_port);store->saveProfile(profile);}
            if(listen(listener,16))throw std::runtime_error("Cannot start the profile listener");
            NodeEvent ready;ready.kind=NodeEvent::Kind::Ready;ready.peerId=profile.peerId;ready.text=profile.name;ready.port=profile.listenPort;emit(std::move(ready));publishContacts();publishGroups();
            // Pending friend IDs are intentionally reconnectable too. The first
            // successful TLS handshake supplies the public key that turns the
            // saved fingerprint reference into a verified contact.
            for(const auto& c:contacts)if(!c.second.endpoints.empty()){const auto& endpoint=c.second.endpoints.front();RetryState retry;retry.due=Clock::now();retry.host=endpoint.host;retry.port=endpoint.port;retry.source=L"Saved contact";retry.route=L"automatic reconnect";reconnect[c.first]=std::move(retry);}
            while(running){
                drain();resolved();mediaEvents();
                if(voiceState!=VoiceState::Idle&&voiceDeadline!=Clock::time_point{}&&Clock::now()>=voiceDeadline){if(auto connection=voiceConnection(voicePeerId)){protocol::Writer end(protocol::CallEnd);end.string(voiceCallId);packet(*connection,end);}clearVoice(L"Call timed out");}
                for(auto& item:reconnect)if(!item.second.suspended&&item.second.due<=Clock::now()&&!exists(item.first)){auto& retry=item.second;retry.due=Clock::time_point::max();try{resolve(retry.host,retry.port,item.first,retry.source,retry.route.empty()?L"automatic reconnect":retry.route);}catch(...){scheduleReconnect(item.first,retry.host,retry.port,retry.source,retry.route);}}
                fd_set reads,writes,errors;FD_ZERO(&reads);FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(listener,&reads);
                for(auto& c:connections)if(!c->dead){if(!c->connecting)FD_SET(c->socket,&reads);if(c->connecting||c->sent<c->output.size())FD_SET(c->socket,&writes);FD_SET(c->socket,&errors);}
                timeval timeout{0,20000};if(select(0,&reads,&writes,&errors,&timeout)==SOCKET_ERROR)throw std::runtime_error("Network polling failed");
                if(FD_ISSET(listener,&reads))for(int i=0;i<8;i++){SOCKET socket=accept(listener,nullptr,nullptr);if(socket==INVALID_SOCKET)break;if(connections.size()>=MaxConnections){closesocket(socket);continue;}auto c=std::make_unique<Connection>();c->socket=socket;c->incoming=true;c->attemptId=nextAttemptId++;c->source=L"Inbound direct connection";c->route=L"incoming authenticated route";attachDirectTransport(*c);nonblocking(socket);c->tls=std::make_unique<TlsSession>(*credentials,true);connections.push_back(std::move(c));}
                for(auto& c:connections)if(!c->dead)try{
                    if(FD_ISSET(c->socket,&errors)){int error=0,length=sizeof(error);getsockopt(c->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length);error=error?error:WSAECONNRESET;tcpFailure(*c,error);c->closeReason=L"socket error "+std::to_wstring(error);c->dead=true;}
                    if(c->connecting&&FD_ISSET(c->socket,&writes)){int error=0,length=sizeof(error);if(getsockopt(c->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)||error){error=error?error:WSAECONNRESET;tcpFailure(*c,error);c->closeReason=L"connect completion error "+std::to_wstring(error);c->dead=true;}c->connecting=false;}
                    if(!c->dead&&!c->connecting&&!c->tls){c->tls=std::make_unique<TlsSession>(*credentials,false);collect(*c);}
                    if(!c->dead&&!c->connecting&&FD_ISSET(c->socket,&reads))receive(*c);
                    if(!c->dead&&!c->connecting&&FD_ISSET(c->socket,&writes))flush(*c);
                    auto age=Clock::now()-c->began;if(!c->online&&age>std::chrono::seconds(c->hello?120:10)){emit(connectionEvent(NodeEvent::Kind::Error,L"TCP connection timeout: the peer did not respond in time.",*c,c->peerId.empty()?c->expected:c->peerId));c->closeReason=L"local authentication timeout";c->dead=true;}
                    if(c->output.size()>c->sent&&Clock::now()-c->lastProgress>std::chrono::seconds(15)){c->closeReason=L"local send timeout";c->dead=true;}
                }catch(const std::exception& error){c->dead=true;std::string detail=error.what();std::wstring safeDetail(detail.begin(),detail.end());std::wstring stage=!c->hello?L"TLS handshake failure: ":L"application protocol/identity failure: ";c->closeReason=stage+safeDetail;emit(connectionEvent(NodeEvent::Kind::Error,c->closeReason,*c,c->peerId.empty()?c->expected:c->peerId));}
                for(auto it=connections.begin();it!=connections.end();)if((*it)->dead){auto& connection=**it;std::string id=!connection.expected.empty()?connection.expected:connection.peerId;bool mismatch=connection.identityMismatch;std::wstring host=connection.host,source=connection.source,route=connection.route;std::uint16_t port=connection.port;std::wstring reason=mismatch?L"peer identity mismatch; automatic retry paused":connection.closeReason.empty()?L"connection closed without a recorded origin":connection.closeReason;if(connection.directTransport)project(connection,connection.directTransport->closed(utf8(reason),!connection.online));auto disconnected=connectionEvent(NodeEvent::Kind::Disconnected,reason,connection,id);it=connections.erase(it);if(!id.empty()&&!exists(id)){if(voiceState!=VoiceState::Idle&&voicePeerId==id)clearVoice(L"Network connection lost");emit(std::move(disconnected));scheduleReconnect(id,host,port,source,route,mismatch);}}else ++it;
            }
        }catch(const std::exception& error){std::string reason=error.what();notice(NodeEvent::Kind::Error,std::wstring(reason.begin(),reason.end()));}
        clearVoice(L"Profile closed");running=false;resolvers.clear();connections.clear();sessionTransports.reset();if(listener!=INVALID_SOCKET){closesocket(listener);listener=INVALID_SOCKET;}credentials.reset();store.reset();groups.clear();groupAccount={};SecureZeroMemory(profile.privateKey.data(),profile.privateKey.size());profile={};reconnect.clear();finishedVoiceCalls.clear();if(winsock)WSACleanup();
    }
};
Node::Node():impl_(std::make_unique<Impl>()){}Node::~Node()=default;
void Node::start(const std::wstring& directory,std::uint16_t port,const std::wstring& name){impl_->stop();{std::lock_guard<std::mutex> lock(impl_->eventMutex);impl_->events.clear();}impl_->running=true;impl_->worker=std::thread([this,directory,port,name]{impl_->run(directory,port,name);});}
void Node::stop(){impl_->stop();}
void Node::connect(const std::wstring& host,std::uint16_t port,const std::string& expected,const std::wstring& source,const std::wstring& route,bool forceImmediate){Impl::Command c{};c.action=Impl::Action::Connect;c.text=host;c.port=port;c.peer=expected;c.source=source;c.route=route;c.forceImmediate=forceImmediate;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::invalidateRoute(const std::string& expected,const std::wstring& staleHost,std::uint16_t stalePort,bool allRoutes){if(expected.empty())return;Impl::Command c{};c.action=Impl::Action::InvalidateRoute;c.peer=expected;c.text=staleHost;c.port=stalePort;c.accepted=allRoutes;impl_->command(std::move(c));}
void Node::setNetworkContext(std::uint64_t generation,const std::wstring& fingerprint){Impl::Command c{};c.action=Impl::Action::NetworkContext;c.accountId=generation;c.text=fingerprint;impl_->command(std::move(c));}
void Node::saveContact(const Contact& contact){Impl::Command c{};c.action=Impl::Action::SaveContact;c.contact=contact;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::removeContact(const std::string& peer){Impl::Command c{};c.action=Impl::Action::RemoveContact;c.peer=peer;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::approve(const std::string& peer,bool accepted){Impl::Command c{};c.action=Impl::Action::Approve;c.peer=peer;c.accepted=accepted;impl_->command(std::move(c));}
std::string Node::send(const std::string& peer,const std::wstring& text){if(!protocol::id(peer)||text.empty()||text.size()>16384)return {};Impl::Command c{};c.action=Impl::Action::Send;c.peer=peer;c.text=text;c.id=Storage::newMessageId();auto id=c.id;return impl_->command(std::move(c))?id:std::string{};}
void Node::loadHistory(const std::string& peer,std::int64_t before){Impl::Command c{};c.action=Impl::Action::History;c.peer=peer;c.before=before;impl_->command(std::move(c));}
void Node::setName(const std::wstring& name){if(name.empty()||name.size()>64)return;Impl::Command c{};c.action=Impl::Action::Name;c.text=name;impl_->command(std::move(c));}
void Node::setGroupAccount(std::uint64_t accountId,const std::wstring& accountName){if(!accountId||accountName.empty()||accountName.size()>64)return;Impl::Command c{};c.action=Impl::Action::GroupAccount;c.accountId=accountId;c.name=accountName;impl_->command(std::move(c));}
std::string Node::createGroup(const std::wstring& name){if(name.empty()||name.size()>80)return {};Impl::Command c{};c.action=Impl::Action::GroupCreate;c.group=newGroupId();c.text=name;auto id=c.group;return impl_->command(std::move(c))?id:std::string{};}
bool Node::renameGroup(const std::string& groupId,const std::wstring& name){if(!protocol::id(groupId,32)||name.empty()||name.size()>80)return false;Impl::Command c{};c.action=Impl::Action::GroupRename;c.group=groupId;c.text=name;return impl_->command(std::move(c));}
bool Node::inviteToGroup(const std::string& groupId,std::uint64_t accountId,const std::string& peerId,const std::wstring& name){if(!protocol::id(groupId,32)||!accountId||!protocol::id(peerId)||name.empty()||name.size()>64)return false;Impl::Command c{};c.action=Impl::Action::GroupInvite;c.group=groupId;c.accountId=accountId;c.peer=peerId;c.name=name;return impl_->command(std::move(c));}
bool Node::respondToGroupInvite(const std::string& groupId,bool accept){if(!protocol::id(groupId,32))return false;Impl::Command c{};c.action=Impl::Action::GroupRespond;c.group=groupId;c.accepted=accept;return impl_->command(std::move(c));}
bool Node::leaveGroup(const std::string& groupId){if(!protocol::id(groupId,32))return false;Impl::Command c{};c.action=Impl::Action::GroupLeave;c.group=groupId;return impl_->command(std::move(c));}
bool Node::removeGroupMember(const std::string& groupId,std::uint64_t accountId){if(!protocol::id(groupId,32)||!accountId)return false;Impl::Command c{};c.action=Impl::Action::GroupRemove;c.group=groupId;c.accountId=accountId;return impl_->command(std::move(c));}
bool Node::setGroupAdmin(const std::string& groupId,std::uint64_t accountId,bool admin){if(!protocol::id(groupId,32)||!accountId)return false;Impl::Command c{};c.action=Impl::Action::GroupRole;c.group=groupId;c.accountId=accountId;c.accepted=admin;return impl_->command(std::move(c));}
bool Node::sendGroup(const std::string& groupId,const std::wstring& text){if(!protocol::id(groupId,32)||text.empty()||text.size()>16384)return false;Impl::Command c{};c.action=Impl::Action::GroupSend;c.group=groupId;c.text=text;return impl_->command(std::move(c));}
void Node::loadGroups(){Impl::Command c{};c.action=Impl::Action::Groups;impl_->command(std::move(c));}
void Node::loadGroupHistory(const std::string& group,std::int64_t before){if(!protocol::id(group,32))return;Impl::Command c{};c.action=Impl::Action::GroupHistoryPage;c.group=group;c.before=before;impl_->command(std::move(c));}
std::string Node::startVoiceCall(const std::string& peerId){if(!protocol::id(peerId))return {};Impl::Command command{};command.action=Impl::Action::VoiceStart;command.peer=peerId;command.id=Storage::newMessageId();auto id=command.id;return impl_->command(std::move(command))?id:std::string{};}
bool Node::answerVoiceCall(const std::string& callId){if(!protocol::id(callId,32))return false;Impl::Command command{};command.action=Impl::Action::VoiceAnswer;command.id=callId;return impl_->command(std::move(command));}
bool Node::declineVoiceCall(const std::string& callId){if(!protocol::id(callId,32))return false;Impl::Command command{};command.action=Impl::Action::VoiceDecline;command.id=callId;return impl_->command(std::move(command));}
bool Node::endVoiceCall(const std::string& callId){if(!protocol::id(callId,32))return false;Impl::Command command{};command.action=Impl::Action::VoiceEnd;command.id=callId;return impl_->command(std::move(command));}
bool Node::setVoiceMuted(bool muted){Impl::Command command{};command.action=Impl::Action::VoiceMute;command.accepted=muted;return impl_->command(std::move(command));}
bool Node::setVoiceDeafened(bool deafened){Impl::Command command{};command.action=Impl::Action::VoiceDeafen;command.accepted=deafened;return impl_->command(std::move(command));}
bool Node::setVoiceDevices(int inputDevice,int outputDevice){if(inputDevice < -1||outputDevice < -1)return false;Impl::Command command{};command.action=Impl::Action::VoiceDevices;command.port=static_cast<std::uint16_t>(inputDevice+1);command.before=outputDevice;return impl_->command(std::move(command));}
std::wstring Node::voiceDiagnostics(){auto response=std::make_shared<std::promise<std::wstring>>();auto result=response->get_future();Impl::Command command{};command.action=Impl::Action::VoiceDiagnostics;command.response=response;if(!impl_->command(std::move(command))||result.wait_for(std::chrono::seconds(2))!=std::future_status::ready)return L"media_state=unavailable\nlatest_audio_error=diagnostic request timed out";return result.get();}
MediaTelemetry Node::voiceTelemetry() const {MediaTelemetry result;result.inputLevel=impl_->voiceInputLevel.load();result.outputLevel=impl_->voiceOutputLevel.load();auto flags=impl_->voiceTelemetryFlags.load();result.active=(flags&1u)!=0;result.inputAvailable=(flags&2u)!=0;result.outputAvailable=(flags&4u)!=0;return result;}
std::vector<NodeEvent> Node::poll(){std::lock_guard<std::mutex> lock(impl_->eventMutex);std::vector<NodeEvent> events;while(!impl_->events.empty()){events.push_back(std::move(impl_->events.front()));impl_->events.pop_front();}return events;}
}
