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
    @Test fun liveDmCallButtonShowsActualUnavailableReason(){
        val actions=mutableListOf<UiAction>()
        val direct=ConversationUi("account:8","Other",ChatKind.Direct,true,"","",hasOlder=false)
        val reason="Android voice receive component unavailable"
        val state=PeerChatUiState(demo=false,route=Route.Conversation,selectedId=direct.id,conversations=listOf(direct),account=AccountUi("account:19","Test"),voiceDiagnostic=reason)
        compose.setContent{PeerChatTheme(true){ConversationScreen(direct,state,actions::add)}}
        compose.onNodeWithContentDescription("Voice call").performClick()
        compose.runOnIdle{assertEquals(listOf(UiAction.Notice(reason)),actions)}
    }
    @Test fun groupComposerDoesNotAdvertiseAttachmentsAndRejectsOversizedText(){
        val actions=mutableListOf<UiAction>()
        val group=ConversationUi("group:"+"c".repeat(32),"Group",ChatKind.Group,true,"","",hasOlder=false)
        val state=PeerChatUiState(demo=false,route=Route.Conversation,selectedId=group.id,conversations=listOf(group),account=AccountUi("account:19","Test"))
        compose.setContent{PeerChatTheme(true){ConversationScreen(group,state,actions::add)}}
        compose.onNodeWithContentDescription("Send picture or file").assertDoesNotExist()
        compose.onNodeWithTag("composer").performTextInput("x".repeat(4097))
        compose.runOnIdle{assertTrue(actions.none{it is UiAction.Draft})}
    }
    @Test fun statisticsProjectConfirmedBytesWithoutInventingRates(){
        val current=row(3).copy(done=50000,statisticsKnown=true,statisticsBytes=50000,durationMs=2000,rateBits=80000,etaReliable=true)
        assertTrue(current.statisticsText.contains("50.00%"));assertTrue(current.statisticsText.contains("Current 0.08 Mbps"));assertTrue(current.statisticsText.contains("Average 0.20 Mbps"));assertTrue(current.statisticsText.contains("ETA 5 s"))
        assertFalse(current.copy(rateBits=0,etaReliable=false).statisticsText.contains("ETA"))
        assertTrue(current.copy(state=7).statisticsText.contains("Current 0.00 Mbps"))
        val complete=current.copy(state=4,done=100000,statisticsBytes=100000)
        assertTrue(complete.statisticsText.contains("Average 0.40 Mbps"));assertFalse(complete.statisticsText.contains("Current"))
        assertTrue(row(4).statisticsText.contains("unavailable"))
    }
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
