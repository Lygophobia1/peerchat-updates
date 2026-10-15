package com.peerchat.android.application

// Stage only; never copies server payloads/credentials into the presentation error.
class MessagingStartupFailure(cause:Exception):Exception("Messaging startup failed after authentication",cause)

data class FriendIdentity(val accountId: Long,val username: String,val peerId: String,val online: Boolean,
    val knownPeerIds: List<String> = listOf(peerId))
data class MessageSnapshot(val id: String,val senderDevice: String,val recipientDevice: String,
    val timestamp: Long,val logicalOrder: Long,val sequence: Long,val delivery: Long,val text: String)
data class ConnectionSnapshot(val ready: Boolean=false,val pending: Boolean=false,val retrying: Boolean=false,
    val retryDelayMs: Long=0,val networkRevision: Long=0,val failure: String="none")
data class ConversationSnapshot(val friend: FriendIdentity,val connection: ConnectionSnapshot,
    val messages: List<MessageSnapshot>,val attachments:List<AttachmentSnapshot> = emptyList())
data class MessagingSnapshot(val accountId: Long=0,val username: String="",val localPeerId: String="",
    val authenticated: Boolean=false,val status: String="Not signed in",val conversations: List<ConversationSnapshot> = emptyList(),
    val incomingRequests:List<AccountRequestSnapshot> = emptyList(),
    // Authentication alone is not evidence of a currently refreshed device lease.
    val serverPresenceLive:Boolean=false,val mailboxStorageAllowed:Boolean=false,
    val localHistoryReady:Boolean=false,val archiveProgress:ArchiveSyncProgress=ArchiveSyncProgress(),
    val archiveAuthorizationDiagnostics:String="No authenticated History Sync snapshot yet.",val transferDiagnostics:String="",
    val messagingDiagnostics:String="")

// Fixed native decision codes only: never format provider exception messages,
// server payloads, credentials or key material into the diagnostic projection.
internal fun initialFriendPinResult(result:ByteArray?):String {
    if(result==null||result.size!=2)return "decision_unavailable"
    val code=result[1].toInt()
    if(result[0].toInt()==1)return if(code==0)"accepted" else "decision_unavailable"
    if(result[0].toInt()!=0)return "decision_unavailable"
    return when(code){
        1->"stale_identity_or_attempt"
        2->"no_current_authenticated_friendship"
        3->"existing_verified_account_device_requires_explicit_verification"
        4->"stored_key_mismatch"
        else->"decision_unavailable"
    }
}

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

/** Network quiescence never destroys the local-history projection. */
internal fun localOfflineSnapshot(snapshot:MessagingSnapshot,status:String):MessagingSnapshot = snapshot.copy(
    authenticated=false,serverPresenceLive=false,incomingRequests=emptyList(),status=status,
    conversations=snapshot.conversations.map{it.copy(friend=it.friend.copy(online=false),connection=ConnectionSnapshot())})

/** Shared application projection. Reads native truth; no UI/presenter required,
 * no locally invented conversations, history, ACKs or delivery state. */
internal fun nativeConversationProjection(friends:List<FriendIdentity>,connection:(String)->ByteArray,
    history:(String)->ByteArray):List<ConversationSnapshot> = friends.map {friend ->
    ConversationSnapshot(friend,connectionSnapshot(connection(friend.peerId)),
        accountHistoryPage(friend,Long.MAX_VALUE){peer,_->history(peer)})
}

/** Native Storage.sequence is messages.rowid in ONE profile database, not a
 * per-device counter. Page by that durable cursor, then order for presentation.
 * No new identity authority: these IDs come only from the protected account index.
 * At most 32 native eight-row reads; return eight logical messages per page. */
internal fun accountHistoryPage(friend:FriendIdentity,before:Long,
    history:(String,Long)->ByteArray):List<MessageSnapshot> {
    val devices=friend.knownPeerIds.distinct().sorted()
    require(before>0&&devices.size in 1..32&&friend.peerId in devices&&
        devices.all{it.matches(Regex("[0-9a-f]{64}"))})
    val rows=devices.flatMap{peer->messageSnapshots(history(peer,before)).also{page->require(page.all{it.sequence<before})}}
    // Keep the newest native row for duplicate logical IDs. Delivery/identity
    // fields remain native values; never renumber or rewrite stored messages.
    return chronologicalMessages(rows.sortedByDescending{it.sequence}.distinctBy{it.id}.take(8))
}

internal fun chronologicalMessages(rows:List<MessageSnapshot>):List<MessageSnapshot> =
    rows.sortedWith(compareByDescending<MessageSnapshot>{it.sequence}
        .thenByDescending{when(it.delivery){2L->3;3L->2;1L->1;else->0}})
        .distinctBy{it.id}.sortedWith(compareBy<MessageSnapshot>{it.logicalOrder}
            .thenBy{it.senderDevice}.thenBy{it.id}.thenBy{it.recipientDevice})

internal fun nativeWakeOutputComplete(receivedNanos:Long,wakeStartedNanos:Long,
    attached:Boolean,finished:Boolean,outputIdleNanos:Long):Boolean =
    receivedNanos>=wakeStartedNanos&&attached&&!finished&&outputIdleNanos>receivedNanos

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
        require(sequence>0&&delivery in 0..3)
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
