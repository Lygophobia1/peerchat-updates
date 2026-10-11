package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.peerchat.android.application.*

private data class PendingArchiveConsent(val context:ArchiveEnrollmentSnapshot,val action:ArchiveDeviceAction,val device:String,val read:Boolean=false,val serve:Boolean=false)

@Composable internal fun ArchiveDevicesSection(state:PeerChatUiState,dispatch:(UiAction)->Unit){
    val enrollment=state.archiveEnrollment
    var pending by remember(enrollment?.accountId,enrollment?.authenticationGeneration){mutableStateOf<PendingArchiveConsent?>(null)}
    var managed by remember(enrollment?.accountId,enrollment?.authenticationGeneration){mutableStateOf<String?>(null)}
    var advanced by remember {mutableStateOf(false)}
    // One bounded existing application command at sign-in/reconnect, not a UI retry owner.
    LaunchedEffect(state.account?.accountId,state.connection){
        if(!state.demo&&state.account!=null&&state.connection==ConnectionState.Online&&enrollment==null&&!state.archiveBusy)dispatch(UiAction.ArchiveRefresh)
    }
    Text("History Sync",style=MaterialTheme.typography.titleMedium)
    Row(horizontalArrangement=Arrangement.spacedBy(12.dp)){
        Switch(enrollment?.historySyncEnabled?:false,{value->enrollment?.let{dispatch(UiAction.HistorySync(it,value))}},enabled=enrollment!=null&&!state.archiveBusy)
        Text("Keep approved devices up to date automatically.")
    }
    Text("Existing custom permissions and automatic-sync preferences are preserved. Offline Messages is a separate setting.",style=MaterialTheme.typography.bodySmall)
    Text("Sync Status",style=MaterialTheme.typography.titleMedium)
    val progress=state.archiveProgress
    Text("Phase: "+progress.displayState+" · Source: "+if(progress.source.isEmpty())"No source selected" else "Device "+progress.source.take(12)+"…")
    Text("${progress.received} new messages received this session · ${progress.conversations} conversations completed",style=MaterialTheme.typography.bodySmall)
    Text("Last successful sync: "+if(progress.lastSuccessful==0L)"Not recorded" else java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.SHORT,java.text.DateFormat.SHORT).format(java.util.Date(progress.lastSuccessful)),style=MaterialTheme.typography.bodySmall)
    if(progress.running)LinearProgressIndicator(Modifier.fillMaxWidth()) // Native has no reliable total.
    if(state.archiveBusy)Text("Verifying device metadata…")
    if(progress.displayState in setOf("Error","Waiting","Paused")||enrollment==null)OutlinedButton({
        val source=enrollment?.devices?.firstOrNull{!it.local&&it.mayServe}
        if(enrollment!=null&&source!=null)dispatch(UiAction.ArchiveSynchronize(enrollment,source.peerId))else dispatch(UiAction.ArchiveRefresh)
    },enabled=!state.archiveBusy&&state.account!=null){Text("Retry")}
    OutlinedButton({managed=enrollment?.devices?.firstOrNull{!it.local&&!it.authorized}?.peerId},enabled=enrollment!=null&&!state.archiveBusy&&enrollment.devices.any{!it.local&&!it.authorized}){Text("Add device")}
    for(local in listOf(true,false)){
        Text(if(local)"This Device" else "Other Devices",style=MaterialTheme.typography.titleMedium)
        val devices=enrollment?.devices?.filter{it.local==local}.orEmpty()
        if(devices.isEmpty())Text(if(local)"Sign in to verify this device." else "No other registered devices.",style=MaterialTheme.typography.bodySmall)
        devices.forEach {device->
            OutlinedCard(Modifier.fillMaxWidth()){
                Column(Modifier.padding(12.dp),verticalArrangement=Arrangement.spacedBy(6.dp)){
                    Text(if(device.local)"This device" else "Device "+device.peerId.take(12)+"…",style=MaterialTheme.typography.titleSmall)
                    Text(if(device.mayRead||device.mayServe)"Verified history access" else if(device.authorized)"Registered grant · review trust and permissions" else if(device.revoked)"Access revoked" else "Approval required",style=MaterialTheme.typography.bodySmall)
                    Text(if(!device.local&&device.peerId==progress.source&&progress.running&&progress.displayState=="Syncing")"Available · transferring verified history" else "Availability unknown",style=MaterialTheme.typography.bodySmall)
                    Row{
                        TextButton({managed=device.peerId},enabled=!state.archiveBusy){Text(if(device.authorized)"Manage" else "Approve device")}
                        if(!device.local&&device.mayServe)TextButton({dispatch(UiAction.ArchiveSynchronize(checkNotNull(enrollment),device.peerId))},enabled=!state.archiveBusy&&enrollment?.historySyncEnabled==true&&!progress.running){Text("Sync now")}
                    }
                }
            }
        }
    }
    TextButton({advanced=!advanced}){Text("Advanced history settings")}
    if(advanced){
        Text("Custom permissions are changed only after explicit confirmation. Signing in and nicknames never grant access.",style=MaterialTheme.typography.bodySmall)
        OutlinedButton({dispatch(UiAction.ArchiveRefresh)},enabled=!state.archiveBusy&&state.account!=null){Text("Refresh history devices")}
        Text(progress.state+" · ${progress.processed} records processed · ${progress.attachments} attachment records (not downloaded)",style=MaterialTheme.typography.bodySmall)
        if(enrollment!=null&&progress.running)TextButton({dispatch(UiAction.ArchiveSynchronize(enrollment,pause=true))},enabled=!state.archiveBusy){Text("Pause synchronization")}
    }
    val selected=enrollment?.devices?.firstOrNull{it.peerId==managed}
    if(selected!=null&&enrollment!=null){
        var custom by remember(selected.peerId){mutableStateOf(false)}
        var read by remember(selected.peerId,selected.grantedRead){mutableStateOf(if(selected.authorized)selected.grantedRead else true)}
        var serve by remember(selected.peerId,selected.grantedServe){mutableStateOf(if(selected.authorized)selected.grantedServe else true)}
        AlertDialog(onDismissRequest={managed=null},title={Text(if(selected.authorized)"Manage history device" else "New history device")},text={Column(verticalArrangement=Arrangement.spacedBy(6.dp)){
            Text(if(selected.local)"This device" else "Device "+selected.peerId.take(12)+"…")
            Text(selected.authorizationState)
            TextButton({custom=!custom}){Text("Advanced")}
            if(custom){
                Text("Peer ID: "+selected.peerId,style=MaterialTheme.typography.bodySmall)
                Text("Trusted authorizer: "+selected.approvedIssuer,style=MaterialTheme.typography.bodySmall)
                Row{Switch(read,{read=it},enabled=!state.archiveBusy);Text("Receive history")}
                Row{Switch(serve,{serve=it},enabled=!state.archiveBusy);Text("Provide history")}
                if(!read&&!serve)Text("At least one history permission is required while this device is authorized. Revoke the device if you want to disable history access completely.")
                TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.TrustAuthorizer,selected.peerId)},enabled=!state.archiveBusy){Text("Trust history authorizer")}
                TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.DisableAuthorizer,selected.peerId)},enabled=!state.archiveBusy){Text("Disable authorizer")}
                TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.Revoke,selected.peerId)},enabled=!state.archiveBusy){Text("Revoke device")}
            }
        }},confirmButton={TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.Authorize,selected.peerId,read,serve)},enabled=!state.archiveBusy&&(read||serve)&&(!selected.authorized||read!=selected.grantedRead||serve!=selected.grantedServe)){Text(if(selected.authorized)"Save custom permissions" else "Authorize device")}},dismissButton={TextButton({managed=null}){Text("Close")}})
    }
    pending?.let {p->
        AlertDialog(onDismissRequest={pending=null},title={Text("Confirm history authorization")},text={
            Text("Account "+p.context.accountId+"\nDevice Peer ID:\n"+p.device+"\n\n"+when(p.action){
                ArchiveDeviceAction.TrustAuthorizer->"Trust this device to sign history grants?"
                ArchiveDeviceAction.Authorize->"Authorize selected permissions: receive="+p.read+", provide="+p.serve+"?"
                ArchiveDeviceAction.Revoke->"Revoke this device's access granted by the local authorizer?"
                ArchiveDeviceAction.DisableAuthorizer->"Disable this history authorizer locally?"
            }+"\n\nSigning identities are unchanged. Already received history is not erased.")
        },confirmButton={TextButton({dispatch(UiAction.ArchiveConsent(p.context,p.action,p.device,p.read,p.serve));pending=null;managed=null}){Text("Confirm")}},dismissButton={TextButton({pending=null}){Text("Cancel")}})
    }
}
