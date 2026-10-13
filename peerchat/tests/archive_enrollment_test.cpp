#include "../storage.cpp"
#include "../archive_application.hpp"
#include "../direct_transport_adapter.hpp"
#include <iostream>
using namespace peerchat;
namespace ar=peerchat::archive;
static void check(bool ok){if(!ok)throw std::runtime_error("Archive enrollment regression");}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
int main(){try{
    auto root=std::filesystem::current_path()/L"build"/L"tests"/storage_detail::wide(Storage::newMessageId());
    Storage a((root/L"source").wstring()),b((root/L"reader").wstring());auto pa=a.profile(),pb=b.profile();
    std::uint64_t now=1000;auto clock=[&]{return now;};
    ar::Enrollment source(a,pa,8,clock),reader(b,pb,8,clock);
    rejects([&]{source.registration(10);});source.authenticated(1);reader.authenticated(1);
    auto da=source.registration(10),db=reader.registration(10);
    check(ar::executeEnrollmentOperation(source,1,ar::applicationHeader(1).data,11)==da);
    auto malformed=ar::applicationHeader(4);malformed.string(pa.peerId);malformed.number(1);malformed.data.push_back(0);
    rejects([&]{ar::executeEnrollmentOperation(source,4,malformed.data,20);});
    check(!source.policy().trustsAuthority(pa.publicKey)); // Trailing bytes cannot approve before rejection.
    rejects([&]{ar::executeEnrollmentOperation(source,1,Blob(ar::MaxApplicationBytes+1,0),20);});
    rejects([&]{ar::executeEnrollmentOperation(source,9,ar::applicationHeader(1).data,20);});
    check(source.registration(11)==da&&reader.registration(11)==db);
    auto metadata=[&](const Profile& device,std::uint64_t revision,std::vector<ar::RegisteredGrant> grants,
            std::uint64_t generation=1){
        ar::EnrollmentMetadata m;m.account=8;m.authenticationGeneration=generation;m.revision=revision;
        m.device=device.peerId;m.serverEpoch="test-server-epoch";m.descriptors={da,db};m.grants=std::move(grants);return m;
    };
    source.refresh(metadata(pa,1,{}));reader.refresh(metadata(pb,1,{}));
    auto admission=reader.authorizationDiagnostics(8,1);
    check(admission.find("metadata_fresh=1")!=std::string::npos&&admission.find("required_grant_or_trust_missing")!=std::string::npos);
    rejects([&]{reader.authorizationDiagnostics(9,1);});rejects([&]{reader.authorizationDiagnostics(8,2);});
    check(reader.automaticSync(pa.peerId));reader.automaticSync(pa.peerId,false);check(!reader.automaticSync(pa.peerId));
    {ar::Enrollment reopened(b,pb,8,clock);reopened.authenticated(1);reopened.refresh(metadata(pb,1,{}));check(!reopened.automaticSync(pa.peerId));}
    check(reader.pendingPublications().empty()&&!reader.policy().trustsAuthority(pa.publicKey));
    rejects([&]{reader.automaticSync(std::string(64,'f'),true);});reader.automaticSync(pa.peerId,true);check(reader.automaticSync(pa.peerId));
    auto bound=ar::applicationHeader(10);bound.number(9);bound.number(1);bound.number(4);bound.string(pa.peerId);bound.number(1);
    rejects([&]{ar::executeEnrollmentOperation(source,10,bound.data,20);});
    bound=ar::applicationHeader(10);bound.number(8);bound.number(2);bound.number(4);bound.string(pa.peerId);bound.number(1);
    rejects([&]{ar::executeEnrollmentOperation(source,10,bound.data,20);});
    check(!source.policy().trustsAuthority(pa.publicKey)); // Stale consent cannot authorize a new context.
    check(source.devices().size()==2&&reader.devices().size()==2);
    for(const auto& device:source.devices())check(device.maySetUp==device.local&&device.approvalIssuer.empty());
    for(const auto& device:reader.devices())check(!device.mayRead&&!device.mayServe&&!device.approvedIssuer);
    delivery::DeliveryManager deliveries(b,pb.peerId);
    session::SessionTransportManager sessions("archive-enrollment",pb.peerId,{1});
    ar::Execution execution(b,pb,reader.policy(),deliveries,sessions);
    ar::reconcileInitialSync(reader,execution);check(!execution.running()); // No implicit same-account permission.
    rejects([&]{source.authorizeDevice(pb.peerId,ar::ReadHistory,20,true);});
    rejects([&]{source.approveIssuer(pa.peerId,false);});source.approveIssuer(pa.peerId,true);
    for(const auto& d:source.devices())check(d.mayAuthorize&&!d.pairAuthorized&&!d.localGrantPresent);
    auto serving=source.authorizeDevice(pa.peerId,ar::ServeHistory,20,true);
    auto reading=source.authorizeDevice(pb.peerId,ar::ReadHistory,20,true);
    check(source.pendingPublications().size()==2&&!source.policy().permits(pa.peerId,pa.peerId,ar::ServeHistory));
    std::vector<ar::RegisteredGrant> registered{{ar::encodeGrant(serving),true},{ar::encodeGrant(reading),true}};
    source.refresh(metadata(pa,2,registered));reader.refresh(metadata(pb,2,registered));
    for(const auto& device:source.devices())check(!device.maySetUp);
    for(const auto& device:reader.devices())check(!device.maySetUp&&device.approvalIssuer==(device.local?pa.peerId:std::string{}));
    check(reader.authorizationDiagnostics(8,1).find("authorizer_not_trusted")!=std::string::npos);
    check(source.pendingPublications().empty()&&!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{reader.approveIssuer(pa.peerId,false);});reader.approveIssuer(pa.peerId,true);
    admission=reader.authorizationDiagnostics(8,1);
    for(const auto& device:reader.devices())check(!device.maySetUp&&device.approvalIssuer.empty());
    for(const auto& d:reader.devices())check((d.local||d.pairAuthorized)&&!d.mayAuthorize&&!d.localGrantPresent);
    for(const auto& d:source.devices()){check((d.local||d.pairAuthorized)&&d.mayAuthorize&&d.localGrantPresent);check(d.localGrantRead==(d.peerId==pb.peerId)&&d.localGrantServe==(d.peerId==pa.peerId));}
    check(admission.find("source_provide=allowed reader_receive=allowed")!=std::string::npos);
    check(admission.find("local_generation=1")!=std::string::npos&&admission.find("server_generation=1")!=std::string::npos);
    auto diagnosticCommand=ar::applicationHeader(22);diagnosticCommand.number(8);diagnosticCommand.number(1);
    auto diagnosticBefore=b.archiveAuthorizationState();reader.invalidate();
    auto diagnosticReply=ar::executeSyncOperation(reader,execution,22,diagnosticCommand.data,now,1);
    check(!diagnosticReply.empty()&&reader.authorizationDiagnostics(8,1).find("metadata_stale")!=std::string::npos&&b.archiveAuthorizationState()==diagnosticBefore);
    check(execution.diagnostics().find("active_scope=none")!=std::string::npos);
    reader.refresh(metadata(pb,3,registered));
    check(reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    reader.policy().setHistorySyncEnabled(false);
    ar::reconcileInitialSync(reader,execution);check(!execution.running());
    for(const auto& device:reader.devices())if(device.peerId==pb.peerId)check(device.authorized&&!device.mayRead&&device.grantedRead&&!device.grantedServe);
    reader.policy().setHistorySyncEnabled(true);
    reader.automaticSync(pa.peerId,false);ar::reconcileInitialSync(reader,execution);check(!execution.running());
    reader.automaticSync(pa.peerId,true);reader.automaticSync(pb.peerId,false);ar::reconcileInitialSync(reader,execution);check(!execution.running());
    reader.automaticSync(pb.peerId,true);
    ar::reconcileInitialSync(reader,execution);check(execution.running()&&execution.scope()->archive.source==pa.peerId);
    check(source.pendingPublications().empty()&&reader.pendingPublications().empty()); // Starting cannot create a grant.
    auto drain=[&]{unsigned steps=0;while(!execution.snapshot().complete){
        auto request=execution.request(*execution.scope(),1000,7);check(!request.empty());
        // A late cleanup callback from a retired attempt has the same scope
        // as the new winner. It must not clear that winner's in-flight page.
        execution.disconnected(*execution.scope(),6);
        if(!execution.snapshot().pending)throw std::runtime_error("Retired archive attempt cleared the winner's pending request");
        check(execution.request(*execution.scope(),1001,7).empty());
        if(steps==0){
            execution.disconnected(*execution.scope(),7);check(!execution.snapshot().pending);
            auto replacement=execution.request(*execution.scope(),1002,8);check(!replacement.empty()&&replacement!=request);request=std::move(replacement);
            execution.disconnected(*execution.scope(),7);check(execution.snapshot().pending);
        }
        auto sourceScope=ar::sessionAuthorization(source.policy(),pa.peerId,pa.peerId,pb.peerId);
        auto response=ar::serveSyncControl(a,pa,source.policy(),sourceScope,request,
            [&](const std::string& peer){return a.historicalDeviceAccount(peer);},100);
        execution.control(*execution.scope(),response,100,1000);check(++steps<100);
    }execution.tick(1001,1);check(!execution.running());};
    drain();check(execution.snapshot().complete);
    // The response deadline belongs to its request's transport too. A delayed
    // cleanup of old attempt 70 must not make the timer close replacement 71.
    execution.start(pa.peerId,pa.peerId,true);
    check(!execution.request(*execution.scope(),1000,70).empty());
    session::RouteObservation relay{{71},"relay:deadline","server","test",false,0,session::TransportKind::Relay,session::RouteClass::ServerRelay};
    check(sessions.registerArchiveControlAttempt({1},{71},*execution.scope(),relay,1001));
    session::DirectTransportAdapter replacement({1},{71},relay,pa.peerId);
    check(sessions.observeTransport(replacement.identityPresented(pa.peerId,0),1002));
    check(sessions.markLocallyTrusted({1},{71})&&sessions.markRemoteAccepted({1},{71}));
    check(sessions.arbitrateWinner({1},{71},1003).activated());
    const auto expired=execution.tick(16000,0);
    if(!expired.empty()||!sessions.snapshot(execution.scope()->continuityKey())->connected)
        throw std::runtime_error("Old archive request deadline retired the replacement winner");
    check(!execution.snapshot().pending);
    // A deadline on the actual owner still closes it and schedules bounded
    // retry. Protecting replacements must not disable legitimate timeouts.
    check(!execution.request(*execution.scope(),16001,71).empty());
    const auto ownedDeadline=execution.tick(31001,0);
    check(ownedDeadline.size()==1&&ownedDeadline.front().value==71&&!execution.snapshot().pending);
    check(!sessions.snapshot(execution.scope()->continuityKey())->connected&&
        sessions.snapshot(execution.scope()->continuityKey())->retryScheduled);
    drain();
    ar::reconcileInitialSync(reader,execution,59999);check(!execution.running());
    Storage contact((root/L"contact").wstring());auto pc=contact.profile();
    a.rememberAuthenticatedDeviceAccount(pc.peerId,9);b.rememberAuthenticatedDeviceAccount(pc.peerId,9);
    StoredMessage sent{Storage::newMessageId(),pa.peerId,pc.peerId,L"sent multiline \u4e00\nsecond line",100,Delivery::Delivered,0,1};
    StoredMessage received{Storage::newMessageId(),pc.peerId,pa.peerId,L"received",101,Delivery::Delivered,0,2};
    check(a.append(pc.peerId,sent)&&a.append(pc.peerId,received));
    ar::reconcileInitialSync(reader,execution,60000);check(execution.running());drain();
    check(execution.received()==2&&execution.lastSuccessful()==100&&execution.historyRevision()>0);
    auto copies=b.history(pc.peerId);check(copies.size()==2&&b.message(sent.id)->sender==pa.peerId&&b.message(received.id)->sender==pc.peerId&&b.message(sent.id)->text==sent.text);
    check(b.pending(pc.peerId).empty()&&b.pendingMailboxUploads().empty());
    // Completion survives reopen and remains eligible; explicit Pause does not.
    {ar::Execution reopened(b,pb,reader.policy(),deliveries,sessions);check(reopened.needsAutomaticSync(0)&&reopened.lastSuccessful()==100&&reopened.received()==0);}
    execution.start(pa.peerId,pa.peerId,true);drain();check(execution.received()==0); // Replay is not newly received history.
    auto presentation=ar::applicationHeader(23);presentation.number(8);presentation.number(1);
    check(!ar::executeSyncOperation(reader,execution,23,presentation.data,1001,1).empty());
    presentation=ar::applicationHeader(23);presentation.number(9);presentation.number(1);
    rejects([&]{ar::executeSyncOperation(reader,execution,23,presentation.data,1001,1);});
    execution.stop();ar::reconcileInitialSync(reader,execution);check(!execution.running());
    {ar::Execution reopened(b,pb,reader.policy(),deliveries,sessions);ar::reconcileInitialSync(reader,reopened);check(!reopened.running());}
    // A legacy inactive bit is indistinguishable from explicit Pause. Preserve
    // it; guessing completion would silently override the old user's intent.
    {protocol::Writer key(ar::LocalFormat);key.string("PeerChat/ArchiveIntentContext/v1");key.number(8);key.string(pb.peerId);
        std::string context;static const char hex[]="0123456789abcdef";for(auto byte:digestDevicePayload(key.data)){context+=hex[byte>>4];context+=hex[byte&15];}
        auto saved=b.archiveSyncState(context);protocol::Writer legacy(ar::LocalFormat);legacy.string("PeerChat/ArchiveIntent/v1");legacy.number(8);legacy.string(pb.peerId);legacy.string(pa.peerId);legacy.string(pa.peerId);legacy.number(0);
        b.saveArchiveSyncState(context,saved,legacy.data);ar::Execution reopened(b,pb,reader.policy(),deliveries,sessions);check(!reopened.needsAutomaticSync(120000));
    }
    check(reader.policy().historySyncEnabled());
    reader.policy().setHistorySyncEnabled(false);
    check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    {ar::Authorization reopened(b,pb,8,[](auto,const auto&){return true;},[](auto,const auto&,const auto&)->std::optional<ar::CurrentGrant>{return {};});check(!reopened.historySyncEnabled());}
    reader.policy().setHistorySyncEnabled(true);
    check(reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory)); // Same grant, not re-enrollment.
    auto bad=metadata(pb,4,registered);bad.grants.back().proof.back()^=1;
    rejects([&]{reader.refresh(bad);});check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    reader.refresh(metadata(pb,4,registered));
    rejects([&]{reader.refresh(metadata(pb,2,registered));});check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    reader.refresh(metadata(pb,5,registered));now+=30000;
    check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));rejects([&]{reader.devices();});
    reader.refresh(metadata(pb,6,registered));reader.authenticated(2);
    rejects([&]{reader.refresh(metadata(pb,6,registered));});
    reader.refresh(metadata(pb,1,registered,2));check(reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    source.refresh(metadata(pa,3,registered));auto revoked=source.revokeDevice(pb.peerId,30,true);
    auto renewed=source.authorizeDevice(pb.peerId,ar::ReadHistory,31,true);check(renewed.generation==3);
    // Both intermediate generations survive process loss, even when local
    // re-enrollment happened before the revoke reached the server.
    {ar::Enrollment restarted(a,pa,8,clock);restarted.authenticated(1);
        check(restarted.registration(40)==da&&restarted.pendingPublications().size()==2);
        check(ar::encodeGrant(restarted.pendingPublications()[0])==ar::encodeGrant(revoked));
        check(ar::encodeGrant(restarted.pendingPublications()[1])==ar::encodeGrant(renewed));
    }
    registered.back()={ar::encodeGrant(revoked),false};
    source.refresh(metadata(pa,4,registered));reader.refresh(metadata(pb,2,registered,2));
    for(const auto& device:reader.devices())if(device.peerId==pb.peerId)check(device.revoked&&!device.mayRead);
    check(source.pendingPublications().size()==1&&!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    registered.back()={ar::encodeGrant(renewed),true};
    source.refresh(metadata(pa,5,registered));reader.refresh(metadata(pb,3,registered,2));
    check(source.pendingPublications().empty()&&reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    // Stored-but-disabled tombstones confirm storage, NOT capability. They
    // cannot strand the publication journal ahead of an explicit successor.
    auto disabled=source.authorizeDevice(pb.peerId,ar::ReadHistory,32,true);
    registered.back()={ar::encodeGrant(disabled),false};source.refresh(metadata(pa,6,registered));
    check(source.pendingPublications().empty()&&!source.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    auto successor=source.authorizeDevice(pb.peerId,ar::ReadHistory,33,true);
    check(successor.generation==disabled.generation+1);
    registered.back()={ar::encodeGrant(successor),true};source.refresh(metadata(pa,7,registered));
    check(source.pendingPublications().empty()&&source.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    auto removed=metadata(pb,4,registered,2);removed.descriptors={db};removed.grants[0].eligible=false;removed.grants[1].eligible=false;
    reader.refresh(removed);check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    reader.refresh(metadata(pb,5,registered,2));reader.disableIssuer(pa.peerId,true);
    auto deniedIncoming=ar::decodeSessionBinding(ar::sessionBinding(ar::sessionAuthorization(source.policy(),pa.peerId,pa.peerId,pb.peerId)).data,pb.peerId);
    rejects([&]{execution.incoming(deniedIncoming,now,1);});
    check(execution.diagnostics().find("authorizer_disabled")!=std::string::npos&&execution.diagnostics().find("exact_scope_current=0")!=std::string::npos);
    check(reader.authorizationDiagnostics(8,2).find("authorizer_disabled")!=std::string::npos);
    check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{reader.approveIssuer(pa.peerId,true);}); // No accidental resurrection.
    auto wrong=metadata(pb,6,registered,2);wrong.account=9;rejects([&]{reader.refresh(wrong);});
    wrong=metadata(pb,6,registered,2);wrong.device=pa.peerId;rejects([&]{reader.refresh(wrong);});
    wrong=metadata(pb,6,registered,2);wrong.descriptors.push_back(db);rejects([&]{reader.refresh(wrong);});
    // Bounded durable publication queue; exceeding it cannot partly mutate a
    // grant or drop an earlier intermediate generation.
    for(size_t n=0;n<ar::MaxPendingGrants;++n)source.authorizeDevice(pb.peerId,ar::ReadHistory,100+n,true);
    auto last=source.policy().grant(pa.peerId,pb.peerId);
    rejects([&]{source.authorizeDevice(pb.peerId,ar::ReadHistory,200,true);});
    check(source.policy().grant(pa.peerId,pb.peerId).generation==last.generation&&
        source.pendingPublications().size()==ar::MaxPendingGrants);
    {ar::Enrollment restarted(a,pa,8,clock);check(restarted.pendingPublications().size()==ar::MaxPendingGrants);}
    check(a.profile().peerId==pa.peerId&&b.profile().peerId==pb.peerId);
    // Independent permissions are signed replacements, not whole-device revoke.
    Storage permissionStore((root/L"permissions").wstring());auto identity=permissionStore.profile();ar::Enrollment permissions(permissionStore,identity,18,clock);permissions.authenticated(1);
    auto descriptor=permissions.registration(100);ar::EnrollmentMetadata view;view.account=18;view.authenticationGeneration=1;view.revision=1;view.device=identity.peerId;view.serverEpoch="permissions";view.descriptors={descriptor};permissions.refresh(view);permissions.approveIssuer(identity.peerId,true);
    auto both=permissions.authorizeDevice(identity.peerId,ar::AllCapabilities,101,true);view.revision++;view.grants={{ar::encodeGrant(both),true}};permissions.refresh(view);
    auto provideOnly=permissions.authorizeDevice(identity.peerId,ar::ServeHistory,102,true);view.revision++;view.grants={{ar::encodeGrant(provideOnly),true}};permissions.refresh(view);check(!provideOnly.revoked&&!permissions.devices()[0].grantedRead&&permissions.devices()[0].grantedServe);
    auto receiveOnly=permissions.authorizeDevice(identity.peerId,ar::ReadHistory,103,true);view.revision++;view.grants={{ar::encodeGrant(receiveOnly),true}};permissions.refresh(view);check(!receiveOnly.revoked&&permissions.devices()[0].grantedRead&&!permissions.devices()[0].grantedServe);
    rejects([&]{permissions.authorizeDevice(identity.peerId,0,104,true);});check(permissions.policy().cachedGrant(identity.peerId,identity.peerId)->generation==receiveOnly.generation);
    {ar::Enrollment reopened(permissionStore,identity,18,clock);reopened.authenticated(1);reopened.refresh(view);check(reopened.devices()[0].grantedRead&&!reopened.devices()[0].grantedServe);}
    auto gone=permissions.revokeDevice(identity.peerId,105,true);view.revision++;view.grants={{ar::encodeGrant(gone),false}};permissions.refresh(view);check(permissions.devices()[0].revoked&&!permissions.devices()[0].authorized);
    auto context=permissions.automaticContext(identity.peerId);permissions.automaticSync(identity.peerId,false);auto saved=permissionStore.archiveSyncState(context);auto damaged=saved;damaged.back()^=2;permissionStore.saveArchiveSyncState(context,saved,damaged);rejects([&]{permissions.automaticSync(identity.peerId);});check(permissionStore.profile().peerId==identity.peerId);
    // Two individually permitted devices under different issuers must NOT be
    // combined into one archive authorization. Mirrors the physical ambiguity.
    Storage splitA((root/L"split-source").wstring()),splitB((root/L"split-reader").wstring());auto sa=splitA.profile(),sb=splitB.profile();
    ar::Enrollment se(splitA,sa,19,clock),re(splitB,sb,19,clock);se.authenticated(1);re.authenticated(1);
    ar::EnrollmentMetadata sm;sm.account=19;sm.authenticationGeneration=1;sm.revision=1;sm.device=sa.peerId;sm.serverEpoch="split";sm.descriptors={se.registration(1),re.registration(1)};
    se.refresh(sm);sm.device=sb.peerId;re.refresh(sm);se.approveIssuer(sa.peerId,true);re.approveIssuer(sb.peerId,true);
    auto sg=se.authorizeDevice(sa.peerId,ar::ServeHistory,2,true),rg=re.authorizeDevice(sb.peerId,ar::ReadHistory,2,true);
    sm.revision=2;sm.grants={{ar::encodeGrant(sg),true},{ar::encodeGrant(rg),true}};re.refresh(sm);re.approveIssuer(sa.peerId,true);
    check(re.policy().permits(sa.peerId,sa.peerId,ar::ServeHistory)&&re.policy().permits(sb.peerId,sb.peerId,ar::ReadHistory));
    check(re.authorizationDiagnostics(19,1).find("incompatible_authorizers")!=std::string::npos);rejects([&]{re.sourceIssuer(sa.peerId);});
    auto splitBefore=splitB.archiveAuthorizationState();for(int i=0;i<10;++i)check(re.authorizationDiagnostics(19,1).size()<49152);check(splitBefore==splitB.archiveAuthorizationState());
    // A running legacy intent must not remain pinned to an obsolete authority
    // when a complete, independently trusted current grant pair already exists.
    delivery::DeliveryManager splitDelivery(splitB,sb.peerId);
    session::SessionTransportManager splitSessions("archive-restored",sb.peerId,{1});
    protocol::Writer intentKey(ar::LocalFormat);intentKey.string("PeerChat/ArchiveIntentContext/v1");intentKey.number(19);intentKey.string(sb.peerId);
    std::string intentContext;static const char digits[]="0123456789abcdef";
    for(auto byte:digestDevicePayload(intentKey.data)){intentContext+=digits[byte>>4];intentContext+=digits[byte&15];}
    auto writeIntent=[&](unsigned active){protocol::Writer w(ar::LocalFormat);w.string("PeerChat/ArchiveIntent/v1");w.number(19);w.string(sb.peerId);w.string(sa.peerId);w.string(sa.peerId);w.number(active);splitB.saveArchiveSyncState(intentContext,splitB.archiveSyncState(intentContext),w.data);};
    writeIntent(1);
    {ar::Execution pending(splitB,sb,re.policy(),splitDelivery,splitSessions);ar::reconcileInitialSync(re,pending);check(!pending.running());}
    auto alternate=re.authorizeDevice(sa.peerId,ar::ServeHistory,3,true);
    sm.revision=3;sm.grants.push_back({ar::encodeGrant(alternate),true});re.refresh(sm);re.disableIssuer(sa.peerId,true);
    sm.device=sa.peerId;se.refresh(sm);se.approveIssuer(sb.peerId,true);sm.device=sb.peerId;
    splitA.rememberAuthenticatedDeviceAccount(pc.peerId,9);splitB.rememberAuthenticatedDeviceAccount(pc.peerId,9);
    StoredMessage recovered{Storage::newMessageId(),pc.peerId,sa.peerId,L"restored under existing trusted grant",200,Delivery::Delivered,0,1};check(splitA.append(pc.peerId,recovered));
    auto policyBefore=splitB.archiveAuthorizationState();
    {ar::Execution restored(splitB,sb,re.policy(),splitDelivery,splitSessions);ar::reconcileInitialSync(re,restored);check(restored.running()&&restored.scope()->archive.issuer==sb.peerId&&restored.scope()->archive.source==sa.peerId);
        auto providerScope=ar::sessionAuthorization(se.policy(),sb.peerId,sa.peerId,sb.peerId);unsigned steps=0;
        while(!restored.snapshot().complete){auto request=restored.request(*restored.scope(),100,1);check(!request.empty());auto response=ar::serveSyncControl(splitA,sa,se.policy(),providerScope,request,[&](const std::string& peer){return splitA.historicalDeviceAccount(peer);},201);restored.control(*restored.scope(),response,201,100);check(++steps<100);}
        restored.tick(101,1);check(splitB.message(recovered.id)&&splitB.message(recovered.id)->text==recovered.text&&splitB.history(pc.peerId).size()==1);
    }
    {ar::Execution reopened(splitB,sb,re.policy(),splitDelivery,splitSessions);check(reopened.needsAutomaticSync(0)&&reopened.lastAutomaticSource()==sa.peerId);}
    check(splitB.archiveAuthorizationState()==policyBefore&&!re.policy().trustsAuthority(sa.publicKey));
    // A paused legacy intent is not an implicit request to resume, even though
    // the same alternate authorization is valid.
    writeIntent(0);{ar::Execution paused(splitB,sb,re.policy(),splitDelivery,splitSessions);ar::reconcileInitialSync(re,paused);check(!paused.running());}
    // Expired metadata cannot authorize rebinding; no authorization expiry is
    // relaxed by the earlier application refresh cadence.
    writeIntent(1);now+=30000;
    {ar::Execution stale(splitB,sb,re.policy(),splitDelivery,splitSessions);ar::reconcileInitialSync(re,stale);check(!stale.running());}
    std::cout<<"PASS: explicit source/reader consent, bounded read-only admission reasons, incompatible authorizers, authenticated metadata binding/freshness, no same-account grant, durable exact descriptor and grant retries, generation-ordered revoke/re-enroll, removal and fail-closed tamper/reconnect\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
