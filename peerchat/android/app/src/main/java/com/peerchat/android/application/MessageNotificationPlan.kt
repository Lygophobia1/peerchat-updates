package com.peerchat.android.application

import com.peerchat.android.platform.*

data class LocalMessageNotification(val conversationId:String,val title:String,val body:String,val count:Int)
/** Only authenticated native history produces a notification. Wake hints have
 * no sender/content and cannot invoke this without an owner snapshot. */
fun planMessageNotification(snapshot:MessagingSnapshot,conversation:ConversationSnapshot,
    prior:ConversationPreference?,preview:NotificationPreview):LocalMessageNotification? {
    if(!snapshot.authenticated||prior==null)return null // Establish existing-history baseline first.
    val incoming=conversation.messages.filter{it.senderDevice!=snapshot.localPeerId&&it.sequence>maxOf(prior.readSequence,prior.notifiedSequence)}
    if(incoming.isEmpty())return null
    val title=if(preview==NotificationPreview.HideContent)"PeerChat" else conversation.friend.username
    val body=when(preview){
        NotificationPreview.SenderAndMessage->incoming.last().text.take(240)
        NotificationPreview.SenderOnly->if(incoming.size==1)"New message" else "${incoming.size} new messages"
        NotificationPreview.HideContent->"New PeerChat message"
    }
    return LocalMessageNotification("account:${conversation.friend.accountId}",title,body,incoming.size)
}
