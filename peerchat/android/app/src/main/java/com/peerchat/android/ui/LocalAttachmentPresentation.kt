package com.peerchat.android.ui

import android.net.Uri
import com.peerchat.android.application.AttachmentSnapshot
import com.peerchat.android.platform.AttachmentSendMode

/** Ephemeral display state. The URI stays in this process and never becomes a transfer record. */
internal data class LocalAttachmentPresentation(
    val token:String,val conversation:String,val uri:Uri,val created:Long,
    val baseline:Set<String>,val mode:AttachmentSendMode=AttachmentSendMode.Direct,val name:String="Attachment",val size:Long=0,
    val nativeId:String?=null,val phase:LocalAttachmentPhase=LocalAttachmentPhase.Preparing,val failureText:String=""
)

internal enum class LocalAttachmentPhase { Preparing, ChoosingMode, Failed, Cancelled }

internal fun presentLocalAttachments(
    conversations:List<ConversationUi>,pending:List<LocalAttachmentPresentation>
):List<ConversationUi> = conversations.map{conversation->
    val local=pending.filter{it.conversation==conversation.id}
    if(local.isEmpty())return@map conversation
    // Only exact native identity can reconcile a provisional row. Hiding all
    // new outgoing rows would erase unrelated concurrent attachments.
    val visible=conversation.attachments.filter{row->row.id !in local.map{it.token}.toSet()}
    val ids=visible.map{it.id}.toSet()
    val added=local.filter{it.nativeId==null||it.nativeId !in ids}.map{row->
        AttachmentSnapshot(row.nativeId?:row.token,"",row.name,row.size,
            when(row.phase){LocalAttachmentPhase.Failed->6;LocalAttachmentPhase.Cancelled->5;else->0},
            true,0,row.created,
            when(row.phase){
                LocalAttachmentPhase.Failed->row.failureText.ifEmpty{"Local preparation was not confirmed. Check history before retrying."}
                LocalAttachmentPhase.Cancelled->"Preparation was cancelled. Check history before retrying."
                else->""
            },"",cachePhase=when(row.phase){
                LocalAttachmentPhase.Preparing->"Preparing encrypted file"
                LocalAttachmentPhase.ChoosingMode->"Choose a delivery mode"
                else->""
            })
    }
    conversation.copy(attachments=(visible+added).sortedWith(compareBy<AttachmentSnapshot>{it.created}.thenBy{it.id}))
}
