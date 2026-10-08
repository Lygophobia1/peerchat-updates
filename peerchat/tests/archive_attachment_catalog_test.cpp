#define _WIN32_WINNT 0x0602
#define NOMINMAX
#include "../archive_attachment_transfer.hpp"
#include "../windows_transfer_io.hpp"
#include "../portable/transfer_storage.hpp"
#include <filesystem>
#include <iostream>
using namespace peerchat;
static void check(bool value){if(!value)throw std::runtime_error("Attachment catalog regression");}
template<class F>static void rejects(F body){bool rejected=false;try{body();}catch(const std::exception&){rejected=true;}check(rejected);}
struct TestSource:transfer::Source {
    Blob bytes;std::string filename;
    TestSource(Blob b,std::string n):bytes(std::move(b)),filename(std::move(n)){}
    std::uint64_t size()const override{return bytes.size();}
    std::string name()const override{return filename;}
    Blob read(std::uint64_t offset,size_t count)override{return Blob(bytes.begin()+offset,bytes.begin()+offset+std::min<std::uint64_t>(count,bytes.size()-offset));}
};
struct TestSink:transfer::Sink {
    Blob& bytes;bool& committed;
    TestSink(Blob& b,bool& c):bytes(b),committed(c){}
    void append(const Blob& b)override{bytes.insert(bytes.end(),b.begin(),b.end());}
    void commit()override{committed=true;}
};
int main(){try{
    auto root=std::filesystem::current_path()/"build"/"tests"/("attachment-catalog-"+Storage::newMessageId());
    Storage a((root/"source").wstring()),b((root/"reader").wstring()),c((root/"contact").wstring());
    auto source=a.profile(),reader=b.profile(),contact=c.profile();
    for(auto storage:{&a,&b}){storage->rememberAuthenticatedDeviceAccount(source.peerId,8);storage->rememberAuthenticatedDeviceAccount(reader.peerId,8);storage->rememberAuthenticatedDeviceAccount(contact.peerId,9);}
    std::map<std::string,archive::CurrentGrant> grants;bool current=true;
    auto member=[&](std::uint64_t account,const std::string& device){return current&&account==8&&(device==source.peerId||device==reader.peerId);};
    auto published=[&](std::uint64_t account,const std::string& issuer,const std::string& device)->std::optional<archive::CurrentGrant>{if(account!=8||issuer!=source.peerId||!grants.count(device))return {};return grants.at(device);};
    archive::Authorization serving(a,source,8,member,published),reading(b,reader,8,member,published);
    serving.approveAuthority(source.publicKey,true);reading.approveAuthority(source.publicKey,true);
    auto sg=serving.issue(source.publicKey,archive::ServeHistory,false,100,true),rg=serving.issue(reader.publicKey,archive::ReadHistory,false,100,true);
    for(const auto& g:{sg,rg}){grants[g.device]={g.generation,g.capabilities,g.issuanceId,g.revoked};reading.accept(g);}
    auto sourceScope=archive::sessionAuthorization(serving,source.peerId,source.peerId,reader.peerId);
    auto readerScope=archive::sessionAuthorization(reading,source.peerId,source.peerId,reader.peerId);
    archive::AttachmentCatalog sourceCatalog(a),readerCatalog(b);
    auto directoryA=[&](const std::string& peer){return a.historicalDeviceAccount(peer);};
    auto directoryB=[&](const std::string& peer){return b.historicalDeviceAccount(peer);};
    archive::Attachment attachment;attachment.id=Storage::newMessageId();attachment.conversation=contact.peerId;attachment.sender=contact.peerId;attachment.recipient=source.peerId;
    attachment.senderAccount=9;attachment.recipientAccount=8;attachment.name=u8"résumé-日本.bin";attachment.size=100000;attachment.utc=100;attachment.digest.fill(42);
    attachment.location=Blob{'l','o','c','a','l','-','p','a','t','h'};
    check(sourceCatalog.retain(attachment)&&!sourceCatalog.retain(attachment));
    auto proof=archive::attestAttachment(source,serving,sourceScope,attachment,directoryA);
    check(std::search(proof.begin(),proof.end(),attachment.location.begin(),attachment.location.end())==proof.end());
    auto verified=archive::verifyAttachment(proof,reading,readerScope,directoryB);check(verified.location.empty()&&verified.id==attachment.id&&verified.proof==proof);
    check(readerCatalog.retain(verified)&&!readerCatalog.retain(verified));
    auto changed=attachment;changed.digest[0]^=1;rejects([&]{sourceCatalog.retain(changed);});
    auto damaged=proof;damaged.back()^=1;rejects([&]{archive::verifyAttachment(damaged,reading,readerScope,directoryB);});
    auto wrong=readerScope;wrong.archive.readerGrantId[0]=wrong.archive.readerGrantId[0]=='a'?'b':'a';rejects([&]{archive::verifyAttachment(proof,reading,wrong,directoryB);});
    auto spoofed=attachment;spoofed.senderAccount=8;rejects([&]{archive::attestAttachment(source,serving,sourceScope,spoofed,directoryA);});
    auto pathClaim=verified;pathClaim.location=Blob{'x'};readerCatalog.retain(pathClaim);
    auto remoteProjection=verified;readerCatalog.retain(remoteProjection);check(readerCatalog.find(attachment.id)->location==pathClaim.location);
    auto expected=a.attachmentMetadata(attachment.id);rejects([&]{a.saveAttachmentMetadata(attachment.id,{},expected);});
    auto watermark=a.attachmentMetadataWatermark();
    for(unsigned n=0;n<10;++n){auto another=attachment;another.id=Storage::newMessageId();check(sourceCatalog.retain(another));}
    auto first=a.attachmentMetadataPage(0,a.attachmentMetadataWatermark());check(first.records.size()==8&&!first.complete);
    auto second=a.attachmentMetadataPage(first.next,first.through);check(second.records.size()==3&&second.complete&&second.next==first.through);
    check(a.attachmentMetadataPage(0,watermark).records.size()==1);
    {
        auto sync=std::make_unique<archive::AttachmentReader>(b,reading,readerScope,directoryB);unsigned pages=0;
        while(!sync->snapshot().complete){auto request=sync->nextRequest();rejects([&]{sync->nextRequest();});
            auto response=archive::serveAttachmentPage(a,source,serving,sourceScope,request,directoryA);
            if(pages==0){auto bad=response;bad.back()^=1;rejects([&]{sync->accept(bad);});}
            sync->accept(response);++pages;
            if(pages==1){sync.reset();sync=std::make_unique<archive::AttachmentReader>(b,reading,readerScope,directoryB);}
        }
        check(pages==2&&sync->snapshot().processed==11);sync->rescan();auto request=sync->nextRequest();sync->disconnected();
        request=sync->nextRequest();sync->accept(archive::serveAttachmentPage(a,source,serving,sourceScope,request,directoryA));check(sync->snapshot().complete&&sync->snapshot().processed==11);
    }
    rejects([&]{a.attachmentMetadataPage(-1,watermark);});rejects([&]{a.attachmentMetadataPage(0,watermark,9);});
    // The archive adapter borrows exactly the same transfer mechanics as live
    // file sharing. No socket, peer trust promotion, chunk engine or fake ACK.
    {
        Blob content(100000);for(size_t n=0;n<content.size();++n)content[n]=static_cast<unsigned char>(n*17);
        transfer::windows::Sha256 hash;for(size_t n=0;n<content.size();n+=transfer::ChunkBytes)hash.update(content.data()+n,std::min(transfer::ChunkBytes,content.size()-n));
        auto file=attachment;file.id=Storage::newMessageId();file.digest=hash.finish();sourceCatalog.retain(file);
        auto attested=archive::verifyAttachment(archive::attestAttachment(source,serving,sourceScope,file,directoryA),reading,readerScope,directoryB);readerCatalog.retain(attested);
        transfer::TransferManager sender([]{return std::make_unique<transfer::windows::Sha256>();}),receiver([]{return std::make_unique<transfer::windows::Sha256>();});
        Blob received;bool committed=false;unsigned leases=0;
        bool sourceAvailable=true;
        auto open=[&](const archive::Attachment& f){if(!sourceAvailable)throw std::runtime_error("Test-only missing source");return std::make_unique<TestSource>(content,f.name);};
        auto sink=[&](const transfer::Metadata&,const Blob&){return std::make_unique<TestSink>(received,committed);};
        auto completed=[](const transfer::DurableState&){return Blob{'v','e','r','i','f','i','e','d'};};
        auto lease=[&](const session::ReliableAuthorization&,std::uint64_t){++leases;};
        archive::AttachmentTransfers sending(a,source,serving,sender,open,sink,completed,lease);
        auto receiving=std::make_unique<archive::AttachmentTransfers>(b,reader,reading,receiver,open,sink,completed,lease);
        auto id=receiving->download(readerScope,file.id,Blob{'d','i','r'});
        check(receiving->download(readerScope,file.id,Blob{'o','t','h','e','r'})==id);
        // Pending request survives a controller/process recreation before Offer.
        receiving.reset();receiving=std::make_unique<archive::AttachmentTransfers>(b,reader,reading,receiver,open,sink,completed,lease);
        auto fetch=receiving->request(readerScope,1);archive::validateAttachmentControl(fetch);check(receiving->request(readerScope,1).empty());
        auto wrongScope=sourceScope;wrongScope.archive.readerGrantId[0]=wrongScope.archive.readerGrantId[0]=='a'?'b':'a';rejects([&]{sending.control(wrongScope,fetch,1);});
        sending.control(sourceScope,fetch,1);sending.control(sourceScope,fetch,1);
        for(std::uint64_t now=1;now<30000&&!readerCatalog.find(file.id)->location.size();now+=10){
            sending.maintain(now);receiving->maintain(now);
            sender.tick(now,[](const std::string&){return 1;},[&](const std::string&,const Blob& frame){auto wrapped=sending.outgoing(sourceScope,frame);archive::validateAttachmentControl(wrapped);receiving->control(readerScope,wrapped,now);return true;});
            receiver.tick(now,[](const std::string&){return 1;},[&](const std::string&,const Blob& frame){sending.control(sourceScope,receiving->outgoing(readerScope,frame),now);return true;});
        }
        check(committed&&received==content&&leases&&readerCatalog.find(file.id)->location==completed({}));
        check(receiving->wasArchiveTransfer(id)&&!receiving->owns(id));
        rejects([&]{receiving->download(readerScope,file.id,Blob{'d','i','r'});});
        // Losing the final Complete ACK does not admit a new file or lose the
        // original transfer identity. Only its protected completed binding may
        // issue the existing TransferManager receipt again.
        protocol::Writer finish(protocol::FileTransfer);finish.number(1);finish.number(static_cast<unsigned>(transfer::Kind::Finish));finish.string(id);
        receiving->control(readerScope,archive::wrapAttachmentTransfer(file.id,finish.data),30001);check(receiver.hasPendingControl(id));
        receiver.tick(30002,[](const std::string&){return 2;},[&](const std::string&,const Blob& frame){archive::validateAttachmentControl(receiving->outgoing(readerScope,frame));return true;});receiving->maintain(30003);sending.maintain(30003);
        // Same-account protected journal is an atomic CAS snapshot, not an
        // Android-only outbox or chunk engine. Reopen preserves exact identity.
        auto sourceFactory=[&](const transfer::DurableState& d){return std::make_unique<TestSource>(content,d.metadata.name);};
        auto sinkFactory=[&](const transfer::DurableState&){return std::make_unique<TestSink>(received,committed);};
        transfer::ProtectedStorageJournal journal(b,reader.peerId,sourceFactory,sinkFactory);check(journal.load().empty());
        transfer::DurableState durable;durable.metadata={Storage::newMessageId(),file.name,file.size,file.digest};durable.peer=source.peerId;durable.created=durable.updated=journal.wallTime();durable.expires=durable.created+transfer::RetentionMilliseconds;durable.sending=true;durable.digestReady=true;
        journal.save({durable});transfer::ProtectedStorageJournal reopened(b,reader.peerId,sourceFactory,sinkFactory);check(reopened.load().at(0).metadata==durable.metadata);
        rejects([&]{b.saveTransferJournal(reader.peerId,{},Blob{1});});check(b.transferJournal(reader.peerId).size()>32);
        auto next=file;next.id=Storage::newMessageId();sourceCatalog.retain(next);readerCatalog.retain(archive::verifyAttachment(archive::attestAttachment(source,serving,sourceScope,next,directoryA),reading,readerScope,directoryB));
        auto missingId=receiving->download(readerScope,next.id,Blob{'d','i','r'});sourceAvailable=false;
        sending.control(sourceScope,receiving->request(readerScope,3),40000);auto unavailable=sending.request(sourceScope,3);archive::validateAttachmentControl(unavailable);receiving->control(readerScope,unavailable,40001);
        check(receiving->pending().size()==1&&receiving->pending()[0].state==transfer::State::Failed&&receiving->request(readerScope,4).empty());
        receiving->cancel(missingId);sending.cancel(missingId);check(receiving->pending().empty());sourceAvailable=true;
        auto revokedId=receiving->download(readerScope,next.id,Blob{'d','i','r'});current=false;rejects([&]{receiving->request(readerScope,5);});check(!receiving->scopeForPeer(source.peerId));current=true;receiving->cancel(revokedId);
        auto journalBytes=b.transferJournal(reader.peerId);b.saveTransferJournal(reader.peerId,journalBytes,Blob{1});transfer::ProtectedStorageJournal corrupt(b,reader.peerId,sourceFactory,sinkFactory);rejects([&]{corrupt.load();});b.saveTransferJournal(reader.peerId,Blob{1},journalBytes);
        check(a.pending(contact.peerId).empty()&&b.pending(contact.peerId).empty());
    }
    {Storage reopened((root/"reader").wstring());archive::AttachmentCatalog persisted(reopened);check(persisted.find(attachment.id)->proof==proof&&persisted.find(attachment.id)->location==pathClaim.location);}
    current=false;rejects([&]{archive::verifyAttachment(proof,reading,readerScope,directoryB);});check(readerCatalog.find(attachment.id).has_value());
    check(a.pending(contact.peerId).empty()&&b.pending(contact.peerId).empty());
    std::cout<<"PASS: protected attachment metadata CAS/reopen, bounded stable catalog pages, Unicode/duplicate filenames, immutable logical IDs, scoped archive attestation, path exclusion, account/device binding, tamper/revocation and no message outbox. HOST ONLY\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
