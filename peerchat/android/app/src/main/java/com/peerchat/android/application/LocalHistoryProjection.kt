package com.peerchat.android.application

/** Limits expensive whole-conversation reads while letting native events ask
 * for an immediate projection. Native remains the source of every row. */
internal class OwnerProjectionCadence {
    private var nextAt=0L
    fun due(now:Long)=now>=nextAt
    fun dirty(){nextAt=0L}
    fun remainingDirty(now:Long){nextAt=minOf(nextAt,now+1000)}
    fun published(now:Long,activePeerWork:Boolean,activeAttachmentWork:Boolean=false){nextAt=now+if(activePeerWork||activeAttachmentWork)1000 else 10000}
}

/** Bounded, disposable display cache, never a message/outbox or authorization
 * owner. Native remains the only source of rows and delivery state. */
internal class LocalHistoryProjection {
    private data class Page(val devices:List<String>,val rows:List<MessageSnapshot>,val at:Long)
    private val pages=linkedMapOf<Long,Page>()
    data class Ticket(val epoch:Long,val revision:Long)
    private var epoch=0L
    private var serial=0L
    private val revisions=mutableMapOf<Long,Long>()
    private fun changed(id:Long){revisions[id]=++serial}
    @Synchronized fun ticket(friend:FriendIdentity):Ticket=Ticket(epoch,revisions.getOrPut(friend.accountId){0})
    @Synchronized fun putIfCurrent(friend:FriendIdentity,rows:List<MessageSnapshot>,now:Long,ticket:Ticket):Boolean {
        if(ticket!=Ticket(epoch,revisions[friend.accountId]?:0))return false
        put(friend,rows,now);return true
    }
    // A live durable row is visible even while the initial historical page loads.
    // This remains dirty, and never pretends to be a complete historical page.
    @Synchronized fun observeNativeMessage(friend:FriendIdentity,row:MessageSnapshot,now:Long) {
        if(row.senderDevice !in friend.knownPeerIds&&row.recipientDevice !in friend.knownPeerIds)return
        val current=rows(friend)
        if(friend.accountId !in pages&&pages.size>=128)pages.remove(pages.keys.first())
        pages[friend.accountId]=Page(friend.knownPeerIds.toList(),chronologicalMessages(current.filter{it.id!=row.id}+row).takeLast(8),now)
        dirty.add(friend.accountId);urgent.add(friend.accountId);changed(friend.accountId)
    }
    private val dirty=mutableSetOf<Long>()
    private val urgent=linkedSetOf<Long>()
    @Synchronized fun clear(){epoch++;pages.clear();dirty.clear();urgent.clear();revisions.clear()}
    @Synchronized fun discardLocal(friend:FriendIdentity,ids:Set<String>?) {
        pages[friend.accountId]?.let{page->pages[friend.accountId]=page.copy(rows=if(ids==null)emptyList()else page.rows.filter{it.id !in ids})}
        dirty.add(friend.accountId);urgent.add(friend.accountId);changed(friend.accountId)
    }
    @Synchronized fun invalidate(){val ids=(pages.keys+revisions.keys).toSet();dirty.addAll(ids);ids.forEach{changed(it)}}
    @Synchronized fun invalidate(friend:FriendIdentity){dirty.add(friend.accountId);changed(friend.accountId)}
    @Synchronized fun invalidateUrgent(friend:FriendIdentity){dirty.add(friend.accountId);urgent.add(friend.accountId);changed(friend.accountId)}
    @Synchronized fun invalidateMessages(ids:Set<String>):Boolean {
        val accounts=pages.filterValues{page->page.rows.any{it.id in ids}}.keys
        accounts.forEach{dirty.add(it);urgent.add(it);changed(it)}
        return accounts.isNotEmpty()
    }
    @Synchronized fun hasPending(friends:List<FriendIdentity>):Boolean=friends.any{
        val page=pages[it.accountId];page==null||page.devices!=it.knownPeerIds||it.accountId in dirty
    }
    @Synchronized fun rows(friend:FriendIdentity):List<MessageSnapshot> = pages[friend.accountId]?.takeIf{it.devices==friend.knownPeerIds}?.rows ?: emptyList()
    @Synchronized fun put(friend:FriendIdentity,rows:List<MessageSnapshot>,now:Long){
        require(rows.size<=8)
        if(friend.accountId !in pages&&pages.size>=128)pages.remove(pages.keys.first())
        pages[friend.accountId]=Page(friend.knownPeerIds.toList(),rows.toList(),now);dirty.remove(friend.accountId);urgent.remove(friend.accountId);changed(friend.accountId)
    }
    /** Merge only an exact native durable row into an already complete page.
     * Dirty/missing/device-changed pages still require the normal bounded read. */
    @Synchronized fun applyNativeMessage(friend:FriendIdentity,row:MessageSnapshot,now:Long):Boolean {
        val page=pages[friend.accountId]?:return false
        if(page.devices!=friend.knownPeerIds||friend.accountId in dirty||
            row.senderDevice !in friend.knownPeerIds&&row.recipientDevice !in friend.knownPeerIds)return false
        val rows=chronologicalMessages(page.rows.filter{it.id!=row.id}+row).takeLast(8)
        // Incremental events must not postpone the periodic full reconciliation.
        pages[friend.accountId]=Page(page.devices,rows,minOf(page.at,now));changed(friend.accountId)
        return true
    }
    @Synchronized fun next(friends:List<FriendIdentity>,now:Long):FriendIdentity? {
        val active=friends.map{it.accountId}.toSet();pages.keys.retainAll(active);dirty.retainAll(active);urgent.retainAll(active);revisions.keys.retainAll(active)
        return friends.firstOrNull{it.accountId in urgent} ?: friends.firstOrNull{val page=pages[it.accountId];page==null||page.devices!=it.knownPeerIds||it.accountId in dirty}
            ?: friends.filter{now-(pages[it.accountId]?.at?:now)>=60_000}.minByOrNull{pages[it.accountId]?.at?:0}
    }
    fun refreshOne(friends:List<FriendIdentity>,now:Long,read:(FriendIdentity)->List<MessageSnapshot>) {
        val next=next(friends,now)?:return
        put(next,read(next),now)
    }
}
