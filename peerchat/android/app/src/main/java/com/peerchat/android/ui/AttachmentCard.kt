package com.peerchat.android.ui

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
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
@Composable internal fun AttachmentCard(row:AttachmentSnapshot,dispatch:(UiAction)->Unit) {
    val context=LocalContext.current;val scope=rememberCoroutineScope()
    var expanded by remember(row.id){mutableStateOf(false)}
    var saveError by remember(row.id){mutableStateOf(false)}
    val bitmap by rememberAttachmentPicture(row.localPath,512)
    val save=androidx.activity.compose.rememberLauncherForActivityResult(androidx.activity.result.contract.ActivityResultContracts.CreateDocument("application/octet-stream")){uri->
        if(uri!=null)scope.launch{saveError=try{withContext(Dispatchers.IO){File(row.localPath).inputStream().use{input->context.contentResolver.openOutputStream(uri,"w").use{output->input.copyTo(checkNotNull(output),32768)}}};false}catch(_:Exception){true}}
    }
    Row(Modifier.fillMaxWidth(),horizontalArrangement=if(row.outgoing)Arrangement.End else Arrangement.Start){
        Surface(shape=MaterialTheme.shapes.large,color=if(row.outgoing)MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant,
            modifier=Modifier.widthIn(max=330.dp).fillMaxWidth(.9f).testTag("attachment-${row.id}")){
            Column(Modifier.padding(12.dp),verticalArrangement=Arrangement.spacedBy(6.dp)){
                bitmap?.let{Image(it.asImageBitmap(),row.name,Modifier.fillMaxWidth().heightIn(max=200.dp).clickable{expanded=true},contentScale=ContentScale.Fit)}
                Text(row.name,style=MaterialTheme.typography.bodyMedium)
                Text("${row.size} bytes · "+when(row.state){0->"Preparing";1->"Waiting for recipient";2->"File offered";3->"Transferring";4->"Transfer complete";5->"Cancelled";6->"Failed";7->"Recovering";else->"Suspended"},
                    style=MaterialTheme.typography.labelSmall)
                if(row.state==3&&row.size>0)LinearProgressIndicator(progress={row.done.toFloat()/row.size.toFloat()},modifier=Modifier.fillMaxWidth())
                if(row.error.isNotEmpty())Text(row.error,style=MaterialTheme.typography.bodySmall,color=MaterialTheme.colorScheme.error)
                Row{
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
