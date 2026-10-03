package com.peerchat.android.application

data class FriendIdentity(val accountId: Long,val username: String,val peerId: String,val online: Boolean,
    val knownPeerIds: List<String> = listOf(peerId))
data class MessageSnapshot(val id: String,val senderDevice: String,val recipientDevice: String,
    val timestamp: Long,val logicalOrder: Long,val sequence: Long,val delivery: Long,val text: String)
data class ConnectionSnapshot(val ready: Boolean=false,val pending: Boolean=false,val retrying: Boolean=false,
    val retryDelayMs: Long=0,val networkRevision: Long=0,val failure: String="none")
data class ConversationSnapshot(val friend: FriendIdentity,val connection: ConnectionSnapshot,
    val messages: List<MessageSnapshot>)
data class MessagingSnapshot(val accountId: Long=0,val username: String="",val localPeerId: String="",
    val authenticated: Boolean=false,val status: String="Not signed in",val conversations: List<ConversationSnapshot> = emptyList(),
    val incomingRequests:List<AccountRequestSnapshot> = emptyList(),
    // Authentication alone is not evidence of a currently refreshed device lease.
    val serverPresenceLive:Boolean=false)

internal fun expiredServerSession(failure:Exception):Boolean = failure is com.peerchat.android.platform.ServerRejected &&
    failure.code in setOf("session_invalid","session_superseded","event_session_superseded")

// Safe classification only: never expose exception messages, request bodies,
// session tokens, credentials, or TLS provider buffers to the UI/diagnostics.
internal fun serverRefreshFailure(failure:Exception):String = when(failure) {
    is com.peerchat.android.platform.ServerRejected -> "Server rejected presence/session refresh (${failure.code.take(80).filter {it.isLetterOrDigit()||it=='_'}})"
    is java.io.IOException -> "Server presence/session refresh unavailable (network/TLS I/O)"
    else -> "Server presence/session refresh validation failed"
}
data class AccountRequestSnapshot(val accountId:Long,val username:String)

internal fun connectionSnapshot(bytes: ByteArray): ConnectionSnapshot {
    val record=NativeRecord(bytes,4)
    fun flag(): Boolean=record.number().also {require(it in 0..1)}==1L
    val result=ConnectionSnapshot(flag(),flag(),flag(),record.number(),record.number(),record.text().take(300))
    record.end();return result
}
internal fun messageSnapshots(bytes: ByteArray): List<MessageSnapshot> {
    val record=NativeRecord(bytes,2);val count=record.number();require(count<=8)
    val result=List(count.toInt()) {
        val id=record.text();val sender=record.text();val recipient=record.text()
        require(id.matches(Regex("[0-9a-f]{32}"))&&sender.matches(Regex("[0-9a-f]{64}"))&&recipient.matches(Regex("[0-9a-f]{64}")))
        val timestamp=record.number();val order=record.number();val sequence=record.number();val delivery=record.number()
        require(sequence>0&&delivery in 0..2)
        MessageSnapshot(id,sender,recipient,timestamp,order,sequence,delivery,record.text())
    };record.end();return result
}
// Public display metadata/history-of-device-bindings only. Server friendship is
// still authoritative; the native trust guard decides whether a key may be pinned.
internal fun reconcileFriends(previous: List<FriendIdentity>,current: List<FriendIdentity>): List<FriendIdentity> {
    require(previous.size<=1024&&current.size<=1024&&current.map {it.accountId}.distinct().size==current.size&&
        current.map {it.peerId}.distinct().size==current.size)
    val old=previous.associateBy {it.accountId}
    return current.map {friend ->
        require(friend.accountId>0&&friend.username.length<=64&&friend.peerId.matches(Regex("[0-9a-f]{64}")))
        val ids=((old[friend.accountId]?.knownPeerIds ?: emptyList())+friend.peerId).distinct()
        require(ids.size<=32&&ids.all {it.matches(Regex("[0-9a-f]{64}"))})
        friend.copy(knownPeerIds=ids.toList())
    }
}
