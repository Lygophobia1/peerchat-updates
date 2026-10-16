package com.peerchat.android.application

/** Bounded, disposable display cache, never a message/outbox or authorization
 * owner. Native remains the only source of rows and delivery state. */
internal class LocalHistoryProjection {
    private data class Page(val devices:List<String>,val rows:List<MessageSnapshot>,val at:Long)
    private val pages=linkedMapOf<Long,Page>()
    private val dirty=mutableSetOf<Long>()
    @Synchronized fun clear(){pages.clear();dirty.clear()}
    @Synchronized fun invalidate(){dirty.addAll(pages.keys)}
    @Synchronized fun invalidate(friend:FriendIdentity){dirty.add(friend.accountId)}
    @Synchronized fun rows(friend:FriendIdentity):List<MessageSnapshot> = pages[friend.accountId]?.takeIf{it.devices==friend.knownPeerIds}?.rows ?: emptyList()
    @Synchronized fun put(friend:FriendIdentity,rows:List<MessageSnapshot>,now:Long){
        require(rows.size<=8)
        if(friend.accountId !in pages&&pages.size>=128)pages.remove(pages.keys.first())
        pages[friend.accountId]=Page(friend.knownPeerIds.toList(),rows.toList(),now);dirty.remove(friend.accountId)
    }
    fun refreshOne(friends:List<FriendIdentity>,now:Long,read:(FriendIdentity)->List<MessageSnapshot>) {
        val next=synchronized(this){
            val active=friends.map{it.accountId}.toSet();pages.keys.retainAll(active);dirty.retainAll(active)
            friends.firstOrNull{val page=pages[it.accountId];page==null||page.devices!=it.knownPeerIds||it.accountId in dirty}
                ?: friends.filter{now-(pages[it.accountId]?.at?:now)>=10_000}.minByOrNull{pages[it.accountId]?.at?:0}
        } ?: return
        put(next,read(next),now)
    }
}
