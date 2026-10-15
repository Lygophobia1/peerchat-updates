#pragma once
#include "archive_session.hpp"
#include "group_message_envelope.hpp"
#include "group_text.hpp"

namespace peerchat::archive {
constexpr const char* GroupHistoryDomain="PeerChat/ArchiveGroupHistory/v1";
struct GroupHistoryContext {group_keys::RosterSnapshot roster;GroupState ledger;};
using GroupHistoryContextProvider=std::function<std::optional<GroupHistoryContext>(const std::string&)>;
inline bool reconciliationPeer(const group_keys::RosterSnapshot& roster,const std::string& local,const std::string& remote,std::uint64_t round){
    if(local==roster.distributorPeerId||remote==roster.distributorPeerId)return true;
    std::vector<std::string> peers;for(const auto& d:roster.devices)if(!d.invited&&d.peerId!=local&&d.peerId!=roster.distributorPeerId)peers.push_back(d.peerId);
    std::sort(peers.begin(),peers.end());if(peers.empty())return false;
    const auto start=static_cast<size_t>(round%peers.size());
    for(size_t n=0;n<std::min<size_t>(2,peers.size());++n)if(peers[(start+n)%peers.size()]==remote)return true;
    return false;
}
// Application adapter only: existing storage, epoch keys, signature/GCM providers
// and group message records. No sockets, delivery ACKs, key release or retry owner.
class GroupHistoryAccess {
    Storage& store_;const Profile& local_;std::uint64_t account_;
    GroupHistoryContextProvider context_;std::function<void(const std::string&)> imported_;
public:
    struct Checked {unsigned state=1;std::string local;Blob plaintext;}; // 0 eligible, 1 unresolved, 2 denied
    GroupHistoryAccess(Storage& store,const Profile& local,std::uint64_t account,GroupHistoryContextProvider context,
        std::function<void(const std::string&)> imported={})
        :store_(store),local_(local),account_(account),context_(std::move(context)),imported_(std::move(imported)){require(account_&&bool(context_));}
    Checked check(const Blob& wire,const session::ReliableAuthorization& scope,bool providing=false){
        require(scope.valid());const bool group=scope.scope==session::ReliableAuthorizationScope::GroupControl;
        require(group||(scope.permitsArchiveControl()&&scope.archive.account==account_&&(scope.archive.source==local_.peerId||scope.archive.reader==local_.peerId)));
        group_messages::Envelope e;require(group_messages::parse(wire,e)==group_messages::Result::Accepted);
        require(!group||e.groupId==scope.groupId);
        auto current=context_(e.groupId);if(!current)return {};
        const auto& c=*current;require(c.roster.serverGroupId==e.groupId&&group_keys::GroupKeyManager::matchesSignedLedger(c.roster,c.ledger));
        std::uint64_t sourceAccount=account_,readerAccount=account_;auto source=scope.archive.source,reader=scope.archive.reader;
        if(group){
            require(c.roster.generation==scope.rosterGeneration);std::uint64_t remoteAccount=0;
            for(const auto& d:c.roster.devices)if(!d.invited&&d.peerId==scope.expectedRemoteDeviceId)remoteAccount=d.accountId;
            require(remoteAccount&&group_keys::GroupKeyManager::currentMember(c.roster,account_,local_.peerId));
            source=providing?local_.peerId:scope.expectedRemoteDeviceId;reader=providing?scope.expectedRemoteDeviceId:local_.peerId;
            sourceAccount=providing?account_:remoteAccount;readerAccount=providing?remoteAccount:account_;
        }
        if(!group_keys::GroupKeyManager::currentMember(c.roster,sourceAccount,source)||
           !group_keys::GroupKeyManager::currentMember(c.roster,readerAccount,reader))return {2,{}, {}};
        // Only a currently authenticated, signed-ledger-matched epoch can be
        // captured. An old envelope/current membership is never backdated.
        group_history::captureEpoch(store_,e.groupId,c.roster.generation,c.ledger);
        auto old=group_history::retainedEpoch(store_,e.groupId,e.rosterGeneration);
        if(!old)return {};
        require(old->local==c.roster.legacyGroupId&&e.keyGeneration==e.rosterGeneration&&e.rosterGeneration<=c.roster.generation);
        if(!group_history::continuousMember(*old,c.ledger,sourceAccount,source)||
           !group_history::continuousMember(*old,c.ledger,readerAccount,reader))return {2,{}, {}};
        auto sender=old->devices.find(e.senderDevice);
        require(sender!=old->devices.end()&&sender->second.accountId==e.senderAccount&&
            verifyDevicePayload(group_messages::signingPayload(e),e.signature,sender->second.publicKey));
        struct Secret {Blob value;~Secret(){platform::group_crypto::wipe(value.data(),value.size());}};
        Secret key{store_.groupKey(old->local,e.keyGeneration)};if(key.value.empty())return {};
        Checked result;result.local=old->local;auto tag=e.tag;
        require(platform::group_crypto::gcm(false,key.value,group_messages::associatedData(e),e.nonce,e.ciphertext,result.plaintext,tag));
        if(!group_text::valid(result.plaintext)){platform::group_crypto::wipe(result.plaintext.data(),result.plaintext.size());require(false);}
        result.state=0;return result;
    }
    bool commit(const Blob& wire,Checked& checked){
        require(checked.state==0&&group_text::valid(checked.plaintext));group_messages::Envelope e;
        require(group_messages::parse(wire,e)==group_messages::Result::Accepted);
        const auto body=group_text::decode(checked.plaintext);
        bool inserted=store_.appendAuthenticatedGroupMessage(e.groupId,e.messageId,wire,checked.plaintext,checked.local);
        platform::group_crypto::wipe(checked.plaintext.data(),checked.plaintext.size());checked.plaintext.clear();
        store_.observeLogicalOrder(body.order);if(inserted&&imported_)imported_(checked.local);return inserted;
    }
};
inline protocol::Writer groupHistoryHeader(unsigned kind){protocol::Writer w(protocol::ArchiveControl);w.string(GroupHistoryDomain);w.number(1);w.number(kind);return w;}
struct GroupHistoryRequest {std::string id,store;std::uint64_t after=0,through=0;};
inline void groupRequestFields(protocol::Writer& w,const GroupHistoryRequest& q){
    require(protocol::id(q.id,32)&&(q.store.empty()||protocol::id(q.store,32))&&q.after<=INT64_MAX&&q.through<=INT64_MAX&&
        (!q.through||q.through>=q.after)&&(!q.store.empty()||(!q.after&&!q.through)));
    w.string(q.id);w.string(q.store);w.number(q.after);w.number(q.through);
}
inline GroupHistoryRequest groupRequestFields(protocol::Reader& r){GroupHistoryRequest q{r.string(),r.string(),r.number(),r.number()};auto w=groupHistoryHeader(1);groupRequestFields(w,q);return q;}
inline Blob encodeGroupHistoryRequest(const GroupHistoryRequest& q){auto w=groupHistoryHeader(1);groupRequestFields(w,q);return w.data;}
inline GroupHistoryRequest decodeGroupHistoryRequest(const Blob& bytes){require(bytes.size()<=512);protocol::Reader r(bytes);require(r.type()==protocol::ArchiveControl&&r.string()==GroupHistoryDomain&&r.number()==1&&r.number()==1);auto q=groupRequestFields(r);r.end();return q;}
struct GroupHistoryItem {std::uint64_t cursor=0;unsigned state=0;Blob wire;};
struct GroupHistoryPage {GroupHistoryRequest request;std::uint64_t next=0;bool complete=false;std::vector<GroupHistoryItem> items;};
inline Blob encodeGroupHistoryPage(const GroupHistoryPage& page){
    auto w=groupHistoryHeader(2);groupRequestFields(w,page.request);require(protocol::id(page.request.store,32)&&page.next>=page.request.after&&page.next<=page.request.through&&page.items.size()<=8&&(!page.complete||page.next==page.request.through)&&(page.complete||!page.items.empty()));
    w.number(page.next);w.number(page.complete);w.number(page.items.size());auto prior=page.request.after;
    for(const auto& item:page.items){require(item.cursor>prior&&item.cursor<=page.next&&item.state<=2&&((item.state==0)==!item.wire.empty()));
        prior=item.cursor;w.number(item.cursor);w.number(item.state);w.number(item.wire.size());w.data.insert(w.data.end(),item.wire.begin(),item.wire.end());require(w.data.size()<=protocol::MaxPacket);}
    require(page.complete||page.next==prior);return w.data;
}
inline GroupHistoryPage decodeGroupHistoryPage(const Blob& bytes){
    require(bytes.size()<=protocol::MaxPacket);protocol::Reader r(bytes);require(r.type()==protocol::ArchiveControl&&r.string()==GroupHistoryDomain&&r.number()==1&&r.number()==2);
    GroupHistoryPage page;page.request=groupRequestFields(r);page.next=r.number();auto complete=r.number();require(complete<=1);page.complete=complete!=0;auto count=r.number();require(count<=8);
    for(std::uint64_t n=0;n<count;++n){GroupHistoryItem item;item.cursor=r.number();auto state=r.number();require(state<=2);item.state=static_cast<unsigned>(state);auto length=r.number();require(length<=group_messages::MaxEnvelopeBytes&&length<=r.remaining());item.wire.assign(bytes.begin()+r.cursor,bytes.begin()+r.cursor+static_cast<size_t>(length));r.cursor+=static_cast<size_t>(length);page.items.push_back(std::move(item));}
    r.end();require(encodeGroupHistoryPage(page)==bytes);return page;
}
inline Blob serveGroupHistory(Storage& store,const Profile& local,Authorization& policy,const session::ReliableAuthorization& scope,const Blob& request,GroupHistoryAccess& access){
    require(scope.archive.source==local.peerId&&currentSessionAuthorization(policy,scope));auto q=decodeGroupHistoryRequest(request);
    auto id=store.archiveStoreId();require(q.store.empty()||q.store==id);q.store=id;if(!q.through)q.through=store.groupArchiveWatermark();require(q.through>=q.after);
    auto rows=store.groupArchivePage(q.after,q.through);GroupHistoryPage page{q,static_cast<std::uint64_t>(rows.next),rows.complete,{}};
    for(auto& row:rows.records){require(currentSessionAuthorization(policy,scope));auto checked=access.check(row.envelope,scope,true);platform::group_crypto::wipe(checked.plaintext.data(),checked.plaintext.size());
        page.items.push_back({static_cast<std::uint64_t>(row.sequence),checked.state,checked.state?Blob{}:std::move(row.envelope)});}
    require(currentSessionAuthorization(policy,scope));return encodeGroupHistoryPage(page);
}
inline Blob serveGroupHistory(Storage& store,const session::ReliableAuthorization& scope,const Blob& request,GroupHistoryAccess& access,const std::function<bool()>& authorized){
    require(scope.scope==session::ReliableAuthorizationScope::GroupControl&&scope.valid()&&authorized&&authorized());
    auto q=decodeGroupHistoryRequest(request);auto id=store.archiveStoreId();require(q.store.empty()||q.store==id);q.store=id;
    if(!q.through)q.through=store.groupArchiveWatermark(scope.groupId);
    require(q.through>=q.after);auto rows=store.groupArchivePage(q.after,q.through,8,scope.groupId);
    GroupHistoryPage page{q,static_cast<std::uint64_t>(rows.next),rows.complete,{}};
    for(auto& row:rows.records){require(authorized());auto checked=access.check(row.envelope,scope,true);platform::group_crypto::wipe(checked.plaintext.data(),checked.plaintext.size());page.items.push_back({static_cast<std::uint64_t>(row.sequence),checked.state,checked.state?Blob{}:std::move(row.envelope)});}
    require(authorized());return encodeGroupHistoryPage(page);
}
// A lane of SyncReader's single outstanding request. Durable position advances
// only after verified imports. The earliest unresolved position survives later
// successful pages, completion, restart and explicit rescan.
class GroupHistoryReader {
    Storage& store_;session::ReliableAuthorization scope_;GroupHistoryAccess& access_;std::function<bool()> authorized_;
    GroupHistoryRequest position_;std::string context_;Blob saved_;
    std::uint64_t resume_=0,processed_=0,deferred_=0;bool complete_=false,pending_=false;
    void persist(){auto w=groupHistoryHeader(3);groupRequestFields(w,position_);w.number(resume_);w.number(processed_);w.number(deferred_);w.number(complete_);store_.saveArchiveSyncState(context_,saved_,w.data);saved_=std::move(w.data);}
    void current()const{require(authorized_&&authorized_()&&store_.archiveSyncState(context_)==saved_);}
public:
    GroupHistoryReader(Storage& store,Authorization& policy,session::ReliableAuthorization scope,GroupHistoryAccess& access)
        :GroupHistoryReader(store,scope,access,[&policy,scope]{return currentSessionAuthorization(policy,scope);}){}
    GroupHistoryReader(Storage& store,session::ReliableAuthorization scope,GroupHistoryAccess& access,std::function<bool()> authorized)
        :store_(store),scope_(std::move(scope)),access_(access),authorized_(std::move(authorized)){
        require(scope_.valid()&&authorized_&&authorized_());
        auto w=groupHistoryHeader(4);w.number(scope_.archive.account);w.string(scope_.archive.issuer);w.string(scope_.archive.source);w.string(scope_.archive.reader);
        if(scope_.scope==session::ReliableAuthorizationScope::GroupControl){w.string(scope_.groupId);w.string(scope_.expectedRemoteDeviceId);}
        static const char hex[]="0123456789abcdef";for(auto b:digestDevicePayload(w.data)){context_+=hex[b>>4];context_+=hex[b&15];}
        saved_=store_.archiveSyncState(context_);if(saved_.empty()){position_.id=Storage::newMessageId();persist();}
        else{protocol::Reader r(saved_);require(r.type()==protocol::ArchiveControl&&r.string()==GroupHistoryDomain&&r.number()==1&&r.number()==3);position_=groupRequestFields(r);resume_=r.number();processed_=r.number();deferred_=r.number();auto complete=r.number();require(complete<=1&&resume_<=position_.after&&(!position_.through||position_.after<=position_.through));complete_=complete!=0;r.end();}
    }
    void rescan(){current();require(complete_&&!pending_);position_.after=resume_;position_.through=0;complete_=false;processed_=deferred_=0;persist();}
    Blob request(){current();require(!pending_);if(complete_)return {};position_.id=Storage::newMessageId();persist();pending_=true;return encodeGroupHistoryRequest(position_);}
    std::size_t accept(const Blob& bytes,const std::function<void()>& committed){
        current();require(pending_);auto page=decodeGroupHistoryPage(bytes);const auto& q=page.request;
        require(q.id==position_.id&&q.after==position_.after&&(position_.store.empty()||q.store==position_.store)&&(!position_.through||q.through==position_.through));
        struct CheckedPage {std::vector<GroupHistoryAccess::Checked> rows;~CheckedPage(){for(auto& row:rows)platform::group_crypto::wipe(row.plaintext.data(),row.plaintext.size());}} checked;
        for(const auto& item:page.items){current();checked.rows.push_back(item.state?GroupHistoryAccess::Checked{item.state,{}, {}}:access_.check(item.wire,scope_));}
        size_t inserted=0;auto deferred=deferred_;auto resume=resume_;bool blocked=deferred_!=0;
        for(size_t n=0;n<page.items.size();++n){current();auto& row=checked.rows[n];const auto& item=page.items[n];
            if(row.state==1){require(deferred<INT64_MAX);++deferred;if(!blocked){resume=item.cursor-1;blocked=true;}}
            else if(row.state==0){if(access_.commit(item.wire,row)){++inserted;if(committed)committed();}require(processed_<INT64_MAX);++processed_;}}
        current();position_=q;position_.after=page.next;complete_=page.complete;deferred_=deferred;resume_=blocked?resume:page.next;persist();pending_=false;return inserted;
    }
    void disconnected(){pending_=false;}
    bool complete()const{return complete_;}bool pending()const{return pending_;}
    std::uint64_t processed()const{return processed_;}std::uint64_t deferred()const{return deferred_;}
};
// Bounded execution of the SAME history reader on an existing authenticated
// group-control route. Purpose/group/device generation remain the route owner's
// responsibility. This object owns no thread, socket, key or reconnect policy.
class GroupHistoryJob {
    Storage& store_;session::ReliableAuthorization scope_;GroupHistoryAccess access_;std::function<bool()> authorized_;
    std::unique_ptr<GroupHistoryReader> reader_;std::uint64_t pendingAt_=0,nextAt_=0;
    std::string servedId_,servedStore_;std::uint64_t servedThrough_=0;bool providedComplete_=false;
public:
    GroupHistoryJob(Storage& store,const Profile& local,std::uint64_t account,session::ReliableAuthorization scope,
        GroupHistoryContextProvider context,std::function<bool()> authorized,std::function<void(const std::string&)> imported={})
        :store_(store),scope_(std::move(scope)),access_(store,local,account,std::move(context),std::move(imported)),authorized_(std::move(authorized)){
        require(scope_.scope==session::ReliableAuthorizationScope::GroupControl&&scope_.valid());
    }
    Blob request(std::uint64_t now){
        if(reader_&&reader_->complete()&&!providedComplete_)return {};
        if(reader_&&reader_->pending()){
            if(now>=pendingAt_&&now-pendingAt_>=15000){reader_->disconnected();nextAt_=now+1000;}
            return {};
        }
        if(now<nextAt_||!authorized_||!authorized_())return {};
        if(!reader_)reader_=std::make_unique<GroupHistoryReader>(store_,scope_,access_,authorized_);
        if(reader_->complete()){reader_->rescan();}
        auto result=reader_->request();pendingAt_=now;return result;
    }
    Blob control(const Blob& bytes,std::uint64_t now){
        require(authorized_&&authorized_());protocol::Reader r(bytes);require(r.type()==protocol::ArchiveControl&&r.string()==GroupHistoryDomain&&r.number()==1);auto kind=r.number();
        if(kind==1){auto reply=serveGroupHistory(store_,scope_,bytes,access_,authorized_);protocol::Reader header(reply);header.string();header.number();header.number();auto q=groupRequestFields(header);header.number();if(header.number()==1){servedId_=q.id;servedStore_=q.store;servedThrough_=q.through;providedComplete_=false;}return reply;}
        if(kind==5){require(bytes.size()<=512&&r.string()==servedId_&&r.string()==servedStore_&&r.number()==servedThrough_&&!servedId_.empty());r.end();providedComplete_=true;return {};}
        require(kind==2&&reader_&&reader_->pending());reader_->accept(bytes,{});pendingAt_=0;
        if(reader_->complete())nextAt_=now+60000;
        if(reader_->complete()){
            protocol::Reader header(bytes);header.string();header.number();header.number();auto q=groupRequestFields(header);
            auto ack=groupHistoryHeader(5);ack.string(q.id);ack.string(q.store);ack.number(q.through);return ack.data;
        }
        return {};
    }
    bool settled()const{return reader_&&reader_->complete()&&!reader_->pending()&&providedComplete_;}
    std::uint64_t processed()const{return reader_?reader_->processed():0;}
    std::uint64_t deferred()const{return reader_?reader_->deferred():0;}
};
}
