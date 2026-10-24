package com.peerchat.android.ui

import android.graphics.Bitmap
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.longClick
import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.application.AttachmentSnapshot
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class ImageAttachmentCardTest {
    @get:Rule val compose=createComposeRule()

    private fun picture(width:Int,height:Int):Pair<File,Uri>{
        val context=ApplicationProvider.getApplicationContext<android.content.Context>()
        val file=File.createTempFile("peerchat-card-",".png",context.cacheDir)
        val bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888)
        bitmap.eraseColor(android.graphics.Color.CYAN)
        file.outputStream().use{bitmap.compress(Bitmap.CompressFormat.PNG,100,it)}
        bitmap.recycle()
        return file to Uri.fromFile(file)
    }
    private fun row(id:String,state:Int=0)=AttachmentSnapshot(id,"b".repeat(64),"photo.png",1024,state,true,0,1,"","")

    @Test fun portraitFillsTallImageAreaAndImageTapKeepsFooterCollapsed(){
        val (file,uri)=picture(120,240)
        try{
            val attachment=row("local-attachment-portrait")
            compose.setContent{PeerChatTheme(true){AttachmentCard(attachment,{},localPreview=uri)}}
            compose.waitUntil(5000){compose.onAllNodesWithTag("attachment-image-${attachment.id}").fetchSemanticsNodes().isNotEmpty()}
            val bounds=compose.onNodeWithTag("attachment-image-${attachment.id}").fetchSemanticsNode().boundsInRoot
            assertTrue(bounds.height>bounds.width)
            compose.onNodeWithText("photo.png").assertDoesNotExist()
            compose.onNodeWithTag("attachment-image-${attachment.id}").performClick()
            compose.waitUntil(5000){compose.onAllNodesWithTag("attachment-full-image-${attachment.id}").fetchSemanticsNodes().isNotEmpty()}
            compose.onNodeWithText("Close picture").performClick()
            compose.onNodeWithText("Transfer state: Preparing").assertDoesNotExist()
            compose.onNodeWithTag("attachment-footer-${attachment.id}").performClick()
            compose.onNodeWithText("Transfer state: Preparing").assertExists()
        }finally{file.delete()}
    }

    @Test fun landscapeFitsWideAreaAndFooterStagesNeverSayDelivered(){
        val (file,uri)=picture(240,120)
        try{
            val attachment=row("local-attachment-landscape",3).copy(done=512,cachePhase="Uploading encrypted file")
            compose.setContent{PeerChatTheme(true){AttachmentCard(attachment,{},localPreview=uri)}}
            compose.waitUntil(5000){compose.onAllNodesWithTag("attachment-image-${attachment.id}").fetchSemanticsNodes().isNotEmpty()}
            val bounds=compose.onNodeWithTag("attachment-image-${attachment.id}").fetchSemanticsNode().boundsInRoot
            assertTrue(bounds.width>bounds.height)
            compose.onNodeWithText("Uploading",substring=true).assertExists()
            compose.onNodeWithText("Delivered",substring=true).assertDoesNotExist()
        }finally{file.delete()}
    }

    @Test fun imageStageNamesRepresentTransferNotRecipientReceipt(){
        val base=row("c".repeat(32))
        assertEquals("Preparing",imageAttachmentStage(base))
        assertEquals("Uploading",imageAttachmentStage(base.copy(state=3,cachePhase="Uploading encrypted file")))
        assertEquals("Finalizing",imageAttachmentStage(base.copy(state=3,cachePhase="Upload complete; storing notices")))
        assertEquals("Sent",imageAttachmentStage(base.copy(state=4,done=1024)))
        assertEquals("Failed",imageAttachmentStage(base.copy(state=6)))
    }
    @Test fun photoAndFooterLongPressOfferLocalDeleteWithoutOpeningViewer(){
        val (file,uri)=picture(120,240)
        try{
            val attachment=row("local-attachment-delete")
            val actions=mutableListOf<UiAction>()
            compose.setContent{PeerChatTheme(true){AttachmentCard(attachment,actions::add,localPreview=uri,conversationId="account:8")}}
            compose.waitUntil(5000){compose.onAllNodesWithTag("attachment-image-${attachment.id}").fetchSemanticsNodes().isNotEmpty()}
            compose.onNodeWithTag("attachment-image-${attachment.id}").performTouchInput{longClick()}
            compose.onNodeWithText("Delete file from this device?").assertExists()
            compose.onNodeWithTag("attachment-full-image-${attachment.id}").assertDoesNotExist()
            compose.onNodeWithTag("dismiss-local-file-delete").performClick()
            compose.onNodeWithTag("attachment-footer-${attachment.id}").performTouchInput{longClick()}
            compose.onNodeWithText("Delete from this device").performClick()
            compose.runOnIdle{assertEquals(UiAction.DeleteLocalMessage("account:8",attachment.id,attachment=true),actions.last())}
        }finally{file.delete()}
    }
    @Test fun unknownPickedUriStartsAsImageShapedPlaceholderBeforeMetadataArrives(){
        val pending=AttachmentSnapshot("local-attachment-unidentified","","Attachment",0,0,true,0,1,"","")
        compose.setContent{PeerChatTheme(true){AttachmentCard(pending,{},localPreview=Uri.parse("file:///peerchat-test/no-image-before-metadata"))}}
        compose.onNodeWithTag("attachment-footer-${pending.id}").assertExists()
        compose.onNodeWithText("Preparing picture…",useUnmergedTree=true).assertExists()
    }
}
