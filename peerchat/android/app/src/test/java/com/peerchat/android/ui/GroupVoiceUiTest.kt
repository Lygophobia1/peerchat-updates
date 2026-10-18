package com.peerchat.android.ui

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.peerchat.android.application.VoiceControl
import com.peerchat.android.application.VoiceSnapshot
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class GroupVoiceUiTest {
    @get:Rule val compose=createComposeRule()
    private val group="a".repeat(32)
    private val room="b".repeat(32)
    private val device="c".repeat(64)
    private val epoch="d".repeat(32)

    @Test fun liveRoomJoinIsExplicitAndUsesRealLabels() {
        val conversation=ConversationUi("group:$group","Planning",ChatKind.Group,true,"","",
            voiceRooms=listOf(VoiceRoomUi(room,"General",listOf("Jonah","Sammy"))))
        val state=PeerChatUiState(demo=false,route=Route.Details,selectedId=conversation.id,
            conversations=listOf(conversation),voiceAvailable=true)
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme{DetailsScreen(conversation,state){actions+=it}}}
        compose.onNodeWithTag("voice-room-$room").performScrollTo().assertIsEnabled().performClick()
        assertEquals(listOf(UiAction.JoinVoice(room)),actions)
        compose.onNodeWithText("Join Voice (preview)").assertDoesNotExist()
    }

    @Test fun groupCardUsesExactTargetForMediaControls() {
        val voice=VoiceSnapshot(available=true,active=true,owner="owner",peer=device,id=room,lifetime=2,
            state=5,group=group,room=room,epoch=epoch,participation="e".repeat(32))
        val conversation=ConversationUi("group:$group","Planning",ChatKind.Group,true,"","",
            voiceRooms=listOf(VoiceRoomUi(room,"General",listOf("Jonah"),joined=true)))
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme{VoiceCallCard(PeerChatUiState(demo=false,voice=voice,
            conversations=listOf(conversation))){actions+=it}}}
        compose.onNodeWithTag("group-voice-call").assertExists()
        compose.onNodeWithTag("dm-voice-answer").assertDoesNotExist()
        compose.onNodeWithTag("group-voice-mute").performClick()
        compose.onNodeWithTag("group-voice-deafen").performClick()
        compose.onNodeWithTag("group-voice-speaker").performClick()
        compose.onNodeWithTag("group-voice-leave").performClick()
        val target=voice.target!!
        assertEquals(UiAction.ControlDmVoice(target,VoiceControl.Mute,true),actions[0])
        assertEquals(UiAction.ControlDmVoice(target,VoiceControl.Deafen,true),actions[1])
        assertEquals(UiAction.SpeakerDmVoice(target,true),actions[2])
        assertEquals(UiAction.ControlDmVoice(target,VoiceControl.End),actions[3])
        assertTrue(actions.all{it !is UiAction.JoinVoice})
    }
}
