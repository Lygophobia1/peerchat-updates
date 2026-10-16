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
        var saved:ServerEndpoint?=null;var invalid=false;var failSave=false;var failReadback=false
        override fun load():ServerEndpoint? {if(invalid)error("corrupted settings");return saved}
        override fun save(endpoint:ServerEndpoint) {if(failSave)error("write failed");saved=endpoint;if(failReadback)invalid=true}
    }
    private class Commands:MessagingApplication {
        override val state=MutableStateFlow(MessagingSnapshot())
        var reject=true;var sends=0;var failSend=false;var closed=false
        var sendBlocker:kotlinx.coroutines.CompletableDeferred<Unit>?=null
        val sendBlockers=mutableMapOf<String,kotlinx.coroutines.CompletableDeferred<Unit>>()
        val sendIds=mutableMapOf<String,String>()
        var signIns=0;var endpoint:ServerEndpoint?=null
        var registrations=0
        override suspend fun createAccountRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession {
            registrations++;return signInRemembered(endpoint,username,password,remember)
        }
        var resume=false;var remembered=false
        var resumeFailure:Exception?=null
        var resumeDelay=0L
        var localHistory=false
        var localFailure=false
        var resets=0
        var signOuts=0
        val reenrollments=mutableListOf<Pair<MailboxReenrollment,Boolean>>()
        var failReenrollment=false
        override suspend fun reenrollMailbox(value:MailboxReenrollment,accept:Boolean){
            reenrollments.add(value to accept)
            if(failReenrollment)error("test-only enrollment failure")
            state.value=state.value.copy(mailboxReenrollment=null)
        }
        override suspend fun restoreLocalHistory(endpoint:ServerEndpoint):Boolean {
            if(localFailure)error("test-only local history failure")
            if(!localHistory)return false
            state.value=MessagingSnapshot(19,"Jonah","a".repeat(64),false,"Offline; local history available",listOf(
                ConversationSnapshot(FriendIdentity(8,"Sammy","b".repeat(64),false),ConnectionSnapshot(),listOf(
                    MessageSnapshot("c".repeat(32),"b".repeat(64),"a".repeat(64),1,1,1,2,"Persisted test history")))),localHistoryReady=true)
            return true
        }
        override suspend fun restoreRemembered(endpoint:ServerEndpoint):Boolean {
            if(resumeDelay>0)kotlinx.coroutines.delay(resumeDelay)
            resumeFailure?.let{throw it}
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
        override suspend fun send(peerId:String,text:String):String {sends++;(sendBlockers[text]?:sendBlocker)?.await();if(failSend)error("test failure");return sendIds[text]?:"c".repeat(32)}
        override suspend fun older(peerId:String,before:Long)=emptyList<MessageSnapshot>()
        override suspend fun requestFriend(username:String) {}
        val searches=mutableListOf<String>()
        var searchDelay=0L
        var searchBlocker:kotlinx.coroutines.CompletableDeferred<Unit>?=null
        var cancelSearch=false
        override suspend fun searchPeople(query:String):List<DirectoryAccountSnapshot>{
            searches.add(query)
            if(cancelSearch)throw kotlinx.coroutines.CancellationException("test-only lookup interrupted")
            searchBlocker?.let{blocker->kotlinx.coroutines.withContext(kotlinx.coroutines.NonCancellable){blocker.await()}}
            kotlinx.coroutines.delay(searchDelay);return listOf(DirectoryAccountSnapshot(27,query,true))
        }
        override suspend fun refreshContacts(){}
        override suspend fun respondFriend(accountId:Long,accept:Boolean) {}
        override suspend fun signOut(){signOuts++;state.value=MessagingSnapshot()}
        override suspend fun resetNetworkSession(){resets++;state.value=state.value.copy(authenticated=false,serverPresenceLive=false,status="Network session reset")}
        override fun close(){closed=true}
    }
    @Before fun start(){Dispatchers.setMain(dispatcher)}
    @After fun stop(){store.clear();Dispatchers.resetMain()}
    private fun presenter(commands:Commands):NativePresenter {
        val settings=Settings().apply{saved=ServerEndpoint("localhost",7443,"a".repeat(64))}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences()){"a".repeat(64)}
        store.put("native",p);dispatcher.scheduler.runCurrent()
        p.dispatch(UiAction.SaveServer(ServerUi("localhost","7443")))
        dispatcher.scheduler.advanceUntilIdle()
        return p
    }
    @Test fun peopleSearchDebouncesAndDropsPreviousQueryAndOfflineResults(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test password");dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.PeopleQuery("sa"));dispatcher.scheduler.advanceTimeBy(200)
        p.dispatch(UiAction.PeopleQuery("sam"));dispatcher.scheduler.advanceTimeBy(349);dispatcher.scheduler.runCurrent()
        assertTrue(commands.searches.isEmpty())
        dispatcher.scheduler.advanceTimeBy(1);dispatcher.scheduler.runCurrent()
        assertEquals(listOf("sam"),commands.searches);assertEquals("sam",p.state.value.peopleResults.single().username)
        commands.searchDelay=1000
        p.dispatch(UiAction.PeopleQuery("old"));dispatcher.scheduler.advanceTimeBy(350);dispatcher.scheduler.runCurrent()
        p.dispatch(UiAction.PeopleQuery("new"));dispatcher.scheduler.advanceUntilIdle()
        assertEquals("new",p.state.value.peopleResults.single().username)
        commands.state.value=commands.state.value.copy(serverPresenceLive=false);dispatcher.scheduler.runCurrent()
        assertTrue(p.state.value.peopleResults.isEmpty())
        p.dispatch(UiAction.PeopleQuery("offline"));dispatcher.scheduler.advanceUntilIdle()
        assertFalse(commands.searches.contains("offline"));assertNotNull(p.state.value.peopleError)
    }
    @Test fun peopleSearchDeadlineIncludesNonCooperativeLookupAndSerializedWait(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test password");dispatcher.scheduler.advanceUntilIdle()
        val blocker=kotlinx.coroutines.CompletableDeferred<Unit>();commands.searchBlocker=blocker
        try{
            p.dispatch(UiAction.PeopleQuery("hung"));dispatcher.scheduler.advanceTimeBy(350);dispatcher.scheduler.runCurrent()
            assertEquals(listOf("hung"),commands.searches)
            p.dispatch(UiAction.PeopleQuery("queued"));dispatcher.scheduler.advanceTimeBy(12_001);dispatcher.scheduler.runCurrent()
            assertFalse("A blocked lookup must not leave Searching indefinitely",p.state.value.peopleLoading)
            assertTrue(p.state.value.peopleError.orEmpty().contains("timed out"))
            assertEquals("One network lookup remains bounded even when replaced",listOf("hung"),commands.searches)
        }finally{blocker.complete(Unit);dispatcher.scheduler.runCurrent()}
        assertTrue(p.state.value.peopleResults.isEmpty())
        assertTrue(p.state.value.peopleError.orEmpty().contains("timed out"))
        commands.searchBlocker=null;p.dispatch(UiAction.PeopleQuery("retry"));dispatcher.scheduler.advanceUntilIdle()
        assertEquals("retry",p.state.value.peopleResults.single().username)
    }
    @Test fun hungPeopleSearchTimesOutAndLateOldAccountResultCannotPublish(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test password");dispatcher.scheduler.advanceUntilIdle()
        val blocker=kotlinx.coroutines.CompletableDeferred<Unit>();commands.searchBlocker=blocker
        try{
            p.dispatch(UiAction.PeopleQuery("hung"));dispatcher.scheduler.advanceTimeBy(12_001);dispatcher.scheduler.runCurrent()
            assertFalse(p.state.value.peopleLoading);assertTrue(p.state.value.peopleError.orEmpty().contains("timed out"))
            commands.state.value=commands.state.value.copy(accountId=99,username="Other")
            dispatcher.scheduler.runCurrent()
            assertEquals("",p.state.value.peopleQuery);assertNull(p.state.value.peopleError)
        }finally{blocker.complete(Unit);dispatcher.scheduler.runCurrent()}
        assertTrue(p.state.value.peopleResults.isEmpty());assertFalse(p.state.value.peopleLoading)
    }
    @Test fun peopleSearchInterruptedWithoutContextChangeClearsLoadingAndCanRetry(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test password");dispatcher.scheduler.advanceUntilIdle()
        commands.cancelSearch=true;p.dispatch(UiAction.PeopleQuery("interrupted"));dispatcher.scheduler.advanceUntilIdle()
        assertFalse(p.state.value.peopleLoading);assertNotNull(p.state.value.peopleError)
        commands.cancelSearch=false;p.dispatch(UiAction.PeopleQuery("retry"));dispatcher.scheduler.advanceUntilIdle()
        assertEquals("retry",p.state.value.peopleResults.single().username)
    }
    private fun reenrollmentOffer()=mailboxReenrollment(NativeRecordWriter(3).also{w->
        w.text("PeerChat/MailboxReenrollment/v1");w.number(19);w.number(3);w.number(4)
        w.text("a".repeat(64));w.text("b".repeat(32));w.number(2);w.text("localhost:7443")
        w.text("c".repeat(64));w.text("d".repeat(32));w.blob(byteArrayOf(1))
    }.bytes())
    @Test fun mailboxConsentForwardsExactOfferOnceAndDoesNotClaimRegistrationCompleted() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        val value=reenrollmentOffer()
        commands.state.value=commands.state.value.copy(mailboxReenrollment=value);dispatcher.scheduler.runCurrent()
        p.dispatch(UiAction.ReenrollMailbox(value,true));p.dispatch(UiAction.ReenrollMailbox(value,true))
        assertTrue(p.state.value.mailboxRecoveryBusy)
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals(listOf(value to true),commands.reenrollments)
        assertFalse(p.state.value.mailboxRecoveryBusy)
        assertEquals("Enrollment requested; offline receiving waits for authenticated registration.",p.state.value.notice)
    }
    @Test fun mailboxEnrollmentFailurePreservesOfferAndDoesNotReportSuccess() {
        val commands=Commands().apply{reject=false;failReenrollment=true};val p=presenter(commands)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        val value=reenrollmentOffer()
        commands.state.value=commands.state.value.copy(mailboxReenrollment=value);dispatcher.scheduler.runCurrent()
        p.dispatch(UiAction.ReenrollMailbox(value,true));dispatcher.scheduler.advanceUntilIdle()
        assertEquals(value,p.state.value.mailboxReenrollment)
        assertTrue(p.state.value.mailboxRecoveryError!!.contains("Reconnect to the trusted server"))
        assertFalse(p.state.value.mailboxRecoveryBusy)
        assertFalse(p.state.value.notice?.contains("Enrollment requested")==true)
    }
    @Test fun savedEndpointAndPinAreUsedAfterPresenterRestartAndSignOut() {
        val settings=Settings();val application=ApplicationProvider.getApplicationContext<Application>()
        val first=NativePresenter(application,Commands(),settings,dispatcher,Preferences()){"f".repeat(64)};store.put("first",first)
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
    @Test fun createAccountNavigationPreservesEndpointPinAndUsesApplicationRegistration() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        val server=p.state.value.server
        p.dispatch(UiAction.Navigate(Route.Login));p.dispatch(UiAction.Navigate(Route.Register))
        assertEquals(server,p.state.value.server)
        p.dispatch(UiAction.Back);assertEquals(Route.Login,p.state.value.route)
        p.dispatch(UiAction.Navigate(Route.Register));p.dispatch(UiAction.RememberMe(true))
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,commands.registrations);assertTrue(commands.remembered)
        assertEquals(ServerEndpoint(server.host,server.port.toInt(),server.certificatePin),commands.endpoint)
        assertEquals(Route.Home,p.state.value.route)
    }
    @Test fun ownershipAndSecurityErrorsDoNotSuggestReplacingIdentity() {
        assertTrue(accountFailureText(ServerRejected("device_account_mismatch"),false).contains("belongs to another account"))
        assertTrue(accountFailureText(ServerRejected("login_failed"),false).contains("Username or password"))
        assertFalse(accountFailureText(java.io.IOException("test-only"),false).contains("Create Account"))
        assertTrue(accountFailureText(MessagingStartupFailure(java.io.IOException("test-only")),false).contains("Authentication succeeded"))
        assertTrue(accountFailureText(MessagingStartupFailure(ServerRejected("device_account_mismatch")),true).contains("belongs to another account"))
    }
    @Test fun coldStartHistoryRemainsVisibleWhenNetworkAuthenticationFails() {
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443,"f".repeat(64))}
        val commands=Commands().apply{localHistory=true;resumeFailure=java.io.IOException("test-only unavailable network")}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("offline-history",p)
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals(Route.Home,p.state.value.route);assertNull(p.state.value.startupStatus)
        assertEquals(ConnectionState.Offline,p.state.value.connection)
        assertEquals("Persisted test history",p.state.value.conversations.single().messages.single().text)
        assertFalse(p.state.value.conversations.single().online)
        p.dispatch(UiAction.Select("account:8"));p.dispatch(UiAction.PreviewMessage("Offline draft"))
        dispatcher.scheduler.advanceUntilIdle();assertEquals(1,commands.sends)
        // UI never promotes cached history/account metadata to authenticated presence.
        assertFalse(commands.state.value.authenticated);assertFalse(commands.state.value.serverPresenceLive)
    }
    @Test fun slowAuthenticationDoesNotCoverLocalHistoryWithStartupScreen() {
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443,"f".repeat(64))}
        val commands=Commands().apply{localHistory=true;resumeDelay=10000}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("slow-history",p)
        dispatcher.scheduler.runCurrent()
        assertEquals(Route.Home,p.state.value.route);assertNull(p.state.value.startupStatus)
        assertEquals("Persisted test history",p.state.value.conversations.single().messages.single().text)
        assertEquals(ConnectionState.Offline,p.state.value.connection);assertTrue(p.state.value.loginBusy)
        p.dispatch(UiAction.Select("account:8"))
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals(Route.Conversation,p.state.value.route);assertEquals("account:8",p.state.value.selectedId)
    }
    @Test fun savingBubbleIsImmediateAndNewTypingSurvivesDurableReturn(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","fixture");dispatcher.scheduler.advanceUntilIdle();p.dispatch(UiAction.Select("account:8"))
        val blocker=kotlinx.coroutines.CompletableDeferred<Unit>();commands.sendBlocker=blocker
        p.dispatch(UiAction.Draft("account:8","sent draft"));p.dispatch(UiAction.PreviewMessage("sent draft"))
        assertEquals("",p.state.value.drafts["account:8"])
        assertEquals(MessageStatus.Saving,p.state.value.selected!!.messages.single().status)
        assertEquals(0,commands.sends)
        dispatcher.scheduler.runCurrent();assertTrue(p.state.value.sendBusy);assertEquals(1,commands.sends)
        p.dispatch(UiAction.Draft("account:8","new typing"));blocker.complete(Unit);dispatcher.scheduler.advanceUntilIdle()
        assertEquals("new typing",p.state.value.drafts["account:8"])
        assertEquals(MessageStatus.Queued,p.state.value.selected!!.messages.single().status)
        assertFalse(p.state.value.sendBusy)
    }
    @Test fun accountChangeInvalidatesBusySendAndRejectsStaleCompletion(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","fixture");dispatcher.scheduler.advanceUntilIdle();p.dispatch(UiAction.Select("account:8"))
        val blocker=kotlinx.coroutines.CompletableDeferred<Unit>();commands.sendBlocker=blocker
        p.dispatch(UiAction.PreviewMessage("fixture"));dispatcher.scheduler.runCurrent();assertTrue(p.state.value.sendBusy)
        commands.state.value=commands.state.value.copy(accountId=20);dispatcher.scheduler.runCurrent()
        assertFalse(p.state.value.sendBusy);assertNull(p.state.value.selectedId)
        blocker.complete(Unit);dispatcher.scheduler.advanceUntilIdle()
        assertFalse(p.state.value.sendAccepted);assertFalse(p.state.value.sendBusy)
    }
    @Test fun twoCapturedSendsProgressIndependentlyAndCompleteInReverseOrder(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","fixture");dispatcher.scheduler.advanceUntilIdle();p.dispatch(UiAction.Select("account:8"))
        val hello=kotlinx.coroutines.CompletableDeferred<Unit>();val second=kotlinx.coroutines.CompletableDeferred<Unit>()
        commands.sendBlockers["Hello"]=hello;commands.sendBlockers["How are you"]=second
        commands.sendIds["Hello"]="c".repeat(32);commands.sendIds["How are you"]="d".repeat(32)
        p.dispatch(UiAction.Draft("account:8","Hello"));p.dispatch(UiAction.PreviewMessage("Hello"))
        assertEquals("",p.state.value.drafts["account:8"])
        p.dispatch(UiAction.Draft("account:8","How are you"));p.dispatch(UiAction.PreviewMessage("How are you"))
        assertEquals("",p.state.value.drafts["account:8"])
        assertEquals(listOf(MessageStatus.Saving,MessageStatus.Saving),p.state.value.selected!!.messages.map{it.status})
        dispatcher.scheduler.runCurrent();assertEquals(2,commands.sends)
        p.dispatch(UiAction.Draft("account:8","third draft"))
        second.complete(Unit);dispatcher.scheduler.runCurrent()
        assertTrue(p.state.value.sendBusy);assertEquals("third draft",p.state.value.drafts["account:8"])
        assertEquals(MessageStatus.Queued,p.state.value.selected!!.messages.single{it.id=="d".repeat(32)}.status)
        assertEquals(MessageStatus.Saving,p.state.value.selected!!.messages.single{it.text=="Hello"}.status)
        hello.complete(Unit);dispatcher.scheduler.advanceUntilIdle()
        assertFalse(p.state.value.sendBusy);assertEquals("third draft",p.state.value.drafts["account:8"])
        assertEquals(setOf("c".repeat(32),"d".repeat(32)),p.state.value.selected!!.messages.map{it.id}.toSet())
    }
    @Test fun failedCapturedMessageCannotReplaceNewTypingAndRetainsItsProtectedDraft(){
        val commands=Commands().apply{reject=false;failSend=true}
        val preferences=Preferences();val settings=Settings().apply{saved=ServerEndpoint("localhost",7443,"a".repeat(64))}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,preferences)
        store.put("capture-failure",p);dispatcher.scheduler.runCurrent()
        p.login("Jonah","fixture");dispatcher.scheduler.advanceUntilIdle();p.dispatch(UiAction.Select("account:8"))
        p.dispatch(UiAction.Draft("account:8","captured original"));dispatcher.scheduler.advanceUntilIdle()
        val blocker=kotlinx.coroutines.CompletableDeferred<Unit>();commands.sendBlocker=blocker
        p.dispatch(UiAction.PreviewMessage("captured original"));dispatcher.scheduler.runCurrent()
        assertEquals("",p.state.value.drafts["account:8"])
        p.dispatch(UiAction.Draft("account:8","new typing"));dispatcher.scheduler.advanceTimeBy(300);dispatcher.scheduler.runCurrent()
        assertTrue(preferences.value.conversations.values.any{it.draft=="captured original"})
        blocker.complete(Unit);dispatcher.scheduler.advanceUntilIdle()
        val failed=p.state.value.selected!!.messages.single()
        assertEquals(MessageStatus.Failed,failed.status);assertEquals("captured original",failed.text)
        p.dispatch(UiAction.RetryPreview(failed.id))
        assertEquals("new typing",p.state.value.drafts["account:8"])
        assertEquals(1,commands.sends)
        p.dispatch(UiAction.Draft("account:8",""));p.dispatch(UiAction.RetryPreview(failed.id));dispatcher.scheduler.advanceUntilIdle()
        assertEquals("captured original",p.state.value.drafts["account:8"])
        assertEquals(1,commands.sends)
    }
    @Test fun localSendBoundRejectsSeventeenthWithoutClearingItsDraft(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","fixture");dispatcher.scheduler.advanceUntilIdle();p.dispatch(UiAction.Select("account:8"))
        val blocker=kotlinx.coroutines.CompletableDeferred<Unit>();commands.sendBlocker=blocker
        repeat(16){index->val text="message $index";commands.sendIds[text]=(index+1).toString(16).padStart(32,'0')
            p.dispatch(UiAction.Draft("account:8",text));p.dispatch(UiAction.PreviewMessage(text))
        }
        assertEquals(16,p.state.value.localSendCount)
        assertEquals(16,p.state.value.selected!!.messages.count{it.status==MessageStatus.Saving})
        p.dispatch(UiAction.Draft("account:8","retained seventeenth"));p.dispatch(UiAction.PreviewMessage("retained seventeenth"))
        assertEquals("retained seventeenth",p.state.value.drafts["account:8"])
        dispatcher.scheduler.runCurrent();assertEquals(16,commands.sends)
        blocker.complete(Unit);dispatcher.scheduler.advanceUntilIdle()
    }
    @Test fun typingBeforeProtectedPreferencesLoadIsMergedAndSaved(){
        val tasks=java.util.ArrayDeque<Runnable>()
        val configuration=object:kotlinx.coroutines.CoroutineDispatcher(){
            override fun dispatch(context:kotlin.coroutines.CoroutineContext,block:Runnable){tasks.addLast(block)}
        }
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443,"f".repeat(64))}
        val preferences=Preferences();val commands=Commands().apply{localHistory=true}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,configuration,preferences,Preferences())
        store.put("early-draft",p);dispatcher.scheduler.runCurrent()
        // Server configuration was queued before the independently loaded QoL
        // preferences. Admit local history while preference IO is still pending.
        tasks.removeFirst().run();dispatcher.scheduler.runCurrent()
        assertEquals(Route.Home,p.state.value.route)
        p.dispatch(UiAction.Draft("account:8","new local typing"))
        repeat(10){if(tasks.isNotEmpty())tasks.removeFirst().run();dispatcher.scheduler.runCurrent()}
        dispatcher.scheduler.advanceTimeBy(300);dispatcher.scheduler.runCurrent()
        repeat(10){if(tasks.isNotEmpty())tasks.removeFirst().run();dispatcher.scheduler.runCurrent()}
        assertEquals("new local typing",p.state.value.drafts["account:8"])
        assertTrue(preferences.value.conversations.values.any{it.draft=="new local typing"})
    }
    @Test fun rememberedAuthenticationIsNotOnlineUntilOwnerPublishesPresence() {
        val settings=Settings().apply {saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val commands=Commands().apply {resume=true}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("remember",p)
        dispatcher.scheduler.runCurrent()
        assertEquals("Connecting…",p.state.value.startupStatus);assertTrue(p.state.value.rememberMe)
        assertNotEquals(Route.Home,p.state.value.route);assertEquals(0,commands.signIns)
        assertEquals(ConnectionState.Reconnecting,p.state.value.connection)
        commands.state.value=commands.state.value.copy(serverPresenceLive=true)
        dispatcher.scheduler.runCurrent();assertEquals(ConnectionState.Online,p.state.value.connection)
        assertEquals(Route.Home,p.state.value.route);assertNull(p.state.value.startupStatus)
    }
    @Test fun savedSignInChecksImmediatelyWithoutShowingWelcome() {
        val settings=Settings().apply {saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),Commands(),settings,dispatcher,Preferences());store.put("startup",p)
        assertNotNull(p.state.value.startupStatus)
        dispatcher.scheduler.advanceUntilIdle()
        assertNull(p.state.value.startupStatus);assertEquals(Route.Welcome,p.state.value.route)
    }
    @Test fun rejectedSavedSignInReturnsToLoginWithoutClaimingOnline() {
        val settings=Settings().apply {saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val commands=Commands().apply{resumeFailure=ServerRejected("remember_revoked")}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("rejected",p)
        dispatcher.scheduler.advanceUntilIdle()
        assertNull(p.state.value.startupStatus);assertEquals(Route.Login,p.state.value.route)
        assertEquals(ConnectionState.Offline,p.state.value.connection);assertNotNull(p.state.value.loginError)
    }
    @Test fun temporaryUnavailableStartupRemainsReconnectableNotOnline() {
        val settings=Settings().apply {saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val commands=Commands().apply{resumeFailure=java.net.ConnectException()}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("network",p)
        dispatcher.scheduler.runCurrent()
        assertEquals("Reconnecting…",p.state.value.startupStatus)
        assertEquals(ConnectionState.Offline,p.state.value.connection);assertEquals(0,commands.signIns)
        p.dispatch(UiAction.SignOut);dispatcher.scheduler.advanceUntilIdle()
        assertNull(p.state.value.startupStatus);assertEquals(Route.Welcome,p.state.value.route)
    }
    @Test fun rememberChoiceIsPassedOnlyOnExplicitAuthentication() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.dispatch(UiAction.SaveServer(ServerUi("localhost","7443","a".repeat(64))));dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.RememberMe(true));p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertTrue(commands.remembered);assertEquals(1,commands.signIns)
    }
    @Test fun localHistoryFailureReleasesSignInAndDoesNotInvalidateTrustedSettings() {
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val commands=Commands().apply{localFailure=true;reject=false}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("local-failure",p)
        dispatcher.scheduler.advanceUntilIdle()
        assertFalse(p.state.value.loginBusy);assertNull(p.state.value.startupStatus)
        assertEquals(Route.Login,p.state.value.route);assertNotNull(p.state.value.loginError)
        assertTrue(p.state.value.diagnostics.contains("Messaging startup"))
        p.dispatch(UiAction.DisconnectChangeServer);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(Route.Server,p.state.value.route);assertEquals(1,commands.resets)
        assertEquals("a".repeat(64),settings.saved!!.certificatePin)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,commands.signIns);assertEquals(ConnectionState.Online,p.state.value.connection)
    }
    @Test fun resetCancelsOldRememberedStartupWithoutRevocationOrHistoryLoss() {
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val commands=Commands().apply{localHistory=true;resume=true;resumeDelay=60000}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("reset-startup",p)
        dispatcher.scheduler.runCurrent();assertTrue(p.state.value.loginBusy)
        val history=p.state.value.conversations
        p.dispatch(UiAction.ResetNetworkSession);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,commands.resets);assertEquals(0,commands.signOuts)
        assertFalse(p.state.value.loginBusy);assertEquals(Route.Login,p.state.value.route)
        assertEquals(history,p.state.value.conversations);assertEquals("a".repeat(64),p.state.value.server.certificatePin)
        p.setForeground(true);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(Route.Login,p.state.value.route);assertFalse(commands.state.value.authenticated)
    }
    @Test fun authenticationWithoutLivePresenceTimesOutAndRetainsFailureEvidence() {
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443,"a".repeat(64))}
        val commands=Commands().apply{resume=true}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences());store.put("presence-timeout",p)
        dispatcher.scheduler.runCurrent();assertEquals(ConnectionState.Reconnecting,p.state.value.connection)
        dispatcher.scheduler.advanceTimeBy(30001);dispatcher.scheduler.runCurrent()
        assertEquals(1,commands.resets);assertEquals(0,commands.signOuts)
        assertEquals(Route.Login,p.state.value.route);assertEquals(ConnectionState.Offline,p.state.value.connection)
        assertTrue(p.state.value.notice!!.contains("timed out"))
        assertTrue(p.state.value.diagnostics.contains("startup budget"))
    }
    @Test fun establishedLoginAndArchiveRetriesNeverRunStartupLogoutWatchdog(){
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(Route.Home,p.state.value.route)
        repeat(5){
            commands.state.value=commands.state.value.copy(serverPresenceLive=false,
                status="Reconnecting authenticated server events; local history available",
                archiveProgress=commands.state.value.archiveProgress.copy(state="Archive source unavailable; retry pending"))
            dispatcher.scheduler.runCurrent();dispatcher.scheduler.advanceTimeBy(31000);dispatcher.scheduler.runCurrent()
            assertEquals(0,commands.resets);assertEquals(0,commands.signOuts);assertEquals(1,commands.signIns)
            assertTrue(commands.state.value.authenticated);assertEquals(Route.Home,p.state.value.route)
            assertEquals(ConnectionState.Reconnecting,p.state.value.connection)
            commands.state.value=commands.state.value.copy(serverPresenceLive=true)
            dispatcher.scheduler.runCurrent();assertEquals(ConnectionState.Online,p.state.value.connection)
        }
        p.dispatch(UiAction.Select("account:8"));p.dispatch(UiAction.PreviewMessage("fixture"))
        dispatcher.scheduler.advanceUntilIdle();assertEquals(1,commands.sends)
    }
    @Test fun disconnectDuringAuthenticatedSessionAllowsVerifiedSettingsSave() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.DisconnectChangeServer);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(Route.Server,p.state.value.route);assertEquals(0,commands.signOuts)
        p.dispatch(UiAction.SaveServer(ServerUi("localhost","7443","a".repeat(64))))
        dispatcher.scheduler.advanceUntilIdle()
        assertNull(p.state.value.serverTrust);assertTrue(p.state.value.notice!!.contains("saved securely"))
    }
    @Test fun rememberDoesNotPermitAnUnpinnedServerOrSendThePassword() {
        val commands=Commands().apply{reject=false};val p=presenter(commands)
        p.dispatch(UiAction.SaveServer(ServerUi("unknown.example","7443")));dispatcher.scheduler.advanceUntilIdle()
        assertNotNull(p.state.value.serverTrust)
        p.dispatch(UiAction.CancelServerTrust)
        // The pending untrusted endpoint never replaced the trusted one.
        assertEquals("localhost",p.state.value.server.host)
        val emptySettings=Settings().apply{saved=ServerEndpoint("unknown.example",7443)}
        val untrusted=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,emptySettings,dispatcher,Preferences()){"b".repeat(64)}
        store.put("untrusted",untrusted);dispatcher.scheduler.advanceUntilIdle()
        untrusted.dispatch(UiAction.RememberMe(true));untrusted.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,commands.signIns);assertTrue(untrusted.state.value.loginError!!.contains("fingerprint"))
    }
    @Test fun draftsReopenAndNotificationTapRequiresMatchingAccount() {
        val preferences=Preferences();val settings=Settings().apply{saved=ServerEndpoint("localhost",7443,"a".repeat(64))}
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
        val commands=Commands();val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences()){"a".repeat(64)}
        store.put("blocked",p);dispatcher.scheduler.advanceUntilIdle()
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle();assertEquals(0,commands.signIns)
        settings.invalid=false // Explicit fixture repair; production never drops a corrupt record.
        p.dispatch(UiAction.SaveServer(ServerUi("server.example","7443","a".repeat(64))))
        dispatcher.scheduler.advanceUntilIdle()
        settings.failSave=true
        p.dispatch(UiAction.SaveServer(ServerUi("different.example","7443")))
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals("a".repeat(64),p.state.value.server.certificatePin)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals("a".repeat(64),commands.endpoint!!.certificatePin)
    }
    @Test fun firstUseCancelTrustReconnectAndMismatchNeverReplacePin(){
        val settings=Settings();val commands=Commands()
        var observed="a".repeat(64)
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences()){observed}
        store.put("trust",p);dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.SaveServer(ServerUi("server.example","7443")));dispatcher.scheduler.advanceUntilIdle()
        assertNotNull(p.state.value.serverTrust);assertNull(settings.saved);assertEquals(0,commands.signIns)
        p.dispatch(UiAction.CancelServerTrust);assertNull(settings.saved)
        p.dispatch(UiAction.SaveServer(ServerUi("server.example","7443")));dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.TrustServer);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(observed,settings.saved!!.certificatePin);assertNull(p.state.value.serverTrust)
        p.dispatch(UiAction.SaveServer(ServerUi("SERVER.EXAMPLE","7443")));dispatcher.scheduler.advanceUntilIdle()
        assertNull(p.state.value.serverTrust);assertEquals(observed,settings.saved!!.certificatePin)
        assertEquals("server.example",settings.saved!!.host)
        observed="b".repeat(64)
        p.dispatch(UiAction.SaveServer(ServerUi("server.example","7443")));dispatcher.scheduler.advanceUntilIdle()
        assertEquals("a".repeat(64),p.state.value.serverTrust!!.expected)
        p.dispatch(UiAction.TrustServer);dispatcher.scheduler.advanceUntilIdle()
        assertEquals("a".repeat(64),settings.saved!!.certificatePin);assertEquals(0,commands.signIns)
        assertNotNull(p.state.value.serverIdentityChanged)
        p.dispatch(UiAction.CancelServerTrust)
        assertNotNull(p.state.value.serverIdentityChanged)
    }
    @Test fun firstUseLoginWaitsForDurableTrustThenContinuesExactlyOnce() {
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443)}
        val commands=Commands().apply{reject=false}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences()){"a".repeat(64)}
        store.put("continue-trust",p);dispatcher.scheduler.advanceUntilIdle()
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,commands.signIns);assertNotNull(p.state.value.serverTrust)
        p.dispatch(UiAction.TrustServer);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,commands.signIns)
        assertEquals(settings.saved,commands.endpoint)
        assertEquals("a".repeat(64),p.state.value.server.certificatePin)
        p.dispatch(UiAction.TrustServer);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(1,commands.signIns)
    }
    @Test fun cancelledTrustAndFailedProtectedReadbackNeverAuthenticate() {
        val settings=Settings().apply{saved=ServerEndpoint("server.example",7443)}
        val commands=Commands().apply{reject=false}
        val p=NativePresenter(ApplicationProvider.getApplicationContext<Application>(),commands,settings,dispatcher,Preferences()){"a".repeat(64)}
        store.put("cancel-trust",p);dispatcher.scheduler.advanceUntilIdle()
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        p.dispatch(UiAction.CancelServerTrust)
        p.dispatch(UiAction.SaveServer(ServerUi("server.example","7443","a".repeat(64))))
        dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,commands.signIns)
        settings.saved=ServerEndpoint("server.example",7443)
        p.dispatch(UiAction.SaveServer(ServerUi("server.example","7443")))
        dispatcher.scheduler.advanceUntilIdle()
        settings.failReadback=true
        p.dispatch(UiAction.TrustServer);dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,commands.signIns);assertNotNull(p.state.value.notice)
        p.login("Jonah","test-only");dispatcher.scheduler.advanceUntilIdle()
        assertEquals(0,commands.signIns)
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
        assertTrue(commands.state.value.conversations.single().messages.isEmpty())
        assertEquals(MessageStatus.Queued,p.state.value.selected!!.messages.single().status)
        assertEquals("c".repeat(32),p.state.value.selected!!.messages.single().id)
        commands.failSend=true;p.dispatch(UiAction.PreviewMessage("keep draft"));dispatcher.scheduler.advanceUntilIdle()
        assertFalse(p.state.value.sendAccepted);assertEquals(2,p.state.value.sendCompletion)
        assertTrue(p.state.value.notice!!.contains("draft"))
        p.dispatch(UiAction.JoinVoice("room"));assertTrue(p.state.value.selected!!.voiceRooms.isEmpty())
        store.clear();assertTrue(commands.closed)
    }
}
