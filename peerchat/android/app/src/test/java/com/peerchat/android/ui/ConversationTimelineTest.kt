package com.peerchat.android.ui

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.peerchat.android.application.*
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34],qualifiers="w600dp-h1200dp-port")
class ConversationTimelineTest {
    @get:Rule val compose=createComposeRule()
    private val local="a".repeat(64)
    private val peer="b".repeat(64)
    private fun text(id:String,time:Long,order:Long)=MessageUi(id,local,"Local",id,true,"00:00",
        date="1970-01-01",timestamp=time,logicalOrder=order)
    private fun file(id:String,time:Long)=AttachmentSnapshot(id,peer,"fixture-$id.png",1000,2,false,0,time,"","")
    private fun conversation(messages:List<MessageUi>,files:List<AttachmentSnapshot>)=
        ConversationUi("account:2","Peer",ChatKind.Direct,true,"","",messages=messages,attachments=files)
    @Test fun picturesAndFilesAreInterleavedAndCompletionDoesNotMoveThem(){
        val a=text("before",100,1);val b=text("after",300,2)
        val f=file("1".repeat(32),200);val g=file("2".repeat(32),400)
        val c=conversation(listOf(a,b),listOf(g,f))
        val keys=conversationTimeline(c).map{it.key}
        assertEquals(listOf("message-before","attachment-${f.id}","message-after","attachment-${g.id}"),keys)
        assertEquals(keys,conversationTimeline(c.copy(attachments=listOf(f.copy(state=4,done=1000),g))).map{it.key})
        assertEquals(keys,conversationTimeline(c.copy(messages=c.messages.map{it.copy()},attachments=c.attachments.map{it.copy()})).map{it.key})
        assertEquals(2,conversationTimeline(c).filterIsInstance<ConversationTimelineEntry.Text>().size)
    }
    @Test fun nativeProjectionUsesChronologyAndPreservesExactFields(){
        val sent=MessageSnapshot("1".repeat(32),local,peer,9000,50,9,0,"Sent fixture")
        val reply=MessageSnapshot("2".repeat(32),peer,local,1000,51,10,2,"Reply fixture")
        val snap=MessagingSnapshot(1,"Local",local,true,"Online",listOf(
            ConversationSnapshot(FriendIdentity(2,"Peer",peer,true),ConnectionSnapshot(),listOf(reply,sent))))
        val c=projectMessaging(snap,PeerChatUiState()).conversations.single()
        assertEquals(listOf(reply.id,sent.id),c.messages.map{it.id})
        assertEquals(listOf(51L,50L),c.messages.map{it.logicalOrder})
        assertEquals(listOf(1000L,9000L),c.messages.map{it.timestamp})
        assertEquals(sent.text,c.preview)
        assertEquals(listOf(reply.id,sent.id),conversationTimeline(c).filterIsInstance<ConversationTimelineEntry.Text>().map{it.message.id})
    }
    @Test fun lateEqualTimeMessagesAndFilesHaveStableOrderAcrossRefresh(){
        val a=text("a",200,99);val b=text("b",200,1);val early=text("late-arrival",100,100)
        val c=conversation(listOf(b,a,early),listOf(file("f",200)))
        val expected=listOf("message-late-arrival","message-a","message-b","attachment-f")
        assertEquals(expected,conversationTimeline(c).map{it.key})
        assertEquals(expected,conversationTimeline(c.copy(messages=listOf(a,early,b))).map{it.key})
    }
    @Test fun composeUsesOneTimelineAndRefreshesInPlace(){
        val f=file("3".repeat(32),200)
        val value=mutableStateOf(conversation(listOf(text("Before picture",100,1)),listOf(f)))
        compose.setContent{PeerChatTheme(true){ConversationScreen(value.value,PeerChatUiState(connection=ConnectionState.Online),{})}}
        compose.runOnIdle{value.value=value.value.copy(messages=value.value.messages+text("Reply after picture",300,2))}
        val first=compose.onNodeWithText("Before picture").fetchSemanticsNode().boundsInRoot
        val picture=compose.onNodeWithTag("attachment-footer-${f.id}").fetchSemanticsNode().boundsInRoot
        val reply=compose.onNodeWithText("Reply after picture").fetchSemanticsNode().boundsInRoot
        assertTrue(first.top<picture.top&&picture.top<reply.top)
        compose.runOnIdle{value.value=value.value.copy(attachments=listOf(f.copy(state=3,done=500)))}
        compose.onNodeWithText("Reply after picture").assertExists()
        assertTrue(compose.onNodeWithTag("attachment-footer-${f.id}").fetchSemanticsNode().boundsInRoot.top<compose.onNodeWithText("Reply after picture").fetchSemanticsNode().boundsInRoot.top)
    }
}
