package com.peerchat.android.ui

import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.platform.AttachmentSendMode
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class PhotoPickerTest {
    @Test fun systemPhotoPickerOnlyRequestsImages(){
        val context=ApplicationProvider.getApplicationContext<android.content.Context>()
        val intent=ActivityResultContracts.PickVisualMedia().createIntent(context,PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
        assertEquals("image/*",intent.type)
        assertFalse(intent.hasExtra(android.content.Intent.EXTRA_ALLOW_MULTIPLE))
    }
    @Test fun galleryResultUsesCacheAndCannotCrossConversationOrLogin(){
        val binding=AttachmentPickerBinding("account:13","account:14",7,AttachmentSendMode.Cached)
        val state=PeerChatUiState(demo=false,route=Route.Conversation,selectedId="account:14",account=AccountUi("account:13","codex"),attachmentContextGeneration=7)
        val uri=android.net.Uri.parse("content://media/photo/1")
        assertEquals(AttachmentSendMode.Cached,attachmentPickerAction(uri,binding,state)?.mode)
        assertNull(attachmentPickerAction(uri,binding,state.copy(selectedId="account:15")))
        assertNull(attachmentPickerAction(uri,binding,state.copy(attachmentContextGeneration=8)))
    }
}
