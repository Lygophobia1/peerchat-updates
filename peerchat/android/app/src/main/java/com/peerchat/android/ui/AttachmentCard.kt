package com.peerchat.android.ui

import android.graphics.BitmapFactory
import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.peerchat.android.application.AttachmentSnapshot
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

// Decode bounds first, then sample. No whole-file byte array or unbounded
// full-resolution image allocation, including for hostile decompression sizes.
internal fun attachmentPicture(path:String,edge:Int):android.graphics.Bitmap? {
    if(path.isEmpty())return null
    val bounds=BitmapFactory.Options().apply{inJustDecodeBounds=true};BitmapFactory.decodeFile(path,bounds)
    if(bounds.outWidth<=0||bounds.outHeight<=0||bounds.outWidth.toLong()*bounds.outHeight>100_000_000)return null
    var sample=1;while(maxOf(bounds.outWidth,bounds.outHeight)/sample>edge)sample*=2
    return BitmapFactory.decodeFile(path,BitmapFactory.Options().apply{inSampleSize=sample})
}
internal fun attachmentPicture(context:android.content.Context,uri:Uri,edge:Int):android.graphics.Bitmap? = try {
    val bounds=BitmapFactory.Options().apply{inJustDecodeBounds=true}
    context.contentResolver.openInputStream(uri)?.use{BitmapFactory.decodeStream(it,null,bounds)}
    if(bounds.outWidth<=0||bounds.outHeight<=0||bounds.outWidth.toLong()*bounds.outHeight>100_000_000)null
    else {
        var sample=1;while(maxOf(bounds.outWidth,bounds.outHeight)/sample>edge)sample*=2
        context.contentResolver.openInputStream(uri)?.use{BitmapFactory.decodeStream(it,null,BitmapFactory.Options().apply{inSampleSize=sample})}
    }
}catch(_:Exception){null}
@Composable private fun rememberAttachmentPicture(path:String,edge:Int):State<android.graphics.Bitmap?> {
    val result=remember(path,edge){mutableStateOf<android.graphics.Bitmap?>(null)}
    LaunchedEffect(path,edge){
        if(path.isNotEmpty()){
            val decoded=withContext(Dispatchers.IO){attachmentPicture(path,edge)}
            withContext(Dispatchers.Main.immediate){result.value=decoded}
        }
    }
    return result
}
@Composable private fun rememberLocalAttachmentPicture(uri:Uri?,edge:Int):State<android.graphics.Bitmap?> {
    val context=LocalContext.current
    val result=remember(uri,edge){mutableStateOf<android.graphics.Bitmap?>(null)}
    LaunchedEffect(uri,edge){if(uri!=null){
        val decoded=withContext(Dispatchers.IO){attachmentPicture(context,uri,edge)}
        withContext(Dispatchers.Main.immediate){result.value=decoded}
    }}
    return result
}
internal fun attachmentThumbnailPicture(encoded:String):android.graphics.Bitmap? {
    if(encoded.length !in 4..2732)return null
    return try{
        val bytes=java.util.Base64.getDecoder().decode(encoded);if(bytes.size !in 16..2048)return null
        val bounds=BitmapFactory.Options().apply{inJustDecodeBounds=true}
        BitmapFactory.decodeByteArray(bytes,0,bytes.size,bounds)
        if(bounds.outMimeType!="image/jpeg"||bounds.outWidth !in 1..128||bounds.outHeight !in 1..128)null
        else BitmapFactory.decodeByteArray(bytes,0,bytes.size)
    }catch(_:Exception){null}
}
@Composable private fun rememberAttachmentThumbnail(bytes:String):State<android.graphics.Bitmap?> {
    val result=remember(bytes){mutableStateOf<android.graphics.Bitmap?>(null)}
    LaunchedEffect(bytes){if(bytes.isNotEmpty()){
        val decoded=withContext(Dispatchers.IO){attachmentThumbnailPicture(bytes)}
        withContext(Dispatchers.Main.immediate){result.value=decoded}
    }}
    return result
}
@OptIn(ExperimentalFoundationApi::class)
@Composable internal fun AttachmentCard(row:AttachmentSnapshot,dispatch:(UiAction)->Unit,preview:String?=null,localPreview:Uri?=null,stableIdentity:String=row.id,conversationId:String?=null) {
    val context=LocalContext.current;val scope=rememberCoroutineScope()
    var detailsExpanded by remember(stableIdentity){mutableStateOf(false)}
    var expanded by remember(stableIdentity){mutableStateOf(false)}
    var saveError by remember(stableIdentity){mutableStateOf(false)}
    var deletePrompt by remember(stableIdentity){mutableStateOf(false)}
    val bitmap by rememberAttachmentPicture(row.localPath,512)
    val localBitmap by rememberLocalAttachmentPicture(localPreview,512)
    val thumbnail by rememberAttachmentThumbnail(row.thumbnail)
    val picture=bitmap?:localBitmap?:thumbnail
    val imageFile=attachmentIsImage(row.name,picture!=null)||
        (row.id.startsWith("local-attachment-")&&localPreview!=null&&row.name=="Attachment")
    val screenWidth=LocalConfiguration.current.screenWidthDp
    val imageWidth=if(imageFile&&picture!=null)minOf(330f,(screenWidth-32).coerceAtLeast(120).toFloat(),460f*picture.width/picture.height.coerceAtLeast(1)).dp else 330.dp
    val longMessage=row.name=="message.peerchat-text"
    val textFile=longMessage||row.name.substringAfterLast('.',"").lowercase() in setOf("txt","c","h","cpp","hpp","java","kt","cs","py","json","md","xml","yaml","yml","log")
    LaunchedEffect(row.id,row.complete){if(longMessage&&(row.complete||row.outgoing))dispatch(UiAction.AttachmentPreview(row.id))}
    val save=androidx.activity.compose.rememberLauncherForActivityResult(androidx.activity.result.contract.ActivityResultContracts.CreateDocument("application/octet-stream")){uri->
        if(uri!=null)scope.launch{saveError=try{withContext(Dispatchers.IO){File(row.localPath).inputStream().use{input->context.contentResolver.openOutputStream(uri,"w").use{output->input.copyTo(checkNotNull(output),32768)}}};false}catch(_:Exception){true}}
    }
    Row(Modifier.fillMaxWidth(),horizontalArrangement=if(row.outgoing)Arrangement.End else Arrangement.Start){
        Surface(shape=MaterialTheme.shapes.large,color=if(row.outgoing)MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant,
            modifier=(if(imageFile)Modifier.width(imageWidth) else Modifier.widthIn(max=330.dp)).then(if(imageFile)Modifier else Modifier.combinedClickable(onClick={detailsExpanded=!detailsExpanded},onLongClick={if(conversationId!=null)deletePrompt=true},role=Role.Button)
                .semantics { contentDescription="Attachment details";stateDescription=if(detailsExpanded)"Expanded" else "Collapsed" })
                .testTag("attachment-${row.id}")){
            Column(if(imageFile)Modifier else Modifier.padding(12.dp),verticalArrangement=if(imageFile)Arrangement.Top else Arrangement.spacedBy(6.dp)){
                if(imageFile){
                    if(picture!=null)BoxWithConstraints(Modifier.fillMaxWidth()){
                        val height=(maxWidth.value*picture.height.toFloat()/picture.width.coerceAtLeast(1)).dp
                        Image(picture.asImageBitmap(),row.name,Modifier.fillMaxWidth().height(height).combinedClickable(onClick={expanded=true},onLongClick={if(conversationId!=null)deletePrompt=true})
                            .testTag("attachment-image-${row.id}"),contentScale=ContentScale.Fit)
                    }else Box(Modifier.fillMaxWidth().height(180.dp).combinedClickable(onClick={detailsExpanded=!detailsExpanded},onLongClick={if(conversationId!=null)deletePrompt=true}),contentAlignment=Alignment.Center){Text(if(row.active)"Preparing picture…" else "Picture preview unavailable",style=MaterialTheme.typography.labelMedium)}
                    val stage=imageAttachmentStage(row)
                    val percent=if(row.size>0&&row.done>0&&row.state in setOf(0,1,2,3,7))" · ${(row.done*100/row.size).coerceIn(0,100)}%" else ""
                    Row(Modifier.fillMaxWidth().combinedClickable(onClick={detailsExpanded=!detailsExpanded},onLongClick={if(conversationId!=null)deletePrompt=true})
                        .semantics { contentDescription="Attachment details";stateDescription=if(detailsExpanded)"Expanded" else "Collapsed" }
                        .testTag("attachment-footer-${row.id}").padding(horizontal=10.dp,vertical=7.dp),
                        verticalAlignment=Alignment.CenterVertically){
                        Text((if(row.size>0)android.text.format.Formatter.formatShortFileSize(context,row.size)+" · " else "")+stage+percent,
                            modifier=Modifier.weight(1f).testTag("attachment-summary-${row.id}"),style=MaterialTheme.typography.labelSmall,
                            color=if(row.state in setOf(5,6,8))MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onPrimaryContainer,
                        )
                        Text(if(detailsExpanded)"⌃" else "⌄",style=MaterialTheme.typography.titleMedium)
                    }
                }else{
                picture?.let{Image(it.asImageBitmap(),row.name,Modifier.fillMaxWidth().heightIn(max=200.dp).clickable{
                    if(bitmap!=null||localBitmap!=null)expanded=true
                    else if(row.awaitingAcceptance)dispatch(UiAction.AttachmentAction(row.id,true))
                    else detailsExpanded=true
                },contentScale=ContentScale.Fit)}
                Text(if(longMessage)"Long message" else row.name,style=MaterialTheme.typography.bodyMedium)
                Text(if(localPreview!=null&&row.id.startsWith("local-attachment-"))when(row.state){
                    5->"Cancelled before confirmation"
                    6->"Preparation failed · check history before retrying"
                    else->if(row.cachePhase=="Choose a delivery mode")"Choose a delivery mode" else "Preparing encrypted file…"
                } else attachmentCompactSummary(row),style=MaterialTheme.typography.labelSmall,
                    color=if(row.state==5||row.state==6)MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier=Modifier.testTag("attachment-summary-${row.id}"))
                if(attachmentShowsCompactProgress(row))LinearProgressIndicator(progress={if(row.size>0)(row.done.toFloat()/row.size.toFloat()).coerceIn(0f,1f)else 0f},modifier=Modifier.fillMaxWidth())
                if(preview!=null){
                    if(longMessage&&needsCompactMessage(preview))CompactLongMessage(preview)
                    else BoundedTextViewer(preview,!longMessage||codeLike(preview),if(longMessage)"Long message" else row.name)
                }
                }
                if(imageFile&&detailsExpanded)Text(row.name,Modifier.padding(horizontal=12.dp),style=MaterialTheme.typography.bodyMedium)
                if(detailsExpanded){
                    Text("Collapse details",modifier=(if(imageFile)Modifier.padding(horizontal=12.dp) else Modifier).testTag("attachment-details-toggle-${row.id}"),
                        style=MaterialTheme.typography.labelSmall,color=MaterialTheme.colorScheme.primary)
                    if(imageFile&&row.size>0)Text("File size: ${android.text.format.Formatter.formatShortFileSize(context,row.size)}",Modifier.padding(horizontal=12.dp),style=MaterialTheme.typography.labelSmall)
                    Text("Transfer state: ${if(imageFile)imageAttachmentStage(row) else attachmentCompactStatus(row)}",style=MaterialTheme.typography.labelSmall)
                    attachmentTransferPhaseText(row)?.let{Text(it,style=MaterialTheme.typography.labelSmall)}
                    if(row.cachePhase.isEmpty()&&attachmentHasCurrentTransferPhase(row))
                        Text(if(row.outgoing)"Direct transfer · selected device only" else "Direct transfer · this device only",style=MaterialTheme.typography.labelSmall)
                    Text(row.statisticsText,style=MaterialTheme.typography.labelSmall)
                    if(imageFile&&row.size>0&&row.active)LinearProgressIndicator(progress={if(row.size>0)(row.done.toFloat()/row.size.toFloat()).coerceIn(0f,1f)else 0f},modifier=Modifier.fillMaxWidth().padding(horizontal=12.dp))
                    row.recipients.forEach{recipient->
                        Text("Account ${recipient.account} · ${recipient.device.take(12)}: ${recipient.state} · ${recipient.done} / ${recipient.total} encrypted bytes"+
                            if(recipient.error.isEmpty())"" else " · ${recipient.error}",style=MaterialTheme.typography.labelSmall)
                    }
                    if(row.error.isNotEmpty())Text(row.error,style=MaterialTheme.typography.bodySmall,color=if(row.error=="Sender is preparing file")MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.error)
                } else if(!imageFile)Text("Tap for details",style=MaterialTheme.typography.labelSmall,color=MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier=Modifier.testTag("attachment-details-toggle-${row.id}"))
                if(!imageFile||detailsExpanded||row.active||row.state in setOf(5,6))Row(modifier=if(imageFile)Modifier.padding(horizontal=8.dp) else Modifier){
                    if(textFile&&preview==null&&(row.complete||longMessage&&row.outgoing))TextButton({dispatch(UiAction.AttachmentPreview(row.id))}){Text("Read text")}
                    if(row.awaitingAcceptance)TextButton({dispatch(UiAction.AttachmentAction(row.id,true))},Modifier.testTag("accept-file")){Text("Accept")}
                    if(row.id.startsWith("local-attachment-")&&row.active)TextButton({dispatch(UiAction.CancelLocalAttachment(row.id))}){Text("Cancel preparation")}
                    else if(row.id.startsWith("local-attachment-")&&row.state in setOf(5,6))TextButton({dispatch(UiAction.DismissLocalAttachment(row.id))}){Text("Dismiss")}
                    if(row.id.startsWith("local-attachment-")&&row.state==6)TextButton({dispatch(UiAction.RetryLocalAttachment(row.id))}){Text("Retry after checking history")}
                    else if(row.active&&!row.id.startsWith("local-attachment-"))TextButton({dispatch(UiAction.AttachmentAction(row.id,false))}){Text(if(row.awaitingAcceptance)"Decline" else "Cancel")}
                    if(row.complete&&row.localPath.isNotEmpty()){
                        if(bitmap!=null)TextButton({expanded=true}){Text("Open picture")}
                        TextButton({save.launch(row.name)}){Text("Save as")}
                    }
                }
                if(saveError)Text("Could not save this copy. The original is retained.",color=MaterialTheme.colorScheme.error)
            }
        }
    }
    if(expanded){
        val full by rememberAttachmentPicture(row.localPath,4096)
        val localFull by rememberLocalAttachmentPicture(localPreview,4096)
        androidx.compose.ui.window.Dialog(onDismissRequest={expanded=false},properties=androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth=false)){
            Surface(Modifier.fillMaxSize(),color=MaterialTheme.colorScheme.background){Column(Modifier.safeDrawingPadding()){
                TextButton({expanded=false}){Text("Close picture")}
                (localFull?:full?:picture)?.let{Image(it.asImageBitmap(),row.name,Modifier.fillMaxSize().testTag("attachment-full-image-${row.id}"),contentScale=ContentScale.Fit)}
            }}
        }
    }
    if(deletePrompt&&conversationId!=null)AlertDialog(onDismissRequest={deletePrompt=false},
        title={Text("Delete file from this device?")},
        text={Text("This removes the attachment from chat history on this device. Saved files are kept, and sending continues if already in progress. It does not unsend the file.")},
        confirmButton={TextButton({deletePrompt=false;dispatch(UiAction.DeleteLocalMessage(conversationId,row.id,attachment=true))}){Text("Delete from this device")}},
        dismissButton={TextButton({deletePrompt=false},Modifier.testTag("dismiss-local-file-delete")){Text("Cancel")}})
}
