package com.peerchat.android.ui

import androidx.compose.runtime.*
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import org.junit.*
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34],qualifiers="w360dp-h800dp-port")
class ConversationAutoScrollTest {
    @get:Rule val compose=createComposeRule()
    private fun message(n:Int)=MessageUi("id$n","sender","Sender","Message number $n",false,"12:00",timestamp=n.toLong())
    @Test fun opensLatestFollowsAppendsButPreservesOlderReadingAndOffersJump() {
        var rows by mutableStateOf((1..60).map(::message))
        compose.setContent{PeerChatTheme(true){ConversationScreen(
            ConversationUi("chat","Chat",ChatKind.Direct,true,"","",messages=rows),PeerChatUiState(),{})}}
        compose.onNodeWithText("Message number 60").assertIsDisplayed()
        compose.runOnIdle{rows=rows+message(61)}
        compose.onNodeWithText("Message number 61").assertIsDisplayed()
        compose.onNodeWithTag("messages").performScrollToIndex(10)
        compose.onNodeWithTag("jump-to-latest").assertIsDisplayed()
        compose.runOnIdle{rows=rows+message(62)}
        compose.onNodeWithText("Message number 62").assertDoesNotExist()
        compose.onNodeWithTag("jump-to-latest").performClick()
        compose.onNodeWithText("Message number 62").assertIsDisplayed()
    }
    @Test fun sendingFromOlderReadingReturnsToLatestWithoutChangingDraftSemantics() {
        val rows=(1..60).map(::message)
        var sent=false
        compose.setContent{PeerChatTheme(true){ConversationScreen(
            ConversationUi("chat","Chat",ChatKind.Direct,true,"","",messages=rows),PeerChatUiState(),{if(it is UiAction.PreviewMessage)sent=true})}}
        compose.onNodeWithTag("messages").performScrollToIndex(10)
        compose.onNodeWithTag("composer").performTextInput("Send while reading older")
        compose.onNodeWithContentDescription("Send local preview").performClick()
        compose.onNodeWithText("Message number 60").assertIsDisplayed()
        compose.runOnIdle{assertTrue(sent)}
    }
    @Test fun nearBottomThresholdIsBoundedAndTreatsInitialLayoutAsFollowing() {
        assertTrue(conversationNearLatest(null,62));assertTrue(conversationNearLatest(59,62))
        assertFalse(conversationNearLatest(58,62));assertTrue(conversationNearLatest(null,0))
    }
    @Test fun viewportShrinkFollowsLatestButPrependingHistoryPreservesOlderAnchor() {
        var rows by mutableStateOf((1..60).map(::message));var height by mutableIntStateOf(700)
        compose.setContent{PeerChatTheme(true){Box(Modifier.height(height.dp)){ConversationScreen(
            ConversationUi("chat","Chat",ChatKind.Direct,true,"","",messages=rows),PeerChatUiState(),{})}}}
        compose.onNodeWithText("Message number 60").assertIsDisplayed()
        compose.runOnIdle{height=450}
        compose.onNodeWithText("Message number 60").assertIsDisplayed()
        compose.onNodeWithTag("messages").performScrollToIndex(10)
        compose.onNodeWithText("Message number 9").assertIsDisplayed()
        compose.runOnIdle{rows=(-20..0).map(::message)+rows}
        compose.onNodeWithText("Message number 9").assertIsDisplayed()
        compose.onNodeWithTag("jump-to-latest").assertIsDisplayed()
        compose.onNodeWithText("Message number 60").assertDoesNotExist()
    }
}
