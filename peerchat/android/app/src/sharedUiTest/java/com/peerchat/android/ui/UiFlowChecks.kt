package com.peerchat.android.ui

import androidx.compose.runtime.*
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.graphics.Color
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Rule
import org.junit.Test

/** UI-only: does not create native handles, identities, aliases or secure records. */
abstract class UiFlowChecks {
    @get:Rule val compose = createComposeRule()
    @Test fun diagnosticsScrollsAndCopiesTheFullSafeReport(){
        val safe=(1..180).joinToString("\n"){"archive_transport attempt=$it stage=archive_control"}+"\nlast_safe_record"
        var state by mutableStateOf(PeerChatUiState(demo=false,route=Route.Diagnostics,diagnostics=safe))
        var clipboard:androidx.compose.ui.platform.ClipboardManager?=null
        compose.setContent{clipboard=androidx.compose.ui.platform.LocalClipboardManager.current;PeerChatTheme(true){PeerChatShell(state,{})}}
        compose.onNodeWithTag("diagnostics-copy").assertIsDisplayed().performClick()
        compose.runOnIdle{assertEquals(diagnosticReport(state),clipboard!!.getText()!!.text)}
        compose.onNodeWithTag("diagnostics-end").performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag("diagnostics-copy").assertIsDisplayed()
        compose.runOnIdle{state=state.copy(diagnostics=safe+"\nupdated_safe_record")}
        compose.onNodeWithTag("diagnostics-copy").performClick()
        compose.runOnIdle{assertEquals(diagnosticReport(state),clipboard!!.getText()!!.text)}
    }
    @Test fun stalledSignInKeepsBothRecoveryActionsEnabled() {
        var action:UiAction?=null
        compose.setContent {PeerChatTheme(true){PeerChatShell(PeerChatUiState(demo=false,route=Route.Login,loginBusy=true,startupStatus=null),{action=it})}}
        compose.onNodeWithTag("sign-in-submit").assertIsNotEnabled()
        compose.onNodeWithTag("login-reset-network").performScrollTo().assertIsEnabled().performClick()
        compose.runOnIdle{assertEquals(UiAction.ResetNetworkSession,action)}
        compose.onNodeWithTag("login-change-server").performScrollTo().assertIsEnabled().performClick()
        compose.runOnIdle{assertEquals(UiAction.DisconnectChangeServer,action)}
    }
    @Test fun serverSettingsRefreshesSavedPinAndDistinguishesCertificateChange() {
        var state by mutableStateOf(PeerChatUiState(route=Route.Server,
            server=ServerUi("server.example","7443")))
        compose.setContent { PeerChatTheme(true) { PeerChatShell(state,{}) } }
        compose.onNodeWithTag("server-trust-status").assertTextEquals("Server trust: Untrusted")
        compose.runOnIdle {state=state.copy(server=state.server.copy(certificatePin="a".repeat(64)))}
        compose.onNodeWithTag("server-trust-status").assertTextEquals("Server trust: Trusted")
        compose.onNodeWithText("Advanced: enter certificate pin manually").performScrollTo().performClick()
        compose.onNodeWithText("a".repeat(64)).assertExists()
        compose.runOnIdle {state=state.copy(serverIdentityChanged=state.server)}
        compose.onNodeWithTag("server-trust-status").assertTextEquals("Server trust: Certificate changed")
    }
    @Test fun onlineColorUsesPresenceAndComposerRemainsVisibleWhileEditing() {
        var onlineColor=Color.Unspecified;var offlineColor=Color.Unspecified
        compose.setContent { PeerChatTheme(true) {
            onlineColor=presenceColor(true);offlineColor=presenceColor(false)
            val state=PeerChatUiState(route=Route.Conversation,selectedId="demo-dm-sammy",conversations=DemoConversations().initialSnapshot())
            ConversationScreen(state.selected!!,state,{})
        } }
        compose.runOnIdle {
            assertEquals(Color(0xFF4ADE80),onlineColor)
            assertNotEquals(onlineColor,offlineColor)
        }
        compose.onNodeWithTag("composer").assertIsDisplayed().performClick().performTextInput("Inset check")
        compose.onNodeWithTag("composer").assertIsDisplayed().assertTextContains("Inset check")
        compose.onNodeWithContentDescription("Send local preview").assertIsDisplayed().assertIsEnabled()
    }
    private fun launch(initial: PeerChatUiState = PeerChatUiState(conversations = DemoConversations().initialSnapshot())) {
        compose.setContent {
            var state by remember { mutableStateOf(initial) }
            PeerChatTheme(true) { PeerChatShell(state, { state = reduceUi(state, it) }, { _, _ ->
                state = state.copy(loginError = "Secure backend not connected")
            }) }
        }
    }
    @Test fun welcomeLoginNoFakeSuccessAndServerTrust() {
        launch()
        compose.onNodeWithText("Sign in").performClick()
        compose.onNodeWithTag("username").performTextInput("demo")
        compose.onNodeWithTag("password").performTextInput("test-only")
        compose.onNodeWithText("Show").performClick()
        compose.onNodeWithText("Hide").assertExists()
        compose.onNodeWithTag("sign-in-submit").performScrollTo().performClick()
        compose.onNodeWithTag("login-error").assertTextContains("not connected", substring = true)
        compose.onNodeWithText("Server settings").performScrollTo().performClick()
        compose.onNodeWithText("Trust/pin: Not established", substring = true).assertExists()
    }
    @Test fun chatsGroupsDetailsAndOfflineComposer() {
        launch()
        compose.onNodeWithTag("explore").performClick()
        compose.onNodeWithText("Sammy").performClick()
        compose.onNodeWithTag("composer").performTextInput("Local UI hello 👋")
        compose.onNodeWithContentDescription("Send local preview").performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithText("Local UI hello 👋").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithText("Local preview · not sent", substring = true).assertExists()
        compose.onNodeWithContentDescription("Conversation details").performClick()
        compose.onNodeWithText("Offline", useUnmergedTree = true).performScrollTo().performClick()
        compose.onNodeWithContentDescription("Back").performClick()
        compose.onNodeWithTag("composer").assertIsNotEnabled()
        compose.onNodeWithText("Offline preview", substring = true).assertExists()
        compose.onNodeWithContentDescription("Back").performClick()
        compose.onNodeWithText("Groups").performClick()
        compose.onNodeWithText("Gaming Group").performClick()
        compose.onNodeWithContentDescription("Conversation details").performClick()
        compose.onNodeWithText("Group details").assertExists()
        compose.onAllNodesWithText("Join Voice (preview)").onFirst().performScrollTo().performClick()
        compose.onNodeWithText("Leave Voice (preview)").assertExists()
        compose.onNodeWithText("Watch (preview)").performScrollTo().performClick()
        compose.onNodeWithText("Stop Watching (preview)").assertExists()
        compose.onNodeWithContentDescription("Back").performClick()
        compose.onNodeWithText("Joined-state preview only", substring = true).assertExists()
        compose.onNodeWithText("Watching Sammy", substring = true).assertExists()
    }
    @Test fun loadingErrorEmptyAndSearch() {
        launch(PeerChatUiState(route = Route.Home, conversations = DemoConversations().initialSnapshot()))
        compose.onNodeWithContentDescription("Preview states").performClick()
        compose.onNodeWithText("Preview: Loading").performClick()
        compose.onNodeWithText("Loading conversations…").assertExists()
        compose.onNodeWithContentDescription("Preview states").performClick()
        compose.onNodeWithText("Preview: Error").performClick()
        compose.onNodeWithText("Retry preview").performClick()
        compose.onNodeWithText("Sammy").assertExists()
        compose.onNodeWithContentDescription("Search conversations").performClick()
        compose.onNodeWithTag("search").performTextInput("no such person")
        compose.onNodeWithText("No matches").assertExists()
        compose.onNodeWithContentDescription("Preview states").performClick()
        compose.onNodeWithText("Preview: Empty").performClick()
        compose.onNodeWithText("No conversations yet").assertExists()
    }
    @Test fun loginFailurePreviewsAreExplicitAndNeverNavigateToChats() {
        launch()
        compose.onNodeWithText("Create account").performClick()
        compose.onNodeWithText("Create your account").assertExists()
        compose.onNodeWithTag("conversation-list").assertDoesNotExist()
        compose.onNodeWithTag("account-mode-switch").performScrollTo().performClick()
        compose.onNodeWithText("Preview sign-in errors").performScrollTo().performClick()
        compose.onNodeWithText("Server unreachable").performClick()
        compose.onNodeWithTag("login-error").assertTextContains("Preview only", substring = true)
        compose.onNodeWithTag("conversation-list").assertDoesNotExist()
    }
    @Test fun callsAndYouToolsHaveCorrectBackNavigation() {
        launch(PeerChatUiState(route = Route.Home, conversations = DemoConversations().initialSnapshot()))
        compose.onNodeWithText("Calls").performClick()
        compose.onNodeWithText("Preview incoming DM call").performScrollTo().performClick()
        compose.onNodeWithText("Answer").assertIsNotEnabled()
        compose.onNodeWithText("Dismiss preview").performClick()
        compose.onNodeWithText("You").performClick()
        compose.onNodeWithText("Light").performScrollTo().performClick().assertIsSelected()
        compose.onNodeWithText("Application diagnostics").performScrollTo().performClick()
        compose.onNodeWithText("UI demo — no live diagnostics").assertExists()
        compose.onNodeWithContentDescription("Back").performClick()
        compose.onNodeWithText("Your space").assertExists()
    }
    @Test fun manyMessagesDateGroupingStillShowsNewestAndComposer() {
        val c = DemoConversations().initialSnapshot().first().copy(messages = (0 until 300).map {
            // Give the synthetic sequence actual chronology: equal timestamps
            // intentionally use sender/ID ties rather than numeric ID suffixes.
            MessageUi("many-$it", "demo-sammy", "Sammy", "Message $it · 日本語 👋", false, "10:00", "Day ${it / 10}",
                timestamp=it.toLong()*60_000,logicalOrder=it.toLong()+1)
        })
        launch(PeerChatUiState(route = Route.Conversation, selectedId = c.id, conversations = listOf(c)))
        compose.onNodeWithText("Message 299 · 日本語 👋").assertIsDisplayed()
        compose.onNodeWithTag("composer").assertIsDisplayed()
    }
}
