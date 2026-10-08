package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.peerchat.android.application.*

private data class PendingArchiveConsent(val context:ArchiveEnrollmentSnapshot,val action:ArchiveDeviceAction,val device:String,val read:Boolean=false,val serve:Boolean=false)

@Composable internal fun ArchiveDevicesSection(state:PeerChatUiState,dispatch:(UiAction)->Unit){
    var pending by remember {mutableStateOf<PendingArchiveConsent?>(null)}
    Text("Archive devices",style=MaterialTheme.typography.titleMedium)
    Text("Archive access is separate from signing in. Refresh on each device, compare Peer IDs, then explicitly trust an authorizer and grant only needed access. Revocation cannot erase history already received.",style=MaterialTheme.typography.bodySmall)
    OutlinedButton({dispatch(UiAction.ArchiveRefresh)},enabled=!state.archiveBusy&&state.account!=null){Text(if(state.archiveBusy)"Verifying…" else "Refresh archive devices")}
    val enrollment=state.archiveEnrollment
    if(enrollment!=null)Row {
        Switch(enrollment.historySyncEnabled,{value->dispatch(UiAction.HistorySync(enrollment,value))},enabled=!state.archiveBusy)
        Text("History synchronization")
    }else Text("History synchronization: refresh to load saved choice.")
    Text("On participates only when explicitly authorized. Off preserves local history, grants and resume cursors. Refresh to load this account's saved choice.",style=MaterialTheme.typography.bodySmall)
    enrollment?.devices?.forEach {device->
        var read by remember(device.peerId,device.mayRead){mutableStateOf(device.mayRead)}
        var serve by remember(device.peerId,device.mayServe){mutableStateOf(device.mayServe)}
        OutlinedCard(Modifier.fillMaxWidth()){
            Column(Modifier.padding(12.dp),verticalArrangement=Arrangement.spacedBy(6.dp)){
                Text(if(device.local)"This device" else "Account device",style=MaterialTheme.typography.titleSmall)
                Text("Peer ID: "+device.peerId,style=MaterialTheme.typography.bodySmall)
                Text("Trusted authorizer: "+(if(device.approvedIssuer)"Yes" else "No")+" · May receive: "+(if(device.mayRead)"Yes" else "No")+" · May serve: "+(if(device.mayServe)"Yes" else "No"),style=MaterialTheme.typography.bodySmall)
                Row{Checkbox(read,{read=it},enabled=!state.archiveBusy);Text("May receive historical messages")}
                Row{Checkbox(serve,{serve=it},enabled=!state.archiveBusy);Text("May serve historical messages")}
                TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.TrustAuthorizer,device.peerId)},enabled=!state.archiveBusy){Text("Trust archive authorizer")}
                TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.Authorize,device.peerId,read,serve)},enabled=!state.archiveBusy&&(read||serve)){Text("Authorize history access")}
                TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.Revoke,device.peerId)},enabled=!state.archiveBusy){Text("Revoke access")}
                TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.DisableAuthorizer,device.peerId)},enabled=!state.archiveBusy){Text("Disable authorizer")}
                if(!device.local)OutlinedButton({dispatch(UiAction.ArchiveSynchronize(enrollment,device.peerId))},enabled=!state.archiveBusy&&device.mayServe&&enrollment.historySyncEnabled&&!state.archiveProgress.running){Text("Synchronize from this archive")}
            }
        }
    }
    Text(state.archiveProgress.state+" · "+state.archiveProgress.processed+" messages processed · "+state.archiveProgress.conversations+" conversations completed",style=MaterialTheme.typography.bodySmall)
    if(enrollment!=null&&state.archiveProgress.running)OutlinedButton({dispatch(UiAction.ArchiveSynchronize(enrollment,pause=true))},enabled=!state.archiveBusy){Text("Pause synchronization")}
    pending?.let {p->
        AlertDialog(onDismissRequest={pending=null},title={Text("Confirm archive authorization")},text={
            Text("Account "+p.context.accountId+"\nDevice Peer ID:\n"+p.device+"\n\n"+when(p.action){
                ArchiveDeviceAction.TrustAuthorizer->"Trust this device to sign archive grants?"
                ArchiveDeviceAction.Authorize->"Authorize selected capabilities: receive="+p.read+", serve="+p.serve+"?"
                ArchiveDeviceAction.Revoke->"Revoke this device's access granted by the local authorizer?"
                ArchiveDeviceAction.DisableAuthorizer->"Disable this archive authorizer locally?"
            }+"\n\nSigning identities are unchanged. Already received history is not erased.")
        },confirmButton={TextButton({dispatch(UiAction.ArchiveConsent(p.context,p.action,p.device,p.read,p.serve));pending=null}){Text("Confirm")}},
        dismissButton={TextButton({pending=null}){Text("Cancel")}})
    }
}
