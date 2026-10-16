package com.peerchat.android.ui

import androidx.compose.runtime.*
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.peerchat.android.application.*
import org.junit.*
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34],qualifiers="w360dp-h800dp-port")
class ContactsUiTest {
    @get:Rule val compose=createComposeRule()
    @Test fun chatsButtonOpensAllContactDestinationsAndBackReturnsHome(){
        var state by mutableStateOf(PeerChatUiState(demo=false,route=Route.Home))
        compose.setContent{PeerChatTheme(true){PeerChatShell(state,{state=reduceUi(state,it)})}}
        compose.onNodeWithTag("chats-add").assertIsDisplayed().performClick()
        compose.onNodeWithText("Find People / Add Friend").assertIsDisplayed()
        compose.onNodeWithText("Incoming Friend Requests (0)").assertIsDisplayed()
        compose.onNodeWithText("Outgoing Friend Requests (0)").assertIsDisplayed()
        compose.onNodeWithText("Group Invitations (0)").assertIsDisplayed()
        compose.onNodeWithText("Start New Chat").assertIsDisplayed().performClick()
        compose.runOnIdle{assertEquals(ContactPage.NewChat,state.contactPage);state=reduceUi(state,UiAction.Back);assertEquals(ContactPage.Menu,state.contactPage);state=reduceUi(state,UiAction.Back);assertEquals(Route.Home,state.route)}
    }
    @Test fun discoveryUsesRelationshipStateAndOfflineDisablesMutation(){
        var state by mutableStateOf(PeerChatUiState(demo=false,route=Route.Contacts,contactPage=ContactPage.FindPeople,
            peopleQuery="sam",peopleResults=listOf(DirectoryAccountSnapshot(3,"sam-friend",true),DirectoryAccountSnapshot(4,"sam-pending",false),DirectoryAccountSnapshot(5,"sam-new",true)),
            outgoingRequests=listOf(MemberUi("account:4","sam-pending",false)),
            conversations=listOf(ConversationUi("account:3","sam-friend",ChatKind.Direct,true,"",""))))
        compose.setContent{PeerChatTheme(false){ContactsScreen(state,{})}}
        compose.onNodeWithText("Open chat").assertExists()
        compose.onNodeWithText("Request pending").assertExists()
        compose.onNodeWithText("Add friend").performScrollTo().assertIsEnabled()
        compose.runOnIdle{state=state.copy(connection=ConnectionState.Offline)}
        compose.onNodeWithText("Add friend").performScrollTo().assertIsNotEnabled()
    }
    @Test fun directoryQueryRulesAreBoundedAndAvoidAccountListFetch(){
        assertFalse(validPeopleQuery(""));assertFalse(validPeopleQuery("a"));assertFalse(validPeopleQuery("bad space"))
        assertFalse(validPeopleQuery("a".repeat(33)));assertTrue(validPeopleQuery("sA"));assertTrue(validPeopleQuery("7"))
    }
}
