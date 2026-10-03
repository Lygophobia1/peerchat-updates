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
    private class Preferences:com.peerchat.android.platform.AndroidQolRepository {
        var value=com.peerchat.android.platform.AndroidQolState()
        override fun load()=value
        override fun save(value:com.peerchat.android.platform.AndroidQolState){this.value=value}
    }
    private class Settings:ServerConfigurationRepository {
        var saved:ServerEndpoint?=null;var invalid=false;var failSave=false
        override fun load():ServerEndpoint? {if(invalid)error("corrupted settings");return saved}
        override fun save(endpoint:ServerEndpoint) {if(failSave)error("write failed");saved=endpoint}
    }
    private class Commands:MessagingApplication {
        override val state=MutableStateFlow(MessagingSnapshot())
        var reject=true;var sends=0;var failSend=false;var closed=false
        var signIns=0;var endpoint:ServerEndpoint?=null
        var resume=false;var remembered=false
        override suspend fun restoreRemembered(endpoint:ServerEndpoint):Boolean {
            if(!resume)return false
            state.value=MessagingSnapshot(19,"Jonah","a".repeat(64),true,"Authenticated",serverPresenceLive=false)
            return true
        }
        override suspend fun signInRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession {
            remembered=remember;return signIn(endpoint,username,password)
        }
        override suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String):AccountSession {
            signIns++;this.endpoint=endpoint
            if(reject)throw ServerRejected("bad_credentials")
            state.value=MessagingSnapshot(19,"Jonah","a".repeat(64),true,"Authenticated",listOf(
                ConversationSnapshot(FriendIdentity(8,"Sammy","b".repeat(64),true),ConnectionSnapshot(ready=true),emptyList())),serverPresenceLive=true)
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
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,Settings(),dispatcher,Preferences())
        store.put("native",p);dispatcher.scheduler.runCurrent()
        p.dispatch(UiAction.SaveServer(ServerUi("localhost","7443")))
        dispatcher.scheduler.advanceUntilIdle()
        return p
    }
    @Test fun savedEndpointAndPinAreUsedAfterPresenterRestartAndSignOut() {
        val settings=Settings();val application=ApplicationProvider.getApplicationContext<Application>()
        val first=NativePresenter(application,Commands(),settings,dispatcher,Preferences());store.put("first",first)
        dispatcher.scheduler.advanceUntilIdle()
        first.dispatch(UiAction.SaveServer(ServerUi("server.example","8443","F".repeat(64))))
        dispatcher.scheduler.advanceUntilIdle()
        val commands=Commands().apply {reject=false}
        val second=NativePresenter(application,commands,settings,dispatcher,Preferences());store.put("second",second)
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals("server.example",second.state.value.server.host)
        second.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(ServerEndpoint("server.example",8443,"f".repeat(64)),commands.endpoint)
        second.dispatch(UiAction.SaveServer(ServerUi("attacker.example","7443")))
        dispatcher.scheduler.advanceUntilIdle();assertEquals("server.example",settings.saved!!.host)
        second.dispatch(UiAction.SignOut);dispatcher.scheduler.advanceUntilIdle()
        assertEquals("f".repeat(64),second.state.value.server.certificatePin)
    }
    @Test fun rememberedAuthenticationIsNotOnlineUntilOwnerPublishesPresence() {
        val settings=Settings().apply {saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val commands=Commands().apply {resume=true}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("remember",p)
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals(Route.Home,p.state.value.route);assertTrue(p.state.value.rememberMe)
        assertEquals(ConnectionState.Reconnecting,p.state.value.connection)
        commands.state.value=commands.state.value.copy(serverPresenceLive=true)
        dispatcher.scheduler.runCurrent();assertEquals(ConnectionState.Online,p.state.value.connection)
    }
    @Test fun rememberChoiceIsPassedOnlyOnExplicitAuthentication() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.dispatch(UiAction.SaveServer(ServerUi("localhost","7443","a".repeat(64))));dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.RememberMe(true));p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertTrue(commands.remembered);assertEquals(1,commands.signIns)
    }
    @Test fun rememberDoesNotPermitAnUnpinnedServerOrSendThePassword() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.dispatch(UiAction.RememberMe(true));p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,commands.signIns);assertTrue(p.state.value.loginError!!.contains("fingerprint"))
    }
    @Test fun draftsReopenAndNotificationTapRequiresMatchingAccount() {
        val preferences=Preferences();val settings=Settings().apply{saved=ServerEndpoint("localhost",7443)}
        val commands=Commands().apply{reject=false}
        val application=ApplicationProvider.getApplicationContext<Application>()
        val first=NativePresenter(application,commands,settings,dispatcher,preferences);store.put("draft-first",first)
        dispatcher.scheduler.advanceUntilIdle();first.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        first.dispatch(UiAction.Select("account:8"));first.dispatch(UiAction.Draft("account:8","unsent 😀 draft"))
        dispatcher.scheduler.advanceUntilIdle()
        val next=Commands().apply{state.value=commands.state.value}
        val second=NativePresenter(application,next,settings,dispatcher,preferences);store.put("draft-second",second)
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals("unsent 😀 draft",second.state.value.drafts["account:8"])
        second.openNotification("f".repeat(64),"account:8");assertNull(second.state.value.selectedId)
        val owner=com.peerchat.android.platform.accountPreferenceKey(settings.saved!!,19)
        second.openNotification(owner,"account:8");assertEquals("account:8",second.state.value.selectedId)
        assertEquals(Route.Conversation,second.state.value.route)
    }
    @Test fun unreadUsesNativeSequenceAndViewingMarksTheLocalCursorOnly() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        val c=commands.state.value.conversations.single()
        val m=MessageSnapshot("c".repeat(32),"b".repeat(64),"a".repeat(64),100,1,1,2,"real owner snapshot")
        commands.state.value=commands.state.value.copy(conversations=listOf(c.copy(messages=listOf(m))))
        dispatcher.scheduler.runCurrent();assertEquals(1,p.state.value.conversations.single().unread)
        p.dispatch(UiAction.Select("account:8"));dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,p.state.value.conversations.single().unread)
        assertEquals(listOf(m),commands.state.value.conversations.single().messages)
    }
    @Test fun corruptSavedConfigurationBlocksLoginAndFailedSaveKeepsTrustedPin() {
        val settings=Settings().apply {invalid=true}
        val commands=Commands();val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences())
        store.put("blocked",p);dispatcher.scheduler.advanceUntilIdle()
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle();assertEquals(0,commands.signIns)
        p.dispatch(UiAction.SaveServer(ServerUi("server.example","7443","a".repeat(64))))
        dispatcher.scheduler.advanceUntilIdle()
        settings.failSave=true
        p.dispatch(UiAction.SaveServer(ServerUi("different.example","7443")))
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals("a".repeat(64),p.state.value.server.certificatePin)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals("a".repeat(64),commands.endpoint!!.certificatePin)
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
