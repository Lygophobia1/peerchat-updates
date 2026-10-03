package com.peerchat.android.ui

import android.graphics.Bitmap
import android.graphics.Canvas
import androidx.activity.ComponentActivity
import androidx.compose.runtime.*
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import java.io.File
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/** Actual Compose/Android rendering on the host; no device, identities or native handles. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w360dp-h800dp-port")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class UiPreviewRenderTest {
    @get:Rule val compose = createAndroidComposeRule<ComponentActivity>()
    private fun save(name: String) {
        compose.waitForIdle()
        // Draw the laid-out Android view directly. Host PixelCopy has no real compositor.
        val bitmap = compose.runOnIdle {
            val view = compose.activity.window.decorView
            Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888).also { view.draw(Canvas(it)) }
        }
        assertTrue(bitmap.width >= 320)
        assertTrue(bitmap.height >= 320)
        val colors = mutableSetOf<Int>()
        for (y in 0 until bitmap.height step 12) for (x in 0 until bitmap.width step 12) colors.add(bitmap.getPixel(x, y))
        assertTrue("Rendering must not be a blank image", colors.size > 16)
        val directory = File("build/reports/ui-previews").apply { mkdirs() }
        File(directory, "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
    }
    @Test fun renderImportantScreensInBothThemes() {
        val base = PeerChatUiState(conversations = DemoConversations().initialSnapshot())
        val group = base.conversations.first { it.id == "demo-group-gaming" }
        val joined = group.copy(voiceRooms = group.voiceRooms.mapIndexed { index, room ->
            if (index == 0) room.copy(joined = true, participants = room.participants + "You") else room },
            shareRooms = group.shareRooms.map { it.copy(watching = true) })
        val fixtures = listOf(
            "welcome" to base,
            "login" to base.copy(route = Route.Login),
            "login-error" to base.copy(route = Route.Login, loginError = "Preview: The server couldn’t be reached."),
            "server" to base.copy(route = Route.Server),
            "chats" to base.copy(route = Route.Home),
            "groups" to base.copy(route = Route.Home, kind = ChatKind.Group),
            "dm" to base.copy(route = Route.Conversation, selectedId = "demo-dm-sammy"),
            "group" to base.copy(route = Route.Conversation, selectedId = group.id),
            "direct-details" to base.copy(route = Route.Details, selectedId = "demo-dm-sammy"),
            "group-details" to base.copy(route = Route.Details, selectedId = group.id),
            "voice-and-watch" to base.copy(route = Route.Conversation, selectedId = joined.id,
                conversations = base.conversations.map { if (it.id == joined.id) joined else it }),
            "calls" to base.copy(route = Route.Home, tab = Tab.Calls, incomingCallPreview = true),
            "you" to base.copy(route = Route.Home, tab = Tab.You),
            "offline" to base.copy(route = Route.Conversation, selectedId = "demo-dm-sammy", connection = ConnectionState.Offline),
            "empty" to base.copy(route = Route.Home, load = LoadState.Empty)
        )
        var state by mutableStateOf(base)
        var dark by mutableStateOf(true)
        compose.setContent { PeerChatTheme(dark) { PeerChatShell(state, {}) } }
        for (theme in listOf(true, false)) for ((name, fixture) in fixtures) {
            compose.runOnIdle { state = fixture; dark = theme }
            save("${if (theme) "dark" else "light"}-$name")
        }
    }
    @Test @Config(qualifiers = "w320dp-h640dp-port") fun compactPhoneLongNameComposerRemainsVisible() {
        val state = PeerChatUiState(route = Route.Conversation, selectedId = "demo-dm-long", conversations = DemoConversations().initialSnapshot())
        compose.setContent { PeerChatTheme(true) { PeerChatShell(state, {}) } }
        compose.onNodeWithTag("composer").assertIsDisplayed().performTextInput("Line one\nLine two\n😀")
        compose.onNodeWithContentDescription("Send local preview").assertIsDisplayed()
        save("compact-long-name-composer")
    }
    @Test @Config(qualifiers = "w800dp-h360dp-land") fun landscapeComposerRemainsVisible() {
        val state = PeerChatUiState(route = Route.Conversation, selectedId = "demo-dm-sammy", conversations = DemoConversations().initialSnapshot())
        compose.setContent { PeerChatTheme(false) { PeerChatShell(state, {}) } }
        compose.onNodeWithTag("composer").assertIsDisplayed()
        save("landscape-dm")
    }
}
