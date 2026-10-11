#include "../storage.cpp"
#include "../archive_application.hpp"
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
    for(const auto& device:reader.devices())check(!device.mayRead&&!device.mayServe&&!device.approvedIssuer);
    delivery::DeliveryManager deliveries(b,pb.peerId);
    session::SessionTransportManager sessions("archive-enrollment",pb.peerId,{1});
    ar::Execution execution(b,pb,reader.policy(),deliveries,sessions);
    ar::reconcileInitialSync(reader,execution);check(!execution.running()); // No implicit same-account permission.
    rejects([&]{source.authorizeDevice(pb.peerId,ar::ReadHistory,20,true);});
    rejects([&]{source.approveIssuer(pa.peerId,false);});source.approveIssuer(pa.peerId,true);
    auto serving=source.authorizeDevice(pa.peerId,ar::ServeHistory,20,true);
    auto reading=source.authorizeDevice(pb.peerId,ar::ReadHistory,20,true);
    check(source.pendingPublications().size()==2&&!source.policy().permits(pa.peerId,pa.peerId,ar::ServeHistory));
    std::vector<ar::RegisteredGrant> registered{{ar::encodeGrant(serving),true},{ar::encodeGrant(reading),true}};
    source.refresh(metadata(pa,2,registered));reader.refresh(metadata(pb,2,registered));
    check(source.pendingPublications().empty()&&!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    rejects([&]{reader.approveIssuer(pa.peerId,false);});reader.approveIssuer(pa.peerId,true);
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
        auto request=execution.request(*execution.scope(),1000);check(!request.empty());
        auto sourceScope=ar::sessionAuthorization(source.policy(),pa.peerId,pa.peerId,pb.peerId);
        auto response=ar::serveSyncControl(a,pa,source.policy(),sourceScope,request,
            [&](const std::string& peer){return a.historicalDeviceAccount(peer);},100);
        execution.control(*execution.scope(),response,100,1000);check(++steps<100);
    }execution.tick(1001,1);check(!execution.running());};
    drain();check(execution.snapshot().complete);
    ar::reconcileInitialSync(reader,execution,59999);check(!execution.running());
    Storage contact((root/L"contact").wstring());auto pc=contact.profile();
    a.rememberAuthenticatedDeviceAccount(pc.peerId,9);b.rememberAuthenticatedDeviceAccount(pc.peerId,9);
    StoredMessage sent{Storage::newMessageId(),pa.peerId,pc.peerId,L"sent multiline \u4e00\nsecond line",100,Delivery::Delivered,0,1};
    StoredMessage received{Storage::newMessageId(),pc.peerId,pa.peerId,L"received",101,Delivery::Delivered,0,2};
    check(a.append(pc.peerId,sent)&&a.append(pc.peerId,received));
    ar::reconcileInitialSync(reader,execution,60000);check(execution.running());drain();
    auto copies=b.history(pc.peerId);check(copies.size()==2&&b.message(sent.id)->sender==pa.peerId&&b.message(received.id)->sender==pc.peerId&&b.message(sent.id)->text==sent.text);
    check(b.pending(pc.peerId).empty()&&b.pendingMailboxUploads().empty());
    // Completion survives reopen and remains eligible; explicit Pause does not.
    {ar::Execution reopened(b,pb,reader.policy(),deliveries,sessions);check(reopened.needsAutomaticSync(0));}
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
    auto bad=metadata(pb,3,registered);bad.grants.back().proof.back()^=1;
    rejects([&]{reader.refresh(bad);});check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    reader.refresh(metadata(pb,3,registered));
    rejects([&]{reader.refresh(metadata(pb,2,registered));});check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));
    reader.refresh(metadata(pb,4,registered));now+=30000;
    check(!reader.policy().permits(pa.peerId,pb.peerId,ar::ReadHistory));rejects([&]{reader.devices();});
    reader.refresh(metadata(pb,5,registered));reader.authenticated(2);
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
    std::cout<<"PASS: explicit source/reader consent, authenticated metadata binding/freshness, no same-account grant, durable exact descriptor and grant retries, generation-ordered revoke/re-enroll, removal and fail-closed tamper/reconnect\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
