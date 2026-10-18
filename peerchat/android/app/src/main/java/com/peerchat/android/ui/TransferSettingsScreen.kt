package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.peerchat.android.application.TransferSettingsSnapshot
import com.peerchat.android.platform.AttachmentSendMode
import java.math.BigDecimal

internal fun transferMbps(bits:Long):String=BigDecimal.valueOf(bits).movePointLeft(6).stripTrailingZeros().toPlainString()
internal fun transferBits(preset:String,custom:String):Long?=when(preset){
    "Unlimited"->0L;"10 Mbps"->10_000_000L;"5 Mbps"->5_000_000L
    else->try{BigDecimal(custom).movePointRight(6).longValueExact().takeIf{it in 1..10_000_000_000L}}catch(_:Exception){null}
}
private fun transferPreset(bits:Long)=when(bits){0L->"Unlimited";10_000_000L->"10 Mbps";5_000_000L->"5 Mbps";else->"Custom"}

@OptIn(ExperimentalMaterial3Api::class,ExperimentalLayoutApi::class)
@Composable internal fun TransferSettingsScreen(state:PeerChatUiState,dispatch:(UiAction)->Unit){
    Scaffold(topBar={PageBar("File Transfer Settings",dispatch)}){padding->
        Column(Modifier.fillMaxSize().padding(padding).imePadding().verticalScroll(rememberScrollState()).padding(20.dp),verticalArrangement=Arrangement.spacedBy(16.dp)){
            state.transferSettingsError?.let{Text(it,color=MaterialTheme.colorScheme.error)}
            state.notice?.let{Text(it)}
            if(state.transferSettingsBusy)LinearProgressIndicator(Modifier.fillMaxWidth())
            Text("Default DM file sending mode",style=MaterialTheme.typography.titleLarge)
            Text("Direct sends to a verified online device, using the authenticated relay when needed. Cached stores encrypted content for authorized devices to retrieve later. Incoming files of either type are handled automatically.",style=MaterialTheme.typography.bodySmall)
            FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                FilterChip(state.attachmentSendMode==AttachmentSendMode.Direct,
                    {dispatch(UiAction.SaveAttachmentSendMode(AttachmentSendMode.Direct))},label={Text("Direct transfer")},
                    modifier=Modifier.testTag("default-direct"))
                FilterChip(state.attachmentSendMode==AttachmentSendMode.Cached,
                    {dispatch(UiAction.SaveAttachmentSendMode(AttachmentSendMode.Cached))},label={Text("Cached transfer")},
                    modifier=Modifier.testTag("default-cached"))
            }
            Text("Choose either mode for one DM file from the conversation attachment menu without changing this default.",style=MaterialTheme.typography.bodySmall)
            val saved=state.transferSettings
            if(saved==null){
                Text("Loading your saved transfer preferences…")
                if(!state.transferSettingsBusy)TextButton({dispatch(UiAction.OpenTransferSettings)}){Text("Retry")}
            }else{
                var uploadPreset by remember(saved){mutableStateOf(transferPreset(saved.uploadBitsPerSecond))}
                var downloadPreset by remember(saved){mutableStateOf(transferPreset(saved.downloadBitsPerSecond))}
                var uploadCustom by remember(saved){mutableStateOf(transferMbps(saved.uploadBitsPerSecond))}
                var downloadCustom by remember(saved){mutableStateOf(transferMbps(saved.downloadBitsPerSecond))}
                var chunk by remember(saved){mutableIntStateOf(saved.preferredChunkBytes)}
                var window by remember(saved){mutableIntStateOf(saved.preferredWindow)}
                Text("Upload and download limits",style=MaterialTheme.typography.titleLarge)
                Text("Mbps means megabits per second. 10 Mbps is about 1.25 MB/s. Unlimited removes the application's speed cap; network and receiver limits still apply.",style=MaterialTheme.typography.bodySmall)
                TransferLimit("Upload",uploadPreset,uploadCustom,!state.transferSettingsBusy,{uploadPreset=it},{uploadCustom=it})
                TransferLimit("Download",downloadPreset,downloadCustom,!state.transferSettingsBusy,{downloadPreset=it},{downloadCustom=it})
                Text("Preferred negotiated capability",style=MaterialTheme.typography.titleMedium)
                Text("Chunk size")
                FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp)){
                    listOf(32768,65536).forEach{value->FilterChip(chunk==value,{chunk=value},enabled=!state.transferSettingsBusy,label={Text("${value/1024} KiB")})}
                }
                Text("Maximum outstanding chunks")
                FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp)){
                    listOf(1,4,8,16).forEach{value->FilterChip(window==value,{window=value},enabled=!state.transferSettingsBusy,label={Text(value.toString())})}
                }
                Text("64 KiB and 16 chunks provide the optimized capability. Peers negotiate compatible settings; older clients retain their supported fallback. Saved limits apply globally to this profile on Wi-Fi and cellular.",style=MaterialTheme.typography.bodySmall)
                val up=transferBits(uploadPreset,uploadCustom);val down=transferBits(downloadPreset,downloadCustom)
                if(up==null||down==null)Text("Enter a positive custom speed up to 10,000 Mbps (at least 1 bit/s).",color=MaterialTheme.colorScheme.error)
                Button({dispatch(UiAction.SaveTransferSettings(TransferSettingsSnapshot(chunk,window,checkNotNull(up),checkNotNull(down))))},
                    enabled=!state.transferSettingsBusy&&up!=null&&down!=null,modifier=Modifier.fillMaxWidth().testTag("save-transfer-settings")){Text("Save transfer settings")}
                Text("Changes are saved only when you tap Save.",style=MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable private fun TransferLimit(label:String,preset:String,custom:String,enabled:Boolean,select:(String)->Unit,edit:(String)->Unit){
    Text(label,style=MaterialTheme.typography.titleMedium)
    FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp)){
        listOf("Unlimited","10 Mbps","5 Mbps","Custom").forEach{choice->FilterChip(preset==choice,{select(choice)},enabled=enabled,label={Text(choice)},modifier=Modifier.testTag("${label.lowercase()}-$choice"))}
    }
    if(preset=="Custom")OutlinedTextField(custom,{edit(it.take(24))},label={Text("$label custom Mbps")},singleLine=true,enabled=enabled,
        keyboardOptions=KeyboardOptions(keyboardType=KeyboardType.Decimal),modifier=Modifier.fillMaxWidth().testTag("${label.lowercase()}-custom"))
}
