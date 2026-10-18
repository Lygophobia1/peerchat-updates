package com.peerchat.android.ui

import android.net.Uri
import com.peerchat.android.platform.AttachmentSendMode
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class AttachmentPickerBindingTest {
    private val original=AttachmentPickerBinding("account:19","account:8",7,AttachmentSendMode.Cached)
    private val current=PeerChatUiState(demo=false,route=Route.Conversation,account=AccountUi("account:19","Jonah"),
        selectedId="account:8",attachmentContextGeneration=7)
    private val source=Uri.parse("content://fixture/selected")

    @Test fun resultKeepsTheModeAndConversationCapturedAtLaunch(){
        assertEquals(UiAction.SendAttachment(source,AttachmentSendMode.Cached,"account:8","account:19",7),
            attachmentPickerAction(source,original,current.copy(attachmentSendMode=AttachmentSendMode.Direct)))
    }
    @Test fun changedConversationAccountOrGenerationDropsPickerResult(){
        assertNull(attachmentPickerAction(source,original,current.copy(selectedId="account:9")))
        assertNull(attachmentPickerAction(source,original,current.copy(account=AccountUi("account:20","Another"))))
        assertNull(attachmentPickerAction(source,original,current.copy(attachmentContextGeneration=8)))
        assertNull(attachmentPickerAction(source,original,current.copy(route=Route.Home)))
        assertNull(attachmentPickerAction(source,null,current))
    }
}
