package com.peerchat.android.application

/** Limits expensive whole-conversation reads while letting native events ask
 * for an immediate projection. Native remains the source of every row. */
internal class OwnerProjectionCadence {
    private var nextAt=0L
    fun due(now:Long)=now>=nextAt
    fun dirty(){nextAt=0L}
    fun remainingDirty(now:Long){nextAt=minOf(nextAt,now+1000)}
    fun published(now:Long,activePeerWork:Boolean){nextAt=now+if(activePeerWork)1000 else 10000}
}

/** Bounded, disposable display cache, never a message/outbox or authorization
 * owner. Native remains the only source of rows and delivery state. */
internal class LocalHistoryProjection {
    private data class Page(val devices:List<String>,val rows:List<MessageSnapshot>,val at:Long)
    private val pages=linkedMapOf<Long,Page>()
    private val dirty=mutableSetOf<Long>()
    private val urgent=linkedSetOf<Long>()
    @Synchronized fun clear(){pages.clear();dirty.clear();urgent.clear()}
    @Synchronized fun invalidate(){dirty.addAll(pages.keys)}
    @Synchronized fun invalidate(friend:FriendIdentity){dirty.add(friend.accountId)}
    @Synchronized fun invalidateUrgent(friend:FriendIdentity){dirty.add(friend.accountId);urgent.add(friend.accountId)}
    @Synchronized fun invalidateMessages(ids:Set<String>):Boolean {
        val accounts=pages.filterValues{page->page.rows.any{it.id in ids}}.keys
        accounts.forEach{dirty.add(it);urgent.add(it)}
        return accounts.isNotEmpty()
    }
    @Synchronized fun hasPending(friends:List<FriendIdentity>):Boolean=friends.any{
        val page=pages[it.accountId];page==null||page.devices!=it.knownPeerIds||it.accountId in dirty
    }
    @Synchronized fun rows(friend:FriendIdentity):List<MessageSnapshot> = pages[friend.accountId]?.takeIf{it.devices==friend.knownPeerIds}?.rows ?: emptyList()
    @Synchronized fun put(friend:FriendIdentity,rows:List<MessageSnapshot>,now:Long){
        require(rows.size<=8)
        if(friend.accountId !in pages&&pages.size>=128)pages.remove(pages.keys.first())
        pages[friend.accountId]=Page(friend.knownPeerIds.toList(),rows.toList(),now);dirty.remove(friend.accountId);urgent.remove(friend.accountId)
    }
    fun refreshOne(friends:List<FriendIdentity>,now:Long,read:(FriendIdentity)->List<MessageSnapshot>) {
        val next=synchronized(this){
            val active=friends.map{it.accountId}.toSet();pages.keys.retainAll(active);dirty.retainAll(active);urgent.retainAll(active)
            friends.firstOrNull{it.accountId in urgent} ?: friends.firstOrNull{val page=pages[it.accountId];page==null||page.devices!=it.knownPeerIds||it.accountId in dirty}
                ?: friends.filter{now-(pages[it.accountId]?.at?:now)>=10_000}.minByOrNull{pages[it.accountId]?.at?:0}
        } ?: return
        put(next,read(next),now)
    }
}
