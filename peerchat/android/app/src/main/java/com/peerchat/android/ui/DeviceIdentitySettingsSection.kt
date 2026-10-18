package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

private data class IdentityChoice(val kind:Int,val account:Long,val fingerprint:String,val label:String)

@Composable internal fun DeviceIdentitySettingsSection(state:PeerChatUiState,dispatch:(UiAction)->Unit){
    val local=state.localDeviceFingerprint
    var selected by remember(state.account?.accountId){mutableStateOf<IdentityChoice?>(null)}
    var entered by remember(selected){mutableStateOf("")}
    Column(verticalArrangement=Arrangement.spacedBy(8.dp)){
        Text("Device identity verification",style=MaterialTheme.typography.titleMedium)
        Text(state.identityDirectoryStatus,style=MaterialTheme.typography.bodySmall)
        Text("One-time authority verification can recognize an account's other signed devices for encrypted message and file delivery. It does not grant history access, group membership, or new contact permission.",style=MaterialTheme.typography.bodySmall)
        if(local.matches(Regex("[0-9a-f]{64}"))){
            Text("This device fingerprint: $local",style=MaterialTheme.typography.bodySmall)
            OutlinedButton({selected=IdentityChoice(0,state.account?.accountId?.removePrefix("account:")?.toLongOrNull()?:0,local,"this device")},
                enabled=!state.identityBusy){Text("Choose this device as account authority")}
            TextButton({selected=IdentityChoice(3,state.account?.accountId?.removePrefix("account:")?.toLongOrNull()?:0,"","your account")},
                enabled=!state.identityBusy){Text("Verify existing account authority")}
        }
        val own=state.ownDevices
        if(own!=null&&own.account==state.account?.accountId?.removePrefix("account:")?.toLongOrNull()){
            own.devices.filter{!it.current&&!it.revoked}.forEach{device->
                Text("Registered device: ${device.name.ifBlank{"Unnamed"}} · ${device.peerId}",style=MaterialTheme.typography.bodySmall)
                TextButton({selected=IdentityChoice(1,own.account,device.peerId,"registered device")},enabled=!state.identityBusy){Text("Approve this exact device")}
            }
        }
        state.conversations.filter{it.kind==ChatKind.Direct}.forEach{conversation->
            val account=conversation.id.removePrefix("account:").toLongOrNull()
            if(account!=null)TextButton({selected=IdentityChoice(2,account,"",conversation.name)},enabled=!state.identityBusy){Text("Verify ${conversation.name}'s authority")}
        }
    }
    selected?.let{choice->
        val expected=choice.fingerprint
        val valid=entered.matches(Regex("[0-9a-f]{64}"))&&(expected.isEmpty()||entered==expected)
        AlertDialog(onDismissRequest={selected=null},title={Text(when(choice.kind){0->"Choose account authority";1->"Approve registered device";3->"Verify existing account authority";else->"Verify contact authority"})},
            text={Column(verticalArrangement=Arrangement.spacedBy(8.dp)){
                Text(when(choice.kind){
                    0->"Confirm that this exact fingerprint is the device you intend to make your account's signing authority: $expected"
                    1->"Confirm this registered device's exact fingerprint through a separate trusted channel before approving it: $expected"
                    3->"Get your account's existing authority fingerprint from your already verified device through a separate trusted channel. An account login or server directory response is not confirmation."
                    else->"Get ${choice.label}'s authority fingerprint from them through a separate trusted channel. The server's directory response is not confirmation."
                })
                Text("Scope: signed device identity for encrypted message and file delivery only. No history, group, or contact permissions.",style=MaterialTheme.typography.bodySmall)
                OutlinedTextField(entered,{entered=it.take(64)},label={Text("Enter full 64-character fingerprint")},singleLine=true)
            }},confirmButton={TextButton({
                when(choice.kind){
                    0->dispatch(UiAction.SelectIdentityAuthority(entered))
                    1->dispatch(UiAction.EndorseOwnIdentityDevice(expected,entered))
                    3->dispatch(UiAction.VerifyOwnAccountAuthority(entered))
                    else->dispatch(UiAction.VerifyContactAuthority(choice.account,entered))
                };selected=null
            },enabled=valid&&!state.identityBusy){Text("Confirm exact fingerprint")}},dismissButton={TextButton({selected=null}){Text("Cancel")}})
    }
}
