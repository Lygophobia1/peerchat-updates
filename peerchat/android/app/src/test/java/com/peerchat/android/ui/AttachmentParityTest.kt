package com.peerchat.android.ui

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.peerchat.android.application.*
import com.peerchat.android.platform.attachmentName
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class AttachmentParityTest {
    @get:Rule val compose=createComposeRule()
    private val peer="b".repeat(64)
    private fun row(state:Int=2,path:String="")=AttachmentSnapshot("c".repeat(32),peer,"résumé-日本.png",100000,state,false,0,1,"",path)
    @Test fun receiveRequiresExplicitAcceptanceAndNativeCompletion(){
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){AttachmentCard(row(),actions::add)}}
        compose.runOnIdle{assertTrue(actions.isEmpty())}
        compose.onNodeWithText("Accept").performClick()
        compose.runOnIdle{assertEquals(listOf(UiAction.AttachmentAction(row().id,true)),actions)}
        compose.onNodeWithText("Open picture").assertDoesNotExist()
        compose.onNodeWithText("Save as").assertDoesNotExist()
        compose.onNodeWithText("Decline").performClick()
        compose.runOnIdle{assertEquals(UiAction.AttachmentAction(row().id,false),actions.last())}
    }
    @Test fun sourceNamesArePortableAndBounded(){
        assertEquals("_CON.txt",attachmentName("CON.txt"))
        assertEquals("résumé-日本.png",attachmentName("../résumé-日本.png"))
        assertEquals("a_b.txt",attachmentName("a:b.txt"))
        assertTrue(attachmentName("😀".repeat(300)).toByteArray().size<=220)
        assertFalse(attachmentName("😀".repeat(300)).endsWith("\uD83D"))
    }
    @Test fun nativeSnapshotsRejectBadLimitsAndKeepRestartState(){
        fun record(state:Long=4,size:Long=100000)=NativeRecordWriter(30).also{w->
            w.number(1);w.text(row().id);w.text(peer);w.text(row().name);w.number(size);w.number(state);w.number(0);w.number(0);w.number(1);w.text("");w.text("/private/attachments/copy.png")
        }.bytes()
        val a=attachmentSnapshots(record()).single()
        assertTrue(a.complete);assertEquals(row().name,a.name);assertFalse(a.outgoing)
        assertEquals(a,attachmentSnapshots(record()).single())
        for(invalid in listOf(record(state=3),record(size=256L*1024*1024+1))){
            try{attachmentSnapshots(invalid);fail("Invalid native attachment accepted")}catch(_:IllegalArgumentException){}
        }
    }
    @Test fun projectionUsesAccountBindingsAndKeepsAttachmentTruthOffline(){
        val attachment=row(4,"/private/attachments/copy.png")
        val snapshot=MessagingSnapshot(1,"Test","a".repeat(64),false,"Offline",
            listOf(ConversationSnapshot(FriendIdentity(2,"Other",peer,false),ConnectionSnapshot(),emptyList(),listOf(attachment))),localHistoryReady=true)
        val ui=projectMessaging(snapshot,PeerChatUiState())
        assertEquals(listOf(attachment),ui.conversations.single().attachments)
        assertEquals(ConnectionState.Offline,ui.connection)
        assertFalse(ui.diagnostics.contains(attachment.name))
    }
}
