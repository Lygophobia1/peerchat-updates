package com.peerchat.android.application

/** Short-lived wake hints, never friendship or device authority. The caller
 * owns the authenticated event lifetime and clears these on session replacement. */
internal class PendingFriendRelayDemands {
    private data class Hint(val account:Long,val peer:String,val expires:Long,var forwardedGeneration:Long=0)
    private val pending=linkedMapOf<String,Hint>()
    fun clear(){pending.clear()}
    fun offer(account:Long,peer:String,now:Long){
        pending.entries.removeAll{it.value.expires<=now}
        if(account<=0||!peer.matches(Regex("[0-9a-f]{64}")))return
        if(peer !in pending&&pending.size>=32)return
        pending[peer]=Hint(account,peer,now+15_000)
    }
    fun drain(friends:List<FriendIdentity>,generation:Long,now:Long,demand:(String)->Unit){
        val iterator=pending.iterator()
        while(iterator.hasNext()){
            val hint=iterator.next().value
            if(hint.expires<=now){iterator.remove();continue}
            if(generation>0&&hint.forwardedGeneration!=generation&&friends.any{it.accountId==hint.account&&it.peerId==hint.peer&&hint.peer !in it.revokedPeerIds}){
                demand(hint.peer);hint.forwardedGeneration=generation
            }
        }
    }
}
