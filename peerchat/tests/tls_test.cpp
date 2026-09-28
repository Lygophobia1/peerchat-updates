#include "tls.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>

void require(bool c,const char* text){if(!c)throw std::runtime_error(text);}
int main() {
    using namespace peerchat;
    try {
        auto runId=Storage::newMessageId();auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::wstring(runId.begin(),runId.end());
        Storage a((root/L"a").wstring()),b((root/L"b").wstring());auto alice=a.profile(),bob=b.profile();
        TlsCredentials ac(alice),bc(bob);TlsSession client(ac,false),server(bc,true);
        require(ac.certificateDiagnostics().find("peer_id="+alice.peerId)!=std::string::npos,"Local certificate did not retain Alice's persistent Peer ID");
        require(ac.certificateDiagnostics().find("not_before_utc=")!=std::string::npos&&ac.certificateDiagnostics().find("not_after_utc=")!=std::string::npos,"Local certificate validity diagnostics are incomplete");
        for(int i=0;i<30&&(!client.ready()||!server.ready());i++) {
            auto c=client.takeOutput();if(!c.empty())server.receive(c);
            auto s=server.takeOutput();if(!s.empty())client.receive(s);
        }
        require(client.ready()&&server.ready(),"Handshake did not finish");
        require(client.remotePublicKey()==bob.publicKey&&server.remotePublicKey()==alice.publicKey,"Mutual certificate identity mismatch");
        Blob hello{'h','e','l','l','o'},reply{'r','e','p','l','y'};
        client.send(hello);auto encrypted=client.takeOutput();require(encrypted!=hello,"TLS returned plaintext");
        // Network fragmentation must not confuse the record parser.
        for(auto byte:encrypted)server.receive(Blob{byte});
        require(server.takePlaintext()==hello,"Client to server text failed");
        server.send(reply);client.receive(server.takeOutput());require(client.takePlaintext()==reply,"Server to client text failed");
        client.send(hello);auto altered=client.takeOutput();altered.back()^=1;bool rejected=false;
        try{server.receive(altered);}catch(const std::exception&){rejected=true;}require(rejected,"Altered record accepted");
        // Restarting recreates only the short-lived self-signed certificate; the
        // encrypted profile's P-256 key and Peer ID must remain unchanged.
        auto reloaded=a.profile();require(reloaded.peerId==alice.peerId&&reloaded.publicKey==alice.publicKey,"Persistent identity changed after storage reload");
        TlsCredentials restarted(reloaded);require(restarted.certificateDiagnostics().find("peer_id="+alice.peerId)!=std::string::npos,"Restarted certificate did not use the persistent identity");
        std::cout<<"PASS: Schannel mutual TLS, persistent P-256 peer identities, bidirectional records, fragmentation, tamper rejection.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
