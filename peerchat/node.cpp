#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define FD_SETSIZE 256
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "node.hpp"
#include "tls.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
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
struct Connection {
    SOCKET socket=INVALID_SOCKET;
    bool incoming=false,connecting=false,dead=false,hello=false,approved=false,remoteApproved=false,online=false,logicalOrdering=false;
    std::wstring host,name;std::uint16_t port=0;
    std::string expected,peerId;
    std::unique_ptr<TlsSession> tls;
    Blob output,input;size_t sent=0;
    Clock::time_point began=Clock::now(),lastProgress=Clock::now();
    std::set<std::string> inflight;
    ~Connection(){if(socket!=INVALID_SOCKET)closesocket(socket);}
};
struct Resolver {
    std::wstring host,service;std::string expected;std::uint16_t port;
    OVERLAPPED overlap{};HANDLE cancel{};PADDRINFOEXW result{};bool pending=false;
    Resolver(const std::wstring& host,std::uint16_t port,const std::string& expected):host(host),service(std::to_wstring(port)),expected(expected),port(port){overlap.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!overlap.hEvent)throw std::runtime_error("Cannot start name lookup");}
    ~Resolver(){if(pending){GetAddrInfoExCancel(&cancel);WaitForSingleObject(overlap.hEvent,INFINITE);}if(result)FreeAddrInfoExW(result);CloseHandle(overlap.hEvent);}
    int begin(){ADDRINFOEXW hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;timeval timeout{5,0};int r=GetAddrInfoExW(host.c_str(),service.c_str(),NS_ALL,nullptr,&hints,&result,&timeout,&overlap,nullptr,&cancel);pending=r==WSA_IO_PENDING;return r;}
};
void nonblocking(SOCKET s){u_long yes=1;if(ioctlsocket(s,FIONBIO,&yes))throw std::runtime_error("Cannot configure socket");BOOL one=TRUE;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&one),sizeof(one));}
std::wstring observedAddress(SOCKET socket){sockaddr_in addr{};int length=sizeof(addr);if(getpeername(socket,reinterpret_cast<sockaddr*>(&addr),&length))return {};wchar_t text[64]{};InetNtopW(AF_INET,&addr.sin_addr,text,64);return text;}
}
using namespace node_detail;
struct Node::Impl {
    enum class Action { Connect,SaveContact,RemoveContact,Approve,Send,History,Name };
    struct Command{Action action;std::string peer,id;std::wstring text;std::uint16_t port=0;bool accepted=false;std::int64_t before=INT64_MAX;Contact contact;};
    std::atomic<bool> running{false};std::thread worker;std::mutex commandMutex,eventMutex;
    std::deque<Command> commands;std::deque<NodeEvent> events;
    std::unique_ptr<Storage> store;std::unique_ptr<TlsCredentials> credentials;Profile profile;
    std::map<std::string,Contact> contacts;
    std::vector<std::unique_ptr<Connection>> connections;
    std::vector<std::unique_ptr<Resolver>> resolvers;
    std::map<std::string,Clock::time_point> reconnect;
    SOCKET listener=INVALID_SOCKET;
    ~Impl(){stop();}
    void emit(NodeEvent e){std::lock_guard<std::mutex> lock(eventMutex);if(events.size()>=4096)events.pop_front();events.push_back(std::move(e));}
    void notice(NodeEvent::Kind kind,const std::wstring& text,const std::string& peer={}){NodeEvent e;e.kind=kind;e.text=text;e.peerId=peer;emit(std::move(e));}
    void tcpFailure(Connection& c,int error){
        if(error==WSAECONNREFUSED)notice(NodeEvent::Kind::Error,L"TCP connection refused: the peer is not listening on this port or Firewall blocked it.",c.expected);
        else if(error==WSAETIMEDOUT)notice(NodeEvent::Kind::Error,L"TCP connection timeout: the peer or Firewall did not respond.",c.expected);
        else if(error==WSAEHOSTUNREACH||error==WSAENETUNREACH)notice(NodeEvent::Kind::Error,L"Peer unreachable/offline: Windows has no network route to the peer endpoint.",c.expected);
        else notice(NodeEvent::Kind::Error,L"TCP connection failed: check the peer address, listener, and Firewall.",c.expected);
    }
    bool command(Command c){std::lock_guard<std::mutex> lock(commandMutex);if(!running||commands.size()>=512)return false;commands.push_back(std::move(c));return true;}
    void publishContacts(){NodeEvent e;e.kind=NodeEvent::Kind::Contacts;for(const auto& item:contacts)e.contacts.push_back(item.second);emit(std::move(e));}
    void stop(){running=false;if(worker.joinable())worker.join();std::lock_guard<std::mutex> lock(commandMutex);commands.clear();}
    bool exists(const std::string& id)const {for(const auto& c:connections)if(!c->dead&&((!id.empty()&&c->peerId==id)||(!id.empty()&&c->expected==id)))return true;for(const auto& r:resolvers)if(!id.empty()&&r->expected==id)return true;return false;}
    void resolve(const std::wstring& host,std::uint16_t port,const std::string& expected){
        if(host.empty()||host.size()>253||!port||(!expected.empty()&&!protocol::id(expected)))throw std::runtime_error("Invalid peer address");
        if(exists(expected))return;
        if(connections.size()+resolvers.size()>=MaxConnections||resolvers.size()>=8)throw std::runtime_error("Too many pending connections");
        // A copied LAN IPv4 address should never depend on DNS or the async
        // resolver. This also avoids resolver differences between Windows PCs.
        IN_ADDR numeric{};
        if(InetPtonW(AF_INET,host.c_str(),&numeric)==1){
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr=numeric;
            auto connection=std::make_unique<Connection>();connection->host=host;connection->port=port;connection->expected=expected;connection->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(connection->socket==INVALID_SOCKET){notice(NodeEvent::Kind::Error,L"Could not create a connection socket.",expected);return;}
            nonblocking(connection->socket);int result=::connect(connection->socket,reinterpret_cast<sockaddr*>(&address),sizeof(address));int error=result==0?0:WSAGetLastError();
            if(result==0||error==WSAEWOULDBLOCK||error==WSAEINPROGRESS){connection->connecting=result!=0;connections.push_back(std::move(connection));}
            else {tcpFailure(*connection,error);closesocket(connection->socket);}
            return;
        }
        auto r=std::make_unique<Resolver>(host,port,expected);int status=r->begin();
        if(status!=0&&status!=WSA_IO_PENDING){notice(NodeEvent::Kind::Error,L"DNS/hostname failure: could not resolve "+host+L":"+std::to_wstring(port)+L".",expected);return;}
        resolvers.push_back(std::move(r));
    }
    void resolved(){
        for(auto it=resolvers.begin();it!=resolvers.end();){
            auto& r=**it;if(r.pending&&WaitForSingleObject(r.overlap.hEvent,0)!=WAIT_OBJECT_0){++it;continue;}
            int result=r.pending?GetAddrInfoExOverlappedResult(&r.overlap):0;r.pending=false;
            if(result==0&&r.result){
                auto c=std::make_unique<Connection>();c->host=r.host;c->port=r.port;c->expected=r.expected;c->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
                if(c->socket!=INVALID_SOCKET){nonblocking(c->socket);int n=::connect(c->socket,r.result->ai_addr,static_cast<int>(r.result->ai_addrlen));
                    if(n==0||WSAGetLastError()==WSAEWOULDBLOCK||WSAGetLastError()==WSAEINPROGRESS){c->connecting=n!=0;connections.push_back(std::move(c));}
                    else {int error=WSAGetLastError();tcpFailure(*c,error);closesocket(c->socket);}
                }
            }else notice(NodeEvent::Kind::Error,L"DNS/hostname failure: lookup failed or timed out for "+r.host+L":"+std::to_wstring(r.port)+L".",r.expected);
            if(!r.expected.empty())reconnect[r.expected]=Clock::now()+std::chrono::seconds(5);
            it=resolvers.erase(it);
        }
    }
    void collect(Connection& c){auto bytes=c.tls->takeOutput();if(c.output.size()-c.sent+bytes.size()>MaxQueue)throw std::runtime_error("Peer is not consuming outgoing data");if(c.sent){c.output.erase(c.output.begin(),c.output.begin()+c.sent);c.sent=0;}c.output.insert(c.output.end(),bytes.begin(),bytes.end());}
    void packet(Connection& c,protocol::Writer& w){c.tls->send(w.frame());collect(c);}
    void outbox(Connection& c){
        if(!c.online)return;
        for(const auto& m:store->pending(c.peerId,100)) {
            if(c.inflight.count(m.id))continue;
            if(c.inflight.size()>=16||c.output.size()-c.sent>MaxQueue/2)break;
            protocol::Writer w(protocol::Text);w.string(m.id);if(c.logicalOrdering)w.number(m.logicalOrder);w.number(m.utcMilliseconds);w.text(m.text);packet(c,w);c.inflight.insert(m.id);
        }
    }
    void online(Connection& c){
        if(c.online||!c.approved||!c.remoteApproved)return;
        for(auto& other:connections)if(other.get()!=&c&&!other->dead&&other->online&&other->peerId==c.peerId){
            if(c.incoming==(profile.peerId<c.peerId)){c.dead=true;return;}other->dead=true;
        }
        c.online=true;reconnect.erase(c.peerId);notice(NodeEvent::Kind::Connected,c.name,c.peerId);outbox(c);
    }
    void approve(Connection& c){
        c.approved=true;protocol::Writer ready(protocol::Accepted);packet(c,ready);online(c);
    }
    void process(Connection& c,const Blob& frame){
        protocol::Reader r(frame);
        if(!c.hello){
            protocol::check(r.type()==protocol::Hello);auto capabilities=r.number();auto port=r.number();c.name=r.text();r.end();
            protocol::check((capabilities&protocol::CapabilityMutualTls)!=0&&port>0&&port<=65535&&c.name.size()<=64);c.logicalOrdering=(capabilities&protocol::CapabilityLogicalOrdering)!=0;c.hello=true;
            c.peerId=Storage::publicKeyId(c.tls->remotePublicKey());protocol::check(c.peerId!=profile.peerId);
            c.port=static_cast<std::uint16_t>(port);if(c.host.empty())c.host=observedAddress(c.socket);
            // A changed identity is a security-relevant verification event, not a silent
            // transport rejection. The UI must show the expected and presented public IDs and
            // require an explicit decision. No private key, password, or message content is
            // included. The presented ID becomes trusted only if approve() is later called.
            if(!c.expected.empty()&&c.expected!=c.peerId){
                NodeEvent changed;changed.kind=NodeEvent::Kind::TrustRequired;changed.peerId=c.peerId;
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
            }else{NodeEvent e;e.kind=NodeEvent::Kind::TrustRequired;e.peerId=c.peerId;e.text=c.name;e.contacts.push_back({c.peerId,c.name,c.tls->remotePublicKey(),{{c.host,c.port}},false});emit(std::move(e));}
            return;
        }
        if(r.type()==protocol::Accepted){r.end();protocol::check(!c.remoteApproved);c.remoteApproved=true;online(c);return;}
        protocol::check(c.online);
        if(r.type()==protocol::Text){
            StoredMessage m;m.id=r.string();if(c.logicalOrdering)m.logicalOrder=r.number();m.utcMilliseconds=r.number();m.text=r.text();r.end();protocol::check(protocol::id(m.id,32));m.sender=c.peerId;m.recipient=profile.peerId;m.delivery=Delivery::Delivered;store->observeLogicalOrder(m.logicalOrder);
            if(store->append(c.peerId,m)){NodeEvent e;e.kind=NodeEvent::Kind::Message;e.peerId=c.peerId;e.messages.push_back(m);emit(std::move(e));}
            protocol::Writer ack(protocol::Ack);ack.string(m.id);packet(c,ack);
        }else if(r.type()==protocol::Ack){
            auto id=r.string();r.end();protocol::check(protocol::id(id,32));
            // A peer can acknowledge only an ID actually sent on this connection.
            if(c.inflight.erase(id)){store->acknowledge(id,Delivery::Delivered);NodeEvent e;e.kind=NodeEvent::Kind::Delivered;e.peerId=c.peerId;StoredMessage m;m.id=id;m.delivery=Delivery::Delivered;e.messages.push_back(m);emit(std::move(e));outbox(c);}
        }else throw std::runtime_error("Unknown peer packet type");
    }
    void receive(Connection& c){
        unsigned char buffer[8192];int n=recv(c.socket,reinterpret_cast<char*>(buffer),sizeof(buffer),0);
        if(n==0){c.dead=true;return;}if(n<0){if(WSAGetLastError()!=WSAEWOULDBLOCK)c.dead=true;return;}
        c.lastProgress=Clock::now();c.tls->receive(Blob(buffer,buffer+n));collect(c);if(c.tls->closed()){c.dead=true;return;}
        if(c.tls->ready()&&!c.hello&&c.peerId.empty()) {
            c.peerId=Storage::publicKeyId(c.tls->remotePublicKey());
            protocol::Writer hello(protocol::Hello);hello.number(protocol::CapabilityMutualTls|protocol::CapabilityLogicalOrdering);hello.number(profile.listenPort);hello.text(profile.name);packet(c,hello);
        }
        auto plain=c.tls->takePlaintext();protocol::check(c.input.size()+plain.size()<=256*1024);c.input.insert(c.input.end(),plain.begin(),plain.end());
        while(c.input.size()>=4&&!c.dead){size_t length=(size_t(c.input[0])<<24)|(size_t(c.input[1])<<16)|(size_t(c.input[2])<<8)|c.input[3];protocol::check(length>=4&&length<=protocol::MaxPacket);if(c.input.size()<length+4)break;
            Blob frame(c.input.begin()+4,c.input.begin()+4+length);c.input.erase(c.input.begin(),c.input.begin()+4+length);process(c,frame);}
    }
    void flush(Connection& c){if(c.sent>=c.output.size())return;int n=::send(c.socket,reinterpret_cast<const char*>(c.output.data()+c.sent),static_cast<int>(std::min<size_t>(16384,c.output.size()-c.sent)),0);if(n>0){c.sent+=n;c.lastProgress=Clock::now();if(c.sent==c.output.size()){c.output.clear();c.sent=0;}}else if(n==0||WSAGetLastError()!=WSAEWOULDBLOCK)c.dead=true;}
    void drain(){
        std::deque<Command> batch;{std::lock_guard<std::mutex> lock(commandMutex);batch.swap(commands);}
        for(auto& command:batch)try{
            if(command.action==Action::Connect){resolve(command.text,command.port,command.peer);}
            else if(command.action==Action::SaveContact){
                auto& c=command.contact;protocol::check(protocol::id(c.peerId)&&!c.name.empty()&&c.name.size()<=64&&!c.endpoints.empty());
                for(const auto& e:c.endpoints)protocol::check(!e.host.empty()&&e.host.size()<=253&&e.port);
                store->saveContact(c);contacts[c.peerId]=c;publishContacts();
                const auto& e=c.endpoints.front();resolve(e.host,e.port,c.peerId);
            }
            else if(command.action==Action::RemoveContact){
                store->removeContact(command.peer);contacts.erase(command.peer);reconnect.erase(command.peer);publishContacts();
            }
            else if(command.action==Action::Approve){for(auto& c:connections)if(!c->dead&&c->hello&&!c->approved&&c->peerId==command.peer){if(!command.accepted){c->dead=true;continue;}Contact contact{c->peerId,c->name,c->tls->remotePublicKey(),{{c->host,c->port}},true};store->saveContact(contact);contacts[c->peerId]=contact;publishContacts();approve(*c);}}
            else if(command.action==Action::Send){
                if(!contacts.count(command.peer)||!contacts[command.peer].verified)throw std::runtime_error("Verify this peer before sending");
                StoredMessage m{command.id,profile.peerId,command.peer,command.text,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),Delivery::Queued,0,store->nextLogicalOrder()};
                store->append(command.peer,m);NodeEvent e;e.kind=NodeEvent::Kind::Message;e.peerId=command.peer;e.messages.push_back(m);emit(std::move(e));
                for(auto& c:connections)if(c->online&&!c->dead&&c->peerId==command.peer)outbox(*c);
            }else if(command.action==Action::History){NodeEvent e;e.kind=NodeEvent::Kind::History;e.peerId=command.peer;e.messages=store->history(command.peer,100,command.before);emit(std::move(e));}
            else if(command.action==Action::Name){profile.name=command.text;store->saveProfile(profile);}
        }catch(const std::exception& error){std::string reason=error.what();notice(NodeEvent::Kind::Error,std::wstring(reason.begin(),reason.end()),command.peer);}
    }
    void run(const std::wstring& directory,std::uint16_t listeningPortOverride,const std::wstring& displayNameOverride){
        WSADATA data{};bool winsock=false;
        try {
            if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Windows sockets unavailable");
            winsock=true;
            store=std::make_unique<Storage>(directory);profile=store->profile();
            if(listeningPortOverride)profile.listenPort=listeningPortOverride;
            if(!displayNameOverride.empty())profile.name=displayNameOverride;
            if(listeningPortOverride||!displayNameOverride.empty())store->saveProfile(profile);
            credentials=std::make_unique<TlsCredentials>(profile);
            contacts.clear();for(auto& c:store->contacts())contacts[c.peerId]=c;
            listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==INVALID_SOCKET)throw std::runtime_error("Cannot create listener");nonblocking(listener);BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof(exclusive));
            sockaddr_in bindAddress{};bindAddress.sin_family=AF_INET;bindAddress.sin_port=htons(profile.listenPort);bindAddress.sin_addr.s_addr=htonl(INADDR_ANY);
            if(bind(listener,reinterpret_cast<sockaddr*>(&bindAddress),sizeof(bindAddress))||listen(listener,16))throw std::runtime_error("Cannot listen on saved port; choose a free port in your profile");
            NodeEvent ready;ready.kind=NodeEvent::Kind::Ready;ready.peerId=profile.peerId;ready.text=profile.name;ready.port=profile.listenPort;emit(std::move(ready));publishContacts();
            // Pending friend IDs are intentionally reconnectable too. The first
            // successful TLS handshake supplies the public key that turns the
            // saved fingerprint reference into a verified contact.
            for(const auto& c:contacts)if(!c.second.endpoints.empty())reconnect[c.first]=Clock::now();
            while(running){
                drain();resolved();
                for(auto& retry:reconnect)if(retry.second<=Clock::now()&&!exists(retry.first)){auto it=contacts.find(retry.first);if(it!=contacts.end()&&!it->second.endpoints.empty()){auto e=it->second.endpoints.front();try{resolve(e.host,e.port,retry.first);}catch(...){} }retry.second=Clock::now()+std::chrono::seconds(5);}
                fd_set reads,writes,errors;FD_ZERO(&reads);FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(listener,&reads);
                for(auto& c:connections)if(!c->dead){if(!c->connecting)FD_SET(c->socket,&reads);if(c->connecting||c->sent<c->output.size())FD_SET(c->socket,&writes);FD_SET(c->socket,&errors);}
                timeval timeout{0,20000};if(select(0,&reads,&writes,&errors,&timeout)==SOCKET_ERROR)throw std::runtime_error("Network polling failed");
                if(FD_ISSET(listener,&reads))for(int i=0;i<8;i++){SOCKET socket=accept(listener,nullptr,nullptr);if(socket==INVALID_SOCKET)break;if(connections.size()>=MaxConnections){closesocket(socket);continue;}auto c=std::make_unique<Connection>();c->socket=socket;c->incoming=true;nonblocking(socket);c->tls=std::make_unique<TlsSession>(*credentials,true);connections.push_back(std::move(c));}
                for(auto& c:connections)if(!c->dead)try{
                    if(FD_ISSET(c->socket,&errors)){int error=0,length=sizeof(error);getsockopt(c->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length);tcpFailure(*c,error?error:WSAECONNRESET);c->dead=true;}
                    if(c->connecting&&FD_ISSET(c->socket,&writes)){int error=0,length=sizeof(error);if(getsockopt(c->socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)||error){tcpFailure(*c,error?error:WSAECONNRESET);c->dead=true;}c->connecting=false;}
                    if(!c->dead&&!c->connecting&&!c->tls){c->tls=std::make_unique<TlsSession>(*credentials,false);collect(*c);}
                    if(!c->dead&&!c->connecting&&FD_ISSET(c->socket,&reads))receive(*c);
                    if(!c->dead&&!c->connecting&&FD_ISSET(c->socket,&writes))flush(*c);
                    auto age=Clock::now()-c->began;if(!c->online&&age>std::chrono::seconds(c->hello?120:10)){notice(NodeEvent::Kind::Error,L"TCP connection timeout: the peer did not respond in time.",c->peerId.empty()?c->expected:c->peerId);c->dead=true;}
                    if(c->output.size()>c->sent&&Clock::now()-c->lastProgress>std::chrono::seconds(15))c->dead=true;
                }catch(const std::exception& error){c->dead=true;std::string detail=error.what();std::wstring safeDetail(detail.begin(),detail.end());std::wstring stage=!c->hello?L"TLS handshake failure: ":L"Peer identity/trust failure: ";notice(NodeEvent::Kind::Error,stage+safeDetail,c->peerId.empty()?c->expected:c->peerId);}
                for(auto it=connections.begin();it!=connections.end();)if((*it)->dead){std::string id=(*it)->peerId.empty()?(*it)->expected:(*it)->peerId;it=connections.erase(it);if(!id.empty()&&!exists(id)){notice(NodeEvent::Kind::Disconnected,L"Peer disconnected. Reconnecting if it is a saved contact.",id);if(contacts.count(id)&&!contacts[id].endpoints.empty())reconnect[id]=Clock::now()+std::chrono::seconds(5);}}else ++it;
            }
        }catch(const std::exception& error){std::string reason=error.what();notice(NodeEvent::Kind::Error,std::wstring(reason.begin(),reason.end()));}
        running=false;resolvers.clear();connections.clear();if(listener!=INVALID_SOCKET){closesocket(listener);listener=INVALID_SOCKET;}credentials.reset();store.reset();SecureZeroMemory(profile.privateKey.data(),profile.privateKey.size());profile={};reconnect.clear();if(winsock)WSACleanup();
    }
};
Node::Node():impl_(std::make_unique<Impl>()){}Node::~Node()=default;
void Node::start(const std::wstring& directory,std::uint16_t port,const std::wstring& name){impl_->stop();{std::lock_guard<std::mutex> lock(impl_->eventMutex);impl_->events.clear();}impl_->running=true;impl_->worker=std::thread([this,directory,port,name]{impl_->run(directory,port,name);});}
void Node::stop(){impl_->stop();}
void Node::connect(const std::wstring& host,std::uint16_t port,const std::string& expected){Impl::Command c{};c.action=Impl::Action::Connect;c.text=host;c.port=port;c.peer=expected;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::saveContact(const Contact& contact){Impl::Command c{};c.action=Impl::Action::SaveContact;c.contact=contact;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::removeContact(const std::string& peer){Impl::Command c{};c.action=Impl::Action::RemoveContact;c.peer=peer;if(!impl_->command(std::move(c)))impl_->notice(NodeEvent::Kind::Error,L"Peer node is stopped or busy.");}
void Node::approve(const std::string& peer,bool accepted){Impl::Command c{};c.action=Impl::Action::Approve;c.peer=peer;c.accepted=accepted;impl_->command(std::move(c));}
std::string Node::send(const std::string& peer,const std::wstring& text){if(!protocol::id(peer)||text.empty()||text.size()>16384)return {};Impl::Command c{};c.action=Impl::Action::Send;c.peer=peer;c.text=text;c.id=Storage::newMessageId();auto id=c.id;return impl_->command(std::move(c))?id:std::string{};}
void Node::loadHistory(const std::string& peer,std::int64_t before){Impl::Command c{};c.action=Impl::Action::History;c.peer=peer;c.before=before;impl_->command(std::move(c));}
void Node::setName(const std::wstring& name){if(name.empty()||name.size()>64)return;Impl::Command c{};c.action=Impl::Action::Name;c.text=name;impl_->command(std::move(c));}
std::vector<NodeEvent> Node::poll(){std::lock_guard<std::mutex> lock(impl_->eventMutex);std::vector<NodeEvent> events;while(!impl_->events.empty()){events.push_back(std::move(impl_->events.front()));impl_->events.pop_front();}return events;}
}
