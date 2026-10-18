package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.peerchat.android.application.*

private data class PendingArchiveConsent(val context:ArchiveEnrollmentSnapshot,val action:ArchiveDeviceAction,val device:String,val read:Boolean=false,val serve:Boolean=false)

@Composable internal fun ArchiveDevicesSection(state:PeerChatUiState,dispatch:(UiAction)->Unit){
    val enrollment=state.archiveEnrollment
    val ownDevices=state.ownDevices?.takeIf{it.account==(enrollment?.accountId?:state.account?.accountId?.toLongOrNull())}
    fun deviceName(peer:String)=ownDevices?.devices?.singleOrNull{it.peerId==peer}?.name?.takeIf(::validDeviceDisplayName)
    fun deviceLabel(peer:String,local:Boolean=false)=deviceName(peer)?.let{"$it · ${peer.take(12)}…"}?:if(local)"This device" else "Device "+peer.take(12)+"…"
    var pending by remember(enrollment?.accountId,enrollment?.authenticationGeneration){mutableStateOf<PendingArchiveConsent?>(null)}
    var managed by remember(enrollment?.accountId,enrollment?.authenticationGeneration){mutableStateOf<String?>(null)}
    var advanced by remember {mutableStateOf(false)}
    // One bounded existing application command at sign-in/reconnect, not a UI retry owner.
    LaunchedEffect(state.account?.accountId,state.connection){
        if(!state.demo&&state.account!=null&&state.connection==ConnectionState.Online&&enrollment==null&&!state.archiveBusy)dispatch(UiAction.ArchiveRefresh)
    }
    Text("History Sync",style=MaterialTheme.typography.titleMedium)
    Row(horizontalArrangement=Arrangement.spacedBy(12.dp)){
        // No verified snapshot means unknown, not OFF. Never present a loading
        // default as a persisted preference (nor write it back to native policy).
        if(enrollment==null)Text("Verifying History Sync setting…")
        else Switch(enrollment.historySyncEnabled,{value->dispatch(UiAction.HistorySync(enrollment,value))},enabled=!state.archiveBusy,modifier=Modifier.testTag("history-sync-policy"))
        Text("Keep approved devices up to date automatically.")
    }
    Text("Existing custom permissions and automatic-sync preferences are preserved. Offline Messages is a separate setting.",style=MaterialTheme.typography.bodySmall)
    val thisDevice=enrollment?.devices?.singleOrNull{it.local}
    if(thisDevice?.primaryAction==ArchiveDeviceAction.SetUp){
        Text("Set up History Sync",style=MaterialTheme.typography.titleMedium)
        Text("Enroll this first trusted device, then explicitly authorize your other devices.")
        Button({pending=PendingArchiveConsent(checkNotNull(enrollment),ArchiveDeviceAction.SetUp,thisDevice.peerId,true,true)},enabled=!state.archiveBusy){Text("Set up History Sync")}
    }
    Text("Sync Status",style=MaterialTheme.typography.titleMedium)
    val progress=state.archiveProgress
    Text("Current activity: "+progress.currentActivity+" · Reader source: "+if(progress.source.isEmpty())"No source selected" else deviceLabel(progress.source))
    Text("Last completed result: "+progress.lastResultSummary,style=MaterialTheme.typography.bodySmall)
    Text("Source role: ${progress.providers.size} active readers · ${progress.providedQueued} records queued for readers (retries included; not confirmed imports)",style=MaterialTheme.typography.bodySmall)
    Text("${progress.received} new messages received this session · ${progress.conversations} conversations completed",style=MaterialTheme.typography.bodySmall)
    if(progress.deferred>0||progress.rejectedBatches>0)Text("${progress.deferred} unresolved/deferred records · ${progress.rejectedBatches} rejected batches",style=MaterialTheme.typography.bodySmall)
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
            val own=state.ownDevices?.takeIf{it.account==enrollment?.accountId}?.devices?.singleOrNull{it.peerId==device.peerId}
            OutlinedCard(Modifier.fillMaxWidth()){
                Column(Modifier.padding(12.dp),verticalArrangement=Arrangement.spacedBy(6.dp)){
                    Text(deviceName(device.peerId)?:if(device.local)"This device" else "Device "+device.peerId.take(12)+"…",style=MaterialTheme.typography.titleSmall)
                    if(own?.name?.isNotEmpty()==true)Text((if(device.local)"This device · "else "Device · ")+device.peerId.take(12)+"…",style=MaterialTheme.typography.bodySmall)
                    if(own?.revoked==true)Text("Account device revoked; retained history is unchanged",style=MaterialTheme.typography.bodySmall)
                    Text(if(device.approved)"Approved" else if(device.mayRead||device.mayServe)"Verified history access" else if(device.authorized)"Registered grant · review trust and permissions" else if(device.revoked)"Access revoked" else "Approval required",style=MaterialTheme.typography.bodySmall)
                    Text(if(!device.local&&device.peerId==progress.source&&progress.running&&progress.displayState=="Syncing")"Available · transferring verified history" else "Availability unknown",style=MaterialTheme.typography.bodySmall)
                    Row{
                        device.primaryAction?.let{action->
                            Button({when(action){
                                ArchiveDeviceAction.SetUp->pending=PendingArchiveConsent(checkNotNull(enrollment),action,device.peerId,true,true)
                                ArchiveDeviceAction.ApproveIssuer->pending=PendingArchiveConsent(checkNotNull(enrollment),action,device.approvalIssuer)
                                else->managed=device.peerId
                            }},enabled=!state.archiveBusy&&own?.revoked!=true,contentPadding=PaddingValues(horizontal=12.dp,vertical=4.dp)){Text(when(action){ArchiveDeviceAction.SetUp->"Set up";ArchiveDeviceAction.ApproveIssuer->"Approve";else->"Authorize"})}
                        }
                        TextButton({managed=device.peerId},enabled=!state.archiveBusy&&own?.revoked!=true){Text("Manage")}
                        if(!device.local&&device.mayServe)TextButton({dispatch(UiAction.ArchiveSynchronize(checkNotNull(enrollment),device.peerId))},enabled=!state.archiveBusy&&own?.revoked!=true&&enrollment?.historySyncEnabled==true&&!progress.running){Text("Sync now")}
                    }
                    if(device.approvalKnown&&!device.approved&&device.primaryAction==null)Text("Approve from an enabled trusted history device. Signing in alone never grants history access.",style=MaterialTheme.typography.bodySmall)
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
    val selected=enrollment?.devices?.firstOrNull{it.peerId==managed&&state.ownDevices?.takeIf{own->own.account==enrollment.accountId}?.devices?.any{own->own.peerId==it.peerId&&own.revoked}!=true}
    if(selected!=null&&enrollment!=null){
        var custom by remember(selected.peerId){mutableStateOf(false)}
        var read by remember(selected.peerId,selected.grantedRead){mutableStateOf(if(selected.authorized)selected.grantedRead else true)}
        var serve by remember(selected.peerId,selected.grantedServe){mutableStateOf(if(selected.authorized)selected.grantedServe else true)}
        AlertDialog(onDismissRequest={managed=null},title={Text(if(selected.authorized)"Manage history device" else "New history device")},text={Column(verticalArrangement=Arrangement.spacedBy(6.dp)){
            Text(deviceLabel(selected.peerId,selected.local))
            Text(selected.authorizationState)
            Text("Receive and Provide let this device exchange eligible encrypted history. Existing custom permissions are preserved.",style=MaterialTheme.typography.bodySmall)
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
        }},confirmButton={TextButton({pending=PendingArchiveConsent(enrollment,ArchiveDeviceAction.Authorize,selected.peerId,read,serve)},enabled=!state.archiveBusy&&selected.approvalKnown&&selected.mayAuthorize&&(read||serve)&&(!selected.localGrantPresent||read!=selected.localGrantRead||serve!=selected.localGrantServe)){Text(if(selected.authorized)"Confirm permissions" else "Authorize device")}},dismissButton={TextButton({managed=null}){Text("Close")}})
    }
    pending?.let {p->
        AlertDialog(onDismissRequest={pending=null},title={Text("Confirm history authorization")},text={
            Text("Account "+p.context.accountId+deviceName(p.device)?.let{"\nDevice name: $it"}.orEmpty()+"\nDevice Peer ID:\n"+p.device+"\n\n"+when(p.action){
                ArchiveDeviceAction.TrustAuthorizer->"Trust this device to sign history grants?"
                ArchiveDeviceAction.Authorize->"Authorize selected permissions: receive="+p.read+", provide="+p.serve+"?"
                ArchiveDeviceAction.Revoke->"Revoke this device's access granted by the local authorizer?"
                ArchiveDeviceAction.DisableAuthorizer->"Disable this history authorizer locally?"
                ArchiveDeviceAction.SetUp->"Enroll this first trusted history device with Receive history ON and Provide history ON? This does not authorize any other device."
                ArchiveDeviceAction.ApproveIssuer->"Trust this exact registered authorizer to use its existing verified grants? No new grant will be issued."
            }+"\n\nSigning identities are unchanged. Already received history is not erased.")
        },confirmButton={TextButton({dispatch(UiAction.ArchiveConsent(p.context,p.action,p.device,p.read,p.serve));pending=null;managed=null}){Text("Confirm")}},dismissButton={TextButton({pending=null}){Text("Cancel")}})
    }
}
