#pragma once
// Host-only adapters around the production endpoint/TransferManager. Synthetic
// file content and local pipe carriage never replace authentication.
#include "../portable/reliable_endpoint.hpp"
#include "../windows_transfer_io.hpp"
#if defined(PEERCHAT_JAVA_TLS_FIXTURE)
#include "../portable/android_tls_provider.hpp"
#endif
#include <iostream>
#include <fstream>

namespace transfer_interop {
using namespace peerchat;
inline Blob content(bool android){Blob bytes(2*1024*1024+123);for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>((i*31+(android?7:11))%251);return bytes;}
struct Received {Blob bytes;bool committed=false;};
struct Sink final:transfer::Sink {
    std::shared_ptr<Received> result;explicit Sink(std::shared_ptr<Received> r):result(std::move(r)){}
    void append(const Blob& bytes)override{result->bytes.insert(result->bytes.end(),bytes.begin(),bytes.end());}
    void commit()override{result->committed=true;}
};
inline std::string hex(const Blob& bytes){const char* digits="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
inline Blob unhex(const std::string& text){protocol::check(text.size()%2==0&&text.size()<=131072);Blob out;for(size_t i=0;i<text.size();i+=2){auto pair=text.substr(i,2);protocol::check(pair.find_first_not_of("0123456789abcdef")==std::string::npos);out.push_back(static_cast<unsigned char>(std::stoul(pair,nullptr,16)));}return out;}
struct Side {
    Storage store;Profile profile;std::string remote;bool android=false,offered=false;
    std::wstring sourcePath;
    std::uint64_t now=1,frames=0,maxPlainFrame=0,maxEncryptedOutput=0;
    std::unique_ptr<TlsCredentials> credentials;
    std::unique_ptr<session::SessionTransportManager> sessions;
    std::unique_ptr<delivery::DeliveryManager> deliveries;
    reliable::AuthenticatedFriendSnapshot friends;
    transfer::TransferManager transfers{[]{return std::make_unique<transfer::windows::Sha256>();}};
    std::unique_ptr<portable::ReliableEndpoint> endpoint;
    std::shared_ptr<Received> received=std::make_shared<Received>();
    const char* stage="fixture_created";
    Side(const std::wstring& directory,const std::string& expected
#if defined(PEERCHAT_JAVA_TLS_FIXTURE)
        ,std::shared_ptr<platform::android::Context> context
#endif
        ):store(directory),remote(expected){
#if defined(PEERCHAT_JAVA_TLS_FIXTURE)
        android=true;profile.identity=std::make_shared<platform::android::Identity>(context);profile.publicKey=profile.identity->publicKey();profile.peerId=Storage::publicKeyId(profile.publicKey);
        credentials=std::make_unique<TlsCredentials>(profile,std::make_shared<platform::android::TlsFactory>(context));
#else
        profile=store.profile();credentials=std::make_unique<TlsCredentials>(profile);
#endif
        sessions=std::make_unique<session::SessionTransportManager>(profile.peerId,profile.peerId,session::SessionEpoch{1});
        deliveries=std::make_unique<delivery::DeliveryManager>(store,profile.peerId);
        friends.replace(android?10:20,1,{{android?20ull:10ull,remote,{remote}}});
        const auto route=session::RouteObservation{{1},"relay:interop-fixture","authenticated fixture","opaque bytes",!android,1,session::TransportKind::Relay,session::RouteClass::ServerRelay};
        std::unique_ptr<TlsSession> tls;
#if defined(PEERCHAT_JAVA_TLS_FIXTURE)
        tls=std::make_unique<TlsSession>(*credentials,false,remote);
#else
        tls=std::make_unique<TlsSession>(*credentials,true);
#endif
        endpoint=std::make_unique<portable::ReliableEndpoint>(profile,store,*sessions,*deliveries,friends,session::SessionEpoch{1},session::TransportInstanceId{1},std::move(tls),remote,route,now,std::nullopt,
            [&](const std::string& peer,const Blob& frame,std::uint64_t time){
                stage="transfer_receive";++frames;maxPlainFrame=std::max<std::uint64_t>(maxPlainFrame,frame.size());
                transfers.capability(peer,1,endpoint->permitsTransferPipeline());transfers.receive(peer,frame,time);
            });
        // Same accepted defaults, with the existing unchanged cap and bounds.
        transfers.defaults({transfer::ExtendedChunkBytes,16,10000000},10000000);
        sourcePath=(std::filesystem::path(directory)/L"interop-fixture.bin").wstring();
        // Use the shipping bounded source adapter: it rejects any read above
        // 32768, proving negotiated 65536 frames are assembled from bounded reads.
        auto bytes=content(android);std::ofstream source(std::filesystem::path(sourcePath),std::ios::binary);
        source.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));protocol::check(bool(source));
    }
    Blob exchange(const Blob& encrypted){
        stage="peer_tls_receive";protocol::check(encrypted.size()<=65536);endpoint->receive(encrypted,++now);
        while(auto event=endpoint->poll())if(event->kind==portable::ReliableEndpoint::EventKind::TrustRequired){stage="initial_pin";protocol::check(endpoint->initialPin(1,++now));}
        if(endpoint->ready()){
            protocol::check(endpoint->permitsTransferPipeline());
            if(!offered){stage="local_offer";transfers.offer(remote,Storage::newMessageId(),std::make_unique<transfer::windows::FileSource>(sourcePath),now);offered=true;}
            for(const auto& row:transfers.snapshots())if(row.state==transfer::State::AwaitingAcceptance){stage="local_accept";transfers.accept(row.metadata.id,std::make_unique<Sink>(received),now);}
            for(unsigned i=0;i<10;++i){now+=10;stage="transfer_tick";transfers.capability(remote,1,endpoint->permitsTransferPipeline());
                transfers.tick(now,[](const std::string&){return 1;},[&](const std::string&,const Blob& bytes){return endpoint->sendTransfer(bytes);});}
        }
        stage="opaque_output";auto output=endpoint->takeOutput();protocol::check(output.size()<=65536);maxEncryptedOutput=std::max<std::uint64_t>(maxEncryptedOutput,output.size());return output;
    }
    bool complete()const{return received->committed&&transfers.completedRecords().size()==2;}
    void verify()const{
        protocol::check(complete()&&received->bytes==content(!android)&&maxPlainFrame>65536&&maxEncryptedOutput<=65536);
        for(const auto& row:transfers.diagnostics(now))protocol::check(row.find("protocol_version=2 effective_chunk_bytes=65536 effective_window=16")!=std::string::npos);
    }
};
}
