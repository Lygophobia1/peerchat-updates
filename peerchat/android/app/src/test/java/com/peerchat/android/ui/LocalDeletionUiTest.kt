package com.peerchat.android.ui

import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.runtime.mutableStateOf
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class LocalDeletionUiTest {
    @get:Rule val compose=createComposeRule()
    private val chat=ConversationUi("account:8","Sammy",ChatKind.Direct,true,"Private","10:00",hasOlder=false)

    @Test fun chatLongPressConfirmsDeviceOnlyHistoryDeletion(){
        val actions=mutableListOf<UiAction>()
        compose.setContent{PeerChatTheme(true){Home(PeerChatUiState(demo=false,route=Route.Home,connection=ConnectionState.Online,
            load=LoadState.Ready,conversations=listOf(chat)),actions::add)}}
        compose.onNodeWithTag("conversation-row-${chat.id}").performTouchInput{longClick()}
        compose.onNodeWithText("Delete conversation history from this device?").assertExists()
        compose.runOnIdle{assertTrue(actions.none{it is UiAction.DeleteLocalConversation})}
        compose.onNodeWithTag("confirm-delete-conversation").performClick()
        compose.runOnIdle{assertEquals(UiAction.DeleteLocalConversation(chat.id),actions.last())}
    }
    @Test fun accountChangeClosesOldConversationDeleteConfirmation(){
        val actions=mutableListOf<UiAction>()
        val account=mutableStateOf("account:19")
        compose.setContent{PeerChatTheme(true){Home(PeerChatUiState(demo=false,route=Route.Home,connection=ConnectionState.Online,
            load=LoadState.Ready,account=AccountUi(account.value,"Me"),conversations=listOf(chat)),actions::add)}}
        compose.onNodeWithTag("conversation-row-${chat.id}").performTouchInput{longClick()}
        compose.onNodeWithText("Delete conversation history from this device?").assertExists()
        compose.runOnIdle{account.value="account:20"}
        compose.onNodeWithText("Delete conversation history from this device?").assertDoesNotExist()
        compose.runOnIdle{assertTrue(actions.none{it is UiAction.DeleteLocalConversation})}
    }

    @Test fun messageLongPressDeletesOnlyLocalCopyAndSingleTapStillExpands(){
        val actions=mutableListOf<UiAction>()
        val message=MessageUi("a".repeat(32),"local","Me","Private",true,"10:00",timestamp=100)
        val conversation=chat.copy(messages=listOf(message))
        compose.setContent{PeerChatTheme(true){ConversationScreen(conversation,PeerChatUiState(demo=false,route=Route.Conversation,
            selectedId=conversation.id,connection=ConnectionState.Online,conversations=listOf(conversation)),actions::add)}}
        compose.onNodeWithTag("message-bubble-${message.id}").performClick()
        compose.onNodeWithText("Aggregate status: Received").assertExists()
        compose.onNodeWithTag("message-bubble-${message.id}").performTouchInput{longClick()}
        compose.onNodeWithText("Delete message from this device?").assertExists()
        compose.onNodeWithText("Delete from this device").performClick()
        compose.runOnIdle{assertEquals(UiAction.DeleteLocalMessage(conversation.id,message.id),actions.last())}
    }
}
