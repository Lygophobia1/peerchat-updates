#include "session_transport_manager.hpp"
#include "direct_transport_adapter.hpp"
#include <iostream>
#include <stdexcept>
using namespace peerchat::session;
static void check(bool ok){if(!ok)throw std::runtime_error("Archive session scope regression");}
int main(){try{
    const std::string source(64,'a'),reader(64,'b'),issuer(64,'c'),other(64,'d');
    const SessionEpoch epoch{9};
    SessionTransportManager manager("archive-scope",source,epoch);
    RouteObservation relay{{1},"relay:"+reader,"server","opaque peer TLS",false,0,TransportKind::Relay,RouteClass::ServerRelay};
    ReliableAuthorization scope;
    scope.scope=ReliableAuthorizationScope::ArchiveControl;scope.expectedRemoteDeviceId=reader;
    scope.archive={8,2,3,issuer,source,reader,std::string(32,'e'),std::string(32,'f')};
    check(scope.valid()&&scope.permitsArchiveControl()&&!scope.permitsDirectMessage()&&!scope.permitsGroupKeyControl());
    check(manager.registerArchiveControlAttempt(epoch,{1},scope,relay,0));
    check(!manager.registerAttempt(epoch,{1},reader,relay,0));
    check(!manager.registerGroupControlAttempt(epoch,{2},scope,relay,0));
    auto wrong=scope;wrong.archive.source=other;check(!manager.registerArchiveControlAttempt(epoch,{2},wrong,relay,0));
    wrong=scope;wrong.archive.readerGeneration=0;check(!manager.registerArchiveControlAttempt(epoch,{2},wrong,relay,0));
    wrong=scope;wrong.groupId=std::string(32,'e');check(!wrong.valid());
    auto direct=relay;direct.kind=TransportKind::Direct;direct.routeClass=RouteClass::DirectLan;
    check(!manager.registerArchiveControlAttempt(epoch,{2},scope,direct,0));
    DirectTransportAdapter adapter(epoch,{1},relay,reader);
    check(!manager.observeTransport(adapter.identityPresented(other,0),1));
    check(manager.observeTransport(adapter.identityPresented(reader,0),1));
    check(manager.markLocallyTrusted(epoch,{1})&&manager.markRemoteAccepted(epoch,{1}));
    check(manager.arbitrateWinner(epoch,{1},2).activated());
    check(!manager.snapshot(reader)->connected&&manager.snapshot(scope.continuityKey())->connected);
    check(manager.invalidateArchiveControl(epoch,issuer,reader,3).empty());
    check(manager.invalidateArchiveControl({10},issuer,reader,4).empty());
    check(manager.invalidateArchiveControl(epoch,other,reader,0).empty());
    auto retired=manager.invalidateArchiveControl(epoch,issuer,reader,4);
    check(retired.size()==1&&retired.front().value==1&&!manager.authorization(epoch,{1}));
    check(!manager.observeTransport(adapter.identityPresented(reader,0),3));
    scope.archive.readerGeneration=4;scope.archive.readerGrantId=std::string(32,'1');
    check(manager.registerArchiveControlAttempt(epoch,{3},scope,relay,4));
    DirectTransportAdapter replacement(epoch,{3},relay,reader);
    check(manager.observeTransport(replacement.identityPresented(reader,0),5));
    check(manager.markLocallyTrusted(epoch,{3})&&manager.markRemoteAccepted(epoch,{3}));
    check(manager.arbitrateWinner(epoch,{3},6).activated());
    check(manager.invalidateArchiveControl(epoch,issuer,issuer,0).size()==1);
    check(!manager.snapshot(scope.continuityKey())->connected);
    SessionTransportManager receiver("archive-reader",reader,epoch);
    scope.expectedRemoteDeviceId=source;
    check(receiver.registerArchiveControlAttempt(epoch,{4},scope,relay,0));
    std::cout<<"PASS: archive purpose, device binding, isolated winner, generation invalidation and reconnect scope\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
