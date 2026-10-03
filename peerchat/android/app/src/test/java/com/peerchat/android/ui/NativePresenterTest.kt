package com.peerchat.android.ui

import android.app.Application
import androidx.lifecycle.ViewModelStore
import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.application.*
import com.peerchat.android.platform.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.*
import org.junit.*
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class NativePresenterTest {
    private val dispatcher=StandardTestDispatcher()
    private val store=ViewModelStore()
    private class Commands:MessagingApplication {
        override val state=MutableStateFlow(MessagingSnapshot())
        var reject=true;var sends=0;var failSend=false;var closed=false
        override suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String):AccountSession {
            if(reject)throw ServerRejected("bad_credentials")
            state.value=MessagingSnapshot(19,"Jonah","a".repeat(64),true,"Authenticated",listOf(
                ConversationSnapshot(FriendIdentity(8,"Sammy","b".repeat(64),true),ConnectionSnapshot(ready=true),emptyList())))
            return AccountSession(19,"Jonah","test-only-token",true)
        }
        override suspend fun send(peerId:String,text:String):String {sends++;if(failSend)error("test failure");return "c".repeat(32)}
        override suspend fun older(peerId:String,before:Long)=emptyList<MessageSnapshot>()
        override suspend fun requestFriend(username:String) {}
        override suspend fun respondFriend(accountId:Long,accept:Boolean) {}
        override suspend fun signOut(){state.value=MessagingSnapshot()}
        override fun close(){closed=true}
    }
    @Before fun start(){Dispatchers.setMain(dispatcher)}
    @After fun stop(){store.clear();Dispatchers.resetMain()}
    private fun presenter(commands:Commands):NativePresenter {
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands)
        store.put("native",p);dispatcher.scheduler.runCurrent()
        p.dispatch(UiAction.SaveServer(ServerUi("localhost","7443")))
        return p
    }
    @Test fun failedLoginNeverAuthenticatesOrEntersDemo() {
        val commands=Commands();val p=presenter(commands)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertNull(p.state.value.account);assertNotNull(p.state.value.loginError)
        p.dispatch(UiAction.Explore);assertFalse(p.state.value.demo);assertTrue(p.state.value.conversations.isEmpty())
    }
    @Test fun sendDelegatesWithoutInventingHistoryOrDeliveryAndFailurePreservesDraftSignal() {
        val commands=Commands().apply {reject=false};val p=presenter(commands)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.Select("account:8"));p.dispatch(UiAction.PreviewMessage("hello"))
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,commands.sends);assertTrue(p.state.value.sendAccepted)
        assertTrue(p.state.value.selected!!.messages.isEmpty())
        commands.failSend=true;p.dispatch(UiAction.PreviewMessage("keep draft"));dispatcher.scheduler.advanceUntilIdle()
        assertFalse(p.state.value.sendAccepted);assertEquals(2,p.state.value.sendCompletion)
        assertTrue(p.state.value.notice!!.contains("draft"))
        p.dispatch(UiAction.JoinVoice("room"));assertTrue(p.state.value.selected!!.voiceRooms.isEmpty())
        store.clear();assertTrue(commands.closed)
    }
}
