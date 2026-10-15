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
class GroupPresenterPreferencesTest {
    private val dispatcher=StandardTestDispatcher()
    private val lifecycle=ViewModelStore()
    private val endpoint=ServerEndpoint("server.example",7443,"e".repeat(64))
    private val groupId="c".repeat(32)
    private val conversation="group:$groupId"
    private class Preferences:AndroidQolRepository {
        var value=AndroidQolState();var fail=false
        override fun load():AndroidQolState {check(!fail);return value}
        override fun save(value:AndroidQolState){check(!fail);check(value.conversations.size<=128);this.value=value}
    }
    private class Commands:MessagingApplication {
        override val state=MutableStateFlow(MessagingSnapshot())
        var sent=0;var failSend=false
        override suspend fun signIn(endpoint:ServerEndpoint,username:String,password:String)=AccountSession(19,username,"test-only",true)
        override suspend fun send(peerId:String,text:String)="d".repeat(32)
        override suspend fun older(peerId:String,before:Long)=emptyList<MessageSnapshot>()
        override suspend fun sendGroup(id:String,text:String):String {sent++;check(!failSend);return "d".repeat(32)}
        override suspend fun groupHistory(id:String,before:Long)=emptyList<GroupMessageSnapshot>()
        override suspend fun requestFriend(username:String){}
        override suspend fun respondFriend(accountId:Long,accept:Boolean){}
        override suspend fun signOut(){state.value=MessagingSnapshot()}
        override fun close(){}
    }
    @Before fun before(){Dispatchers.setMain(dispatcher)}
    @After fun after(){lifecycle.clear();Dispatchers.resetMain()}
    private fun message(sequence:Long)=GroupMessageSnapshot(sequence.toString(16).padStart(32,'0'),"b".repeat(64),8,sequence,sequence,sequence,"Received","Message $sequence")
    private fun snapshot(messages:List<GroupMessageSnapshot>,unread:Int=0,hasOlder:Boolean=false,account:Long=19)=
        MessagingSnapshot(account,"Test","a".repeat(64),true,"Authenticated",serverPresenceLive=true,
            groups=listOf(GroupConversationSnapshot(groupId,"legacy",1,"Group",true,2,messages,unread,hasOlder=hasOlder)))
    private fun presenter(commands:Commands,preferences:Preferences,server:ServerEndpoint=endpoint):NativePresenter {
        val settings=object:ServerConfigurationRepository {override fun load()=server;override fun save(endpoint:ServerEndpoint){}}
        return NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences(),preferences).also{
            lifecycle.put("presenter-${System.identityHashCode(it)}",it);dispatcher.scheduler.advanceUntilIdle()
        }
    }
    private fun key(account:Long=19,server:ServerEndpoint=endpoint)=groupPreferenceKey(accountPreferenceKey(server,account),groupId)

    @Test fun historicalRowsBaselineOnceAndNativePagingIsPreserved(){
        val commands=Commands();val preferences=Preferences();val p=presenter(commands,preferences)
        commands.state.value=snapshot(listOf(message(10),message(11)),hasOlder=false);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,p.state.value.conversations.single().unread);assertFalse(p.state.value.conversations.single().hasOlder)
        assertEquals(11L,preferences.value.conversations[key()]!!.readSequence)
        commands.state.value=snapshot(listOf(message(1),message(10),message(11)),hasOlder=true);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,p.state.value.conversations.single().unread);assertTrue(p.state.value.conversations.single().hasOlder)
        commands.state.value=snapshot(listOf(message(10),message(11),message(12)),unread=1);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,p.state.value.conversations.single().unread)
        p.dispatch(UiAction.Select(conversation));dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,p.state.value.conversations.single().unread);assertEquals(12L,preferences.value.conversations[key()]!!.readSequence)
    }

    @Test fun liveArrivalDuringInitialHistoryIsUnreadAndReadCursorSurvivesRestart(){
        val preferences=Preferences();val first=Commands();val p=presenter(first,preferences)
        first.state.value=snapshot(listOf(message(9),message(10)),unread=1);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,p.state.value.conversations.single().unread);assertEquals(9L,preferences.value.conversations[key()]!!.readSequence)
        val second=Commands();val reopened=presenter(second,preferences)
        second.state.value=snapshot(listOf(message(9),message(10)),unread=0);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,reopened.state.value.conversations.single().unread)
    }

    @Test fun draftBeforeFirstHistoryDoesNotFabricateUnreadAfterReopen(){
        val preferences=Preferences();val first=Commands();val p=presenter(first,preferences)
        first.state.value=snapshot(emptyList(),hasOlder=true);dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.Draft(conversation,"saved draft"));dispatcher.scheduler.advanceUntilIdle()
        assertTrue(preferences.value.conversations[key()]!!.baselinePending)
        val second=Commands();val reopened=presenter(second,preferences)
        second.state.value=snapshot(listOf(message(40)),hasOlder=false);dispatcher.scheduler.advanceUntilIdle()
        assertEquals("saved draft",reopened.state.value.drafts[conversation]);assertEquals(0,reopened.state.value.conversations.single().unread)
        assertFalse(preferences.value.conversations[key()]!!.baselinePending)
    }

    @Test fun draftsAreScopedToAccountAndServerAndClearedOnlyAfterAcceptedSend(){
        val preferences=Preferences();val commands=Commands();val p=presenter(commands,preferences)
        commands.state.value=snapshot(emptyList());dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.Select(conversation));p.dispatch(UiAction.Draft(conversation,"Private 😀"));dispatcher.scheduler.advanceUntilIdle()
        commands.failSend=true;p.dispatch(UiAction.PreviewMessage("Private 😀"));dispatcher.scheduler.advanceUntilIdle()
        assertEquals("Private 😀",preferences.value.conversations[key()]!!.draft)
        val other=Commands();val otherPresenter=presenter(other,preferences,endpoint.copy(certificatePin="f".repeat(64)))
        other.state.value=snapshot(emptyList());dispatcher.scheduler.advanceUntilIdle();assertEquals("",otherPresenter.state.value.drafts[conversation])
        commands.state.value=snapshot(emptyList(),account=20);dispatcher.scheduler.advanceUntilIdle();assertEquals("",p.state.value.drafts[conversation])
        commands.state.value=snapshot(emptyList());dispatcher.scheduler.advanceUntilIdle();assertEquals("Private 😀",p.state.value.drafts[conversation])
        commands.failSend=false;p.dispatch(UiAction.PreviewMessage("Private 😀"));dispatcher.scheduler.advanceUntilIdle()
        assertEquals("",preferences.value.conversations[key()]!!.draft)
    }

    @Test fun utf8LimitAndPreferenceBoundRejectWithoutSavingOrSending(){
        val preferences=Preferences();val commands=Commands();val p=presenter(commands,preferences)
        commands.state.value=snapshot(emptyList());dispatcher.scheduler.advanceUntilIdle();p.dispatch(UiAction.Select(conversation))
        val fits="😀".repeat(1024);p.dispatch(UiAction.Draft(conversation,fits));dispatcher.scheduler.advanceUntilIdle()
        assertEquals(fits,preferences.value.conversations[key()]!!.draft)
        p.dispatch(UiAction.Draft(conversation,fits+"x"));p.dispatch(UiAction.PreviewMessage(fits+"x"));dispatcher.scheduler.advanceUntilIdle()
        assertEquals(fits,preferences.value.conversations[key()]!!.draft);assertEquals(0,commands.sent)
        val full=Preferences().apply{value=AndroidQolState(conversations=(1..128).associate{groupPreferenceKey("f".repeat(64),it.toString(16).padStart(32,'0')) to ConversationPreference()})}
        val other=Commands();val bounded=presenter(other,full);other.state.value=snapshot(emptyList());dispatcher.scheduler.advanceUntilIdle()
        bounded.dispatch(UiAction.Draft(conversation,"not saved"));dispatcher.scheduler.advanceUntilIdle();assertEquals(128,full.value.conversations.size);assertFalse(full.value.conversations.containsKey(key()))
    }

    @Test fun groupNotificationLinkRequiresMatchingScopedAccount(){
        val commands=Commands();val p=presenter(commands,Preferences());commands.state.value=snapshot(listOf(message(1)));dispatcher.scheduler.advanceUntilIdle()
        p.openNotification(accountPreferenceKey(endpoint,20),conversation);assertNotEquals(conversation,p.state.value.selectedId)
        p.openNotification(accountPreferenceKey(endpoint,19),conversation);assertEquals(conversation,p.state.value.selectedId)
    }
}
