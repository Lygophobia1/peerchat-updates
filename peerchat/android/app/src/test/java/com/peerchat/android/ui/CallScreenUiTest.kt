package com.peerchat.android.ui

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.peerchat.android.application.VoiceCallTarget
import com.peerchat.android.application.VoiceSnapshot
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class CallScreenUiTest {
    @get:Rule val compose=createComposeRule()
    private val peer="a".repeat(64)
    private val call="b".repeat(32)
    private val target=VoiceCallTarget("local-owner",peer,call,7)
    private val conversation=ConversationUi("account:42","JonahNew",ChatKind.Direct,true,"","now")

    private fun state(voice:VoiceSnapshot, error:String?=null)=PeerChatUiState(demo=false,route=Route.Conversation,
        selectedId=conversation.id,conversations=listOf(conversation),voiceAvailable=true,voice=voice,
        voicePeerName=conversation.name,voiceConversationId=conversation.id,voiceUiIdentity="test-call",
        voiceBusy=false,voiceError=error)

    @Test fun timerAppearsOnlyAfterMediaConnected() {
        val actions=mutableListOf<UiAction>()
        val voice=mutableStateOf(VoiceSnapshot(available=true,active=true,owner=target.owner,peer=peer,id=call,
            lifetime=7,state=4,mediaState=1,controlAvailable=true))
        val connectedAt=1L
        compose.setContent { PeerChatTheme(true) { FullScreenDmCall(state(voice.value),actions::add,
            connectedAtElapsed=if(voice.value.mediaState==2L)connectedAt else 0,onMinimize={},onChat={},onRetry={},timeNowElapsed=121_001) } }
        compose.onNodeWithTag("dm-call-status").assertTextContains("Connecting",substring=true)
        compose.onNodeWithText("00:00").assertDoesNotExist()
        compose.runOnIdle { voice.value=voice.value.copy(mediaState=2) }
        compose.onNodeWithTag("dm-call-connected-status").assertTextContains("Connected",substring=true)
        compose.onNodeWithTag("dm-call-connected-status").assertTextContains("02:01",substring=true)
    }

    @Test fun minimizeShowsReturnBarAndRestoresSameCall() {
        val ui=mutableStateOf(state(VoiceSnapshot(available=true,active=true,owner=target.owner,peer=peer,id=call,
            lifetime=7,state=1,mediaState=1,controlAvailable=true)))
        compose.setContent { PeerChatTheme(true) { PeerChatShell(ui.value,{action->ui.value=reduceUi(ui.value,action)}) } }
        compose.onNodeWithTag("dm-call-minimize").performClick()
        compose.onNodeWithTag("dm-call-fullscreen").assertDoesNotExist()
        compose.onNodeWithTag("dm-call-return-bar").assertExists()
        compose.onNodeWithTag("dm-call-return-bar").performClick()
        compose.onNodeWithTag("dm-call-fullscreen").assertExists()
    }

    @Test fun failedCallOffersRetryAndReturnsToCallChat() {
        val actions=mutableListOf<UiAction>()
        val failed=state(VoiceSnapshot(available=true),"Audio setup failed")
        compose.setContent { PeerChatTheme(true) { PeerChatShell(failed,actions::add) } }
        compose.onNodeWithText("Couldn't start call").assertExists()
        compose.onNodeWithTag("dm-call-retry").performClick()
        assertEquals(UiAction.StartDmVoice(conversation.id),actions.single())
        compose.onNodeWithTag("dm-call-back-to-chat").performClick()
        compose.onNodeWithTag("dm-call-return-bar").assertExists()
    }

    @Test fun incomingAnswerAndDeclineKeepExactTarget() {
        val actions=mutableListOf<UiAction>()
        val incoming=state(VoiceSnapshot(available=true,active=true,owner=target.owner,peer=peer,id=call,lifetime=7,
            state=2,direction=1))
        compose.setContent { PeerChatTheme(true) { FullScreenDmCall(incoming,actions::add,connectedAtElapsed=0,
            onMinimize={},onChat={},onRetry={}) } }
        compose.onNodeWithTag("dm-voice-answer").assertExists()
        compose.onNodeWithTag("dm-voice-decline").assertExists()
        compose.onNodeWithTag("dm-voice-answer").performClick()
        compose.onNodeWithTag("dm-voice-decline").performClick()
        assertEquals(listOf(UiAction.AnswerDmVoice(target),UiAction.ControlDmVoice(target,
            com.peerchat.android.application.VoiceControl.Decline)),actions)
    }

    @Test fun preparingCallCanCancelBeforeNativeTargetExists() {
        val actions=mutableListOf<UiAction>()
        val pending=state(VoiceSnapshot(available=true)).copy(voiceBusy=true,voicePreparationToken="consent-17",
            voiceUiIdentity="pending-17",voicePendingConversationId=conversation.id)
        compose.setContent { PeerChatTheme(true) { PeerChatShell(pending,actions::add) } }
        compose.onNodeWithTag("dm-call-fullscreen").assertExists()
        compose.onNodeWithTag("dm-call-cancel").assertIsEnabled().performClick()
        assertEquals(listOf(UiAction.CancelVoicePreparation("consent-17","pending-17")),actions)
    }

    @Test fun routeChooserPassesRealDeviceIdToExactCallTarget() {
        val actions=mutableListOf<UiAction>()
        val connected=state(VoiceSnapshot(available=true,active=true,owner=target.owner,peer=peer,id=call,
            lifetime=7,state=5,mediaState=2,controlAvailable=true))
        compose.setContent { PeerChatTheme(true) { FullScreenDmCall(connected,actions::add,
            connectedAtElapsed=android.os.SystemClock.elapsedRealtime()-1000,onMinimize={},onChat={},onRetry={},
            routeOptionsOverride=listOf(CallAudioRoute(73,"Bluetooth","Car audio"))) } }
        compose.onNodeWithTag("dm-call-audio").performClick()
        compose.onNodeWithTag("dm-audio-route-73").performClick()
        assertEquals(listOf(UiAction.AudioRouteDmVoice(target,73)),actions)
    }

    private fun groupState(connected:Boolean=false):PeerChatUiState {
        val group="c".repeat(32);val room="d".repeat(32)
        val chat=ConversationUi("group:$group","Test group",ChatKind.Group,true,"","",
            voiceRooms=listOf(VoiceRoomUi(room,"General",listOf("codex","codex1"),true)))
        return state(VoiceSnapshot(available=true,active=true,owner="group-owner",peer=peer,id=room,lifetime=8,
            group=group,room=room,epoch="e".repeat(32),participation="f".repeat(32),
            state=if(connected)5 else 4,mediaState=if(connected)2 else 1,controlAvailable=true))
            .copy(conversations=listOf(chat),selectedId=chat.id,voiceConversationId=chat.id,voicePeerName=chat.name,
                voicePendingRoomId=room,voiceUiIdentity="group-call")
    }
    @Test fun groupUsesSharedScreenAndPreservesScopedControls() {
        val ui=groupState(true);val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){FullScreenDmCall(ui,actions::add,1000,{}, {}, {},timeNowElapsed=2000)}}
        compose.onNodeWithTag("group-call-fullscreen").assertExists()
        compose.onNodeWithText("codex1 · Joined").assertExists()
        compose.onNodeWithTag("group-call-mute").performClick()
        compose.onNodeWithTag("group-call-end").performClick()
        assertEquals(listOf(UiAction.ControlDmVoice(checkNotNull(ui.voice.target),com.peerchat.android.application.VoiceControl.Mute,true),
            UiAction.ControlDmVoice(checkNotNull(ui.voice.target),com.peerchat.android.application.VoiceControl.End)),actions)
        compose.onNodeWithText("Leave call").assertExists()
    }
    @Test fun groupPreparationIsVisibleAndCancelable() {
        val actions=mutableListOf<UiAction>()
        val ui=groupState().copy(voice=VoiceSnapshot(available=true),voiceBusy=true,voicePreparationToken="group-consent")
        compose.setContent{PeerChatTheme(true){PeerChatShell(ui,actions::add)}}
        compose.onNodeWithText("Preparing…").assertExists()
        compose.onNodeWithTag("group-call-cancel").performClick()
        assertEquals(listOf(UiAction.CancelVoicePreparation("group-consent","group-call")),actions)
    }
    @Test fun groupMinimizesAndDiagnosticsDoNotEndCall() {
        val ui=mutableStateOf(groupState(true));val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){PeerChatShell(ui.value,{actions+=it;ui.value=reduceUi(ui.value,it)})}}
        compose.onNodeWithTag("group-call-minimize").performClick()
        compose.onNodeWithTag("group-call-return-bar").performClick()
        compose.onNodeWithTag("group-call-diagnostics").performClick()
        assertEquals(listOf(UiAction.Navigate(Route.Diagnostics)),actions)
        compose.onNodeWithTag("group-call-return-bar").assertExists()
    }
    @Test fun groupRetryCannotStartDirectCall() {
        val actions=mutableListOf<UiAction>();val ui=groupState().copy(voice=VoiceSnapshot(available=true),voiceError="Join failed")
        compose.setContent{PeerChatTheme(true){PeerChatShell(ui,actions::add)}}
        compose.onNodeWithTag("group-call-retry").performClick()
        assertEquals(listOf(UiAction.Select(ui.voiceConversationId),UiAction.JoinVoice(ui.voicePendingRoomId)),actions)
    }
}
