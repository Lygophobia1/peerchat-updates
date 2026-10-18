package com.peerchat.android.ui

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
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
@Composable private fun rememberAttachmentPicture(path:String,edge:Int):State<android.graphics.Bitmap?> {
    val result=remember(path,edge){mutableStateOf<android.graphics.Bitmap?>(null)}
    LaunchedEffect(path,edge){result.value=withContext(Dispatchers.IO){attachmentPicture(path,edge)}}
    return result
}
@Composable internal fun AttachmentCard(row:AttachmentSnapshot,dispatch:(UiAction)->Unit,preview:String?=null) {
    val context=LocalContext.current;val scope=rememberCoroutineScope()
    var detailsExpanded by remember(row.id){mutableStateOf(false)}
    var expanded by remember(row.id){mutableStateOf(false)}
    var saveError by remember(row.id){mutableStateOf(false)}
    val bitmap by rememberAttachmentPicture(row.localPath,512)
    val longMessage=row.name=="message.peerchat-text"
    val textFile=longMessage||row.name.substringAfterLast('.',"").lowercase() in setOf("txt","c","h","cpp","hpp","java","kt","cs","py","json","md","xml","yaml","yml","log")
    LaunchedEffect(row.id,row.complete){if(longMessage&&(row.complete||row.outgoing))dispatch(UiAction.AttachmentPreview(row.id))}
    val save=androidx.activity.compose.rememberLauncherForActivityResult(androidx.activity.result.contract.ActivityResultContracts.CreateDocument("application/octet-stream")){uri->
        if(uri!=null)scope.launch{saveError=try{withContext(Dispatchers.IO){File(row.localPath).inputStream().use{input->context.contentResolver.openOutputStream(uri,"w").use{output->input.copyTo(checkNotNull(output),32768)}}};false}catch(_:Exception){true}}
    }
    Row(Modifier.fillMaxWidth(),horizontalArrangement=if(row.outgoing)Arrangement.End else Arrangement.Start){
        Surface(shape=MaterialTheme.shapes.large,color=if(row.outgoing)MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant,
            modifier=Modifier.widthIn(max=330.dp)
                .toggleable(value=detailsExpanded,role=Role.Button,onValueChange={detailsExpanded=it})
                .semantics { contentDescription="Attachment details";stateDescription=if(detailsExpanded)"Expanded" else "Collapsed" }
                .testTag("attachment-${row.id}")){
            Column(Modifier.padding(12.dp),verticalArrangement=Arrangement.spacedBy(6.dp)){
                bitmap?.let{Image(it.asImageBitmap(),row.name,Modifier.fillMaxWidth().heightIn(max=200.dp).clickable{expanded=true},contentScale=ContentScale.Fit)}
                Text(if(longMessage)"Long message" else row.name,style=MaterialTheme.typography.bodyMedium)
                Text(attachmentCompactSummary(row),style=MaterialTheme.typography.labelSmall,
                    color=if(row.state==5||row.state==6)MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier=Modifier.testTag("attachment-summary-${row.id}"))
                if(attachmentShowsCompactProgress(row))LinearProgressIndicator(progress={if(row.size>0)(row.done.toFloat()/row.size.toFloat()).coerceIn(0f,1f)else 0f},modifier=Modifier.fillMaxWidth())
                if(preview!=null){
                    if(longMessage&&needsCompactMessage(preview))CompactLongMessage(preview)
                    else BoundedTextViewer(preview,!longMessage||codeLike(preview),if(longMessage)"Long message" else row.name)
                }
                if(detailsExpanded){
                    Text("Collapse details",style=MaterialTheme.typography.labelSmall,color=MaterialTheme.colorScheme.primary,
                        modifier=Modifier.testTag("attachment-details-toggle-${row.id}"))
                    Text("Transfer state: ${attachmentCompactStatus(row)}",style=MaterialTheme.typography.labelSmall)
                    attachmentTransferPhaseText(row)?.let{Text(it,style=MaterialTheme.typography.labelSmall)}
                    if(row.cachePhase.isEmpty()&&attachmentHasCurrentTransferPhase(row))
                        Text(if(row.outgoing)"Direct transfer · selected device only" else "Direct transfer · this device only",style=MaterialTheme.typography.labelSmall)
                    Text(row.statisticsText,style=MaterialTheme.typography.labelSmall)
                    if(row.error.isNotEmpty())Text(row.error,style=MaterialTheme.typography.bodySmall,color=MaterialTheme.colorScheme.error)
                } else Text("Tap for details",style=MaterialTheme.typography.labelSmall,color=MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier=Modifier.testTag("attachment-details-toggle-${row.id}"))
                Row{
                    if(textFile&&preview==null&&(row.complete||longMessage&&row.outgoing))TextButton({dispatch(UiAction.AttachmentPreview(row.id))}){Text("Read text")}
                    if(row.awaitingAcceptance)TextButton({dispatch(UiAction.AttachmentAction(row.id,true))},Modifier.testTag("accept-file")){Text("Accept")}
                    if(row.active)TextButton({dispatch(UiAction.AttachmentAction(row.id,false))}){Text(if(row.awaitingAcceptance)"Decline" else "Cancel")}
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
        androidx.compose.ui.window.Dialog(onDismissRequest={expanded=false},properties=androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth=false)){
            Surface(Modifier.fillMaxSize(),color=MaterialTheme.colorScheme.background){Column(Modifier.safeDrawingPadding()){
                TextButton({expanded=false}){Text("Close picture")}
                full?.let{Image(it.asImageBitmap(),row.name,Modifier.fillMaxSize(),contentScale=ContentScale.Fit)}
            }}
        }
    }
}
