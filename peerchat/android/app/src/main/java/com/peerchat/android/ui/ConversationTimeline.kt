package com.peerchat.android.ui

import com.peerchat.android.application.AttachmentSnapshot
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

internal sealed class ConversationTimelineEntry {
    abstract val key:String
    abstract val date:String
    data class Text(val message:MessageUi):ConversationTimelineEntry(){
        override val key get()="message-${message.id}"
        override val date get()=message.date
    }
    data class File(val attachment:AttachmentSnapshot,override val date:String):ConversationTimelineEntry(){
        override val key get()="attachment-${attachment.id}"
    }
}

/** Text retains native logical order. Transfers have no logical-order field in
 * the existing protocol/journal; insert their durable local creation timestamp
 * into that order, matching Windows. Never rewrite text order using wall clocks
 * or invent an attachment Lamport clock. Completion/progress cannot move a card. */
internal fun conversationTimeline(c:ConversationUi):List<ConversationTimelineEntry> {
    val rows=c.messages.map{ConversationTimelineEntry.Text(it) as ConversationTimelineEntry}.toMutableList()
    val date=SimpleDateFormat("yyyy-MM-dd",Locale.getDefault())
    c.attachments.sortedWith(compareBy<AttachmentSnapshot>{it.created}.thenBy{it.id}).forEach{file->
        val position=rows.indexOfFirst{it is ConversationTimelineEntry.Text&&it.message.timestamp>file.created}
        val row=ConversationTimelineEntry.File(file,date.format(Date(file.created)))
        if(position<0)rows.add(row) else rows.add(position,row)
    }
    return rows
}
