package com.peerchat.android.ui

/** Provisional UI only, not a queue, message identity or delivery authority.
 * A native ID is bound only after the existing durable command succeeds. */
internal data class LocalSendPresentation(val token:String,val conversation:String,val text:String,
    val created:Long,val baseline:Set<String>,val nativeId:String?=null,val failed:Boolean=false)

internal fun presentLocalSends(conversations:List<ConversationUi>,pending:List<LocalSendPresentation>,
    sender:String,name:String):List<ConversationUi> = conversations.map{conversation->
    val local=pending.filter{it.conversation==conversation.id}
    val saving=local.firstOrNull{it.nativeId==null&&!it.failed}
    // A native snapshot can race the suspend command's return to the UI. Defer
    // newly published local rows for that one pending command, never match text
    // heuristically or duplicate the row before its exact native ID is known.
    val temporary=local.map{it.token}.toSet()
    val confirmed=local.mapNotNull{it.nativeId}.toSet()
    val visible=conversation.messages.filter{it.id !in temporary&&(saving==null||!it.outgoing||it.id in saving.baseline||it.id in confirmed)}
    val ids=conversation.messages.map{it.id}.toSet()+conversation.attachments.map{it.id}
    val added=local.filter{it.nativeId==null||it.nativeId !in ids}.map{row->
        MessageUi(row.nativeId?:row.token,sender,name,row.text,true,
            java.text.SimpleDateFormat("HH:mm",java.util.Locale.getDefault()).format(java.util.Date(row.created)),
            java.text.SimpleDateFormat("yyyy-MM-dd",java.util.Locale.getDefault()).format(java.util.Date(row.created)),
            if(row.failed)MessageStatus.Failed else if(row.nativeId==null)MessageStatus.Saving else MessageStatus.Queued,
            row.created,deliveryDetail=if(row.failed)"Local save not confirmed. Review the retained draft before sending again." else "")
    }
    conversation.copy(messages=(visible+added).sortedWith(compareBy<MessageUi>{it.timestamp}.thenBy{it.id}))
}

internal fun finishLocalStartup(state:PeerChatUiState,available:Boolean):PeerChatUiState =
    if(!available)state else state.copy(startupStatus=null,
        route=if(state.route in setOf(Route.Welcome,Route.Login,Route.Register))Route.Home else state.route)
