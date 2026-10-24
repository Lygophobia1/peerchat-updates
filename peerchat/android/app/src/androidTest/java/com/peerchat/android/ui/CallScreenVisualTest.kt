package com.peerchat.android.ui

import android.graphics.Bitmap
import android.os.SystemClock
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.mutableStateOf
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.VoiceSnapshot
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Synthetic presentation only: no account, microphone, network or live call.
 * Uses platform screenshots so newer emulator APIs do not depend on Espresso's
 * older hidden InputManager injection API. Behavioral checks run in host tests. */
@RunWith(AndroidJUnit4::class)
class CallScreenVisualTest {
    @Test fun captureCallingAndConnected() {
        val chat=ConversationUi("account:8","JonahNew",ChatKind.Direct,true,"","")
        val voice=VoiceSnapshot(available=true,active=true,id="1".repeat(32),peer="a".repeat(64),
            lifetime=1,state=1,direction=0,owner="visual-fixture",controlAvailable=true)
        val state=mutableStateOf(PeerChatUiState(demo=false,route=Route.Conversation,
            account=AccountUi("account:9","Test"),conversations=listOf(chat),selectedId=chat.id,
            voiceAvailable=true,voice=voice,voicePeerName="JonahNew"))
        ActivityScenario.launch(ComponentActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> activity.setContent { PeerChatTheme(true) { PeerChatShell(state.value,{}) } } }
            capture("calling")
            scenario.onActivity { state.value=state.value.copy(voice=voice.copy(state=4,mediaState=1)) }
            capture("connecting")
            scenario.onActivity { state.value=state.value.copy(voice=voice.copy(state=5,mediaState=2)) }
            capture("connected")
        }
    }
    @Test fun captureGroupPreparingAndConnected() {
        val group="c".repeat(32);val room="d".repeat(32)
        val chat=ConversationUi("group:$group","Test group",ChatKind.Group,true,"","",
            voiceRooms=listOf(VoiceRoomUi(room,"General Voice",listOf("codex","codex1"),true)))
        val state=mutableStateOf(PeerChatUiState(demo=false,route=Route.Conversation,
            account=AccountUi("account:9","codex1"),conversations=listOf(chat),selectedId=chat.id,
            voiceAvailable=true,voiceBusy=true,voiceUiIdentity="group-visual",voiceConversationId=chat.id,
            voicePeerName=chat.name,voicePendingRoomId=room))
        ActivityScenario.launch(ComponentActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> activity.setContent { PeerChatTheme(true) { PeerChatShell(state.value,{}) } } }
            capture("group-preparing")
            scenario.onActivity { state.value=state.value.copy(voiceBusy=false,voice=VoiceSnapshot(available=true,active=true,
                owner="visual-group",peer="a".repeat(64),id=room,lifetime=1,group=group,room=room,
                state=5,mediaState=2,controlAvailable=true,diagnostic="Connected · group voice")) }
            capture("group-connected")
        }
    }
    private fun capture(name:String) {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        instrumentation.waitForIdleSync()
        SystemClock.sleep(2000)
        val directory=File(instrumentation.targetContext.getExternalFilesDir(null),"call-ui-validation")
        check(directory.mkdirs() || directory.isDirectory)
        val bitmap=checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        File(directory,"$name.png").outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.PNG,100,it)) }
        bitmap.recycle()
    }
}
