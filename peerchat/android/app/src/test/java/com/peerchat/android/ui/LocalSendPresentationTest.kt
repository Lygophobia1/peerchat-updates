package com.peerchat.android.ui

import org.junit.Assert.*
import org.junit.Test

class LocalSendPresentationTest {
    private val chat=ConversationUi("account:8","contact",ChatKind.Direct,false,"","")
    private val pending=LocalSendPresentation("local-saving-test",chat.id,"fixture",2,emptySet())
    @Test fun provisionalIsNotQueuedAndReapplicationDoesNotDuplicate(){
        val first=presentLocalSends(listOf(chat),listOf(pending),"local","me")
        assertEquals(MessageStatus.Saving,first.single().messages.single().status)
        assertEquals(1,presentLocalSends(first,listOf(pending),"local","me").single().messages.size)
        val queued=presentLocalSends(listOf(chat),listOf(pending.copy(nativeId="c".repeat(32))),"local","me")
        assertEquals("c".repeat(32),queued.single().messages.single().id)
        assertEquals(MessageStatus.Queued,queued.single().messages.single().status)
    }
    @Test fun snapshotRaceAndFailureKeepOneHonestBubble(){
        val actual=MessageUi("c".repeat(32),"local","me","fixture",true,"",timestamp=2)
        val incoming=actual.copy(id="d".repeat(32),outgoing=false)
        val raced=chat.copy(messages=listOf(actual,incoming))
        val saving=presentLocalSends(listOf(raced),listOf(pending),"local","me").single().messages
        assertEquals(2,saving.size);assertFalse(saving.any{it.id==actual.id});assertTrue(saving.any{it.id==incoming.id})
        assertEquals(2,presentLocalSends(listOf(raced),listOf(pending.copy(nativeId=actual.id)),"local","me").single().messages.size)
        assertEquals(MessageStatus.Failed,presentLocalSends(listOf(chat),listOf(pending.copy(failed=true)),"local","me").single().messages.single().status)
    }
    @Test fun startupCompletionPreservesConversationAndDetails(){
        listOf(Route.Conversation,Route.Details,Route.Diagnostics).forEach{route->
            assertEquals(route,finishLocalStartup(PeerChatUiState(route=route),true).route)
        }
        assertEquals(Route.Home,finishLocalStartup(PeerChatUiState(route=Route.Login),true).route)
    }
}
