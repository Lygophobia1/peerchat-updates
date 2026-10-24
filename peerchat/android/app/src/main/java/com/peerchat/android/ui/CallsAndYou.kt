package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import android.app.Activity
import android.os.Build
import android.Manifest
import com.peerchat.android.platform.NotificationPreview
import com.peerchat.android.application.VoiceControl
import com.peerchat.android.application.VoiceSnapshot
import com.peerchat.android.application.VoiceCallTarget
import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.SystemClock
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.delay

internal fun voiceStatus(state:Long):String=when(state){
    1L->"Calling";2L->"Incoming call";3L->"Negotiating";4L->"Connecting audio"
    5L->"Connected";6L->"Ending";7L->"Ended";8L->"Failed";else->"Call status unavailable"
}

internal data class CallAudioRoute(val id:Int,val title:String,val detail:String="")

internal fun callAudioRoutes(context:Context):List<CallAudioRoute> {
    val audio=context.getSystemService(AudioManager::class.java)
    val routes=mutableListOf<CallAudioRoute>()
    val bluetoothGranted=android.os.Build.VERSION.SDK_INT<31 ||
        context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED
    try {
        val devices=if(android.os.Build.VERSION.SDK_INT>=31)audio.availableCommunicationDevices
            else audio.getDevices(AudioManager.GET_DEVICES_OUTPUTS).toList()
        if(devices.any{it.type==AudioDeviceInfo.TYPE_BUILTIN_EARPIECE})routes+=CallAudioRoute(-1,"Phone")
        if(devices.any{it.type==AudioDeviceInfo.TYPE_BUILTIN_SPEAKER})routes+=CallAudioRoute(-2,"Speaker")
        devices.filter { bluetoothGranted && (it.type==AudioDeviceInfo.TYPE_BLUETOOTH_SCO || android.os.Build.VERSION.SDK_INT>=31&&it.type==AudioDeviceInfo.TYPE_BLE_HEADSET) }
            .distinctBy { it.id }.forEach { device->routes+=CallAudioRoute(device.id,"Bluetooth",device.productName.toString().takeIf{it.isNotBlank()}.orEmpty()) }
    }catch(_:SecurityException){}catch(_:RuntimeException){}
    return routes
}

internal fun currentCallAudioRoute(context:Context,voice:VoiceSnapshot):String=try {
    val audio=context.getSystemService(AudioManager::class.java)
    if(android.os.Build.VERSION.SDK_INT>=31){
        val device=audio.communicationDevice
        when {
            device==null||device.type==AudioDeviceInfo.TYPE_BUILTIN_EARPIECE->"Phone"
            device.type==AudioDeviceInfo.TYPE_BUILTIN_SPEAKER->"Speaker"
            device.type==AudioDeviceInfo.TYPE_BLUETOOTH_SCO||device.type==AudioDeviceInfo.TYPE_BLE_HEADSET->"Bluetooth"
            else->device.productName.toString().ifBlank{"Audio device"}
        }
    }else{
        @Suppress("DEPRECATION")
        val bluetooth=audio.isBluetoothScoOn
        @Suppress("DEPRECATION")
        val speaker=audio.isSpeakerphoneOn
        when{bluetooth->"Bluetooth";speaker||voice.speaker->"Speaker";else->"Phone"}
    }
}catch(_:RuntimeException){if(voice.speaker)"Speaker" else "Phone"}

/** The connected timestamp is supplied by the persistent shell, so minimizing and rotation preserve it. */
@Composable internal fun FullScreenDmCall(state:PeerChatUiState,dispatch:(UiAction)->Unit,
    connectedAtElapsed:Long,onMinimize:()->Unit,onChat:()->Unit,onRetry:()->Unit,
    routeOptionsOverride:List<CallAudioRoute>?=null,timeNowElapsed:Long?=null,onDiagnostics:()->Unit={dispatch(UiAction.Navigate(Route.Diagnostics))}) {
    val voice=state.voice
    val target=voice.target
    val group=voice.group.isNotEmpty()||state.voiceConversationId.startsWith("group:")||state.voicePendingConversationId.startsWith("group:")
    val conversation=state.conversations.firstOrNull{it.id==state.voiceConversationId||it.id=="group:${voice.group}"}
    val room=conversation?.voiceRooms?.firstOrNull{it.id==voice.room.ifBlank{state.voicePendingRoomId}}
    fun tag(name:String)=if(group)name.replace("dm-","group-")else name
    val context=LocalContext.current
    var routesOpen by remember { mutableStateOf(false) }
    var routeRefresh by remember { mutableIntStateOf(0) }
    var noiseOpen by remember { mutableStateOf(false) }
    val prefs=remember(context){context.getSharedPreferences("voice-processing",Context.MODE_PRIVATE)}
    val noiseEnabled=prefs.getBoolean("automatic-noise-control",true)
    val noiseLevel=prefs.getInt("automatic-noise-level",1).coerceIn(0,2)
    val noiseLabel=if(noiseEnabled)listOf("Low","Medium","High")[noiseLevel] else "Off"
    var now by remember { mutableLongStateOf(timeNowElapsed?:SystemClock.elapsedRealtime()) }
    LaunchedEffect(target,routesOpen) {
        while(target!=null){now=timeNowElapsed?:SystemClock.elapsedRealtime();if(routesOpen)routeRefresh++;delay(1000)}
    }
    val connected=connectedAtElapsed>0&&voice.mediaState==2L
    val elapsed=if(connected) ((now-connectedAtElapsed)/1000).coerceAtLeast(0) else 0
    val time="%02d:%02d".format(elapsed/60,elapsed%60)
    val incoming=!group&&voice.direction==1L&&voice.state==2L
    val failed=voice.state==8L||!voice.active&&!state.voiceBusy&&state.voiceError!=null
    val title=state.voicePeerName.ifBlank{conversation?.name?:if(group)"Group voice" else "PeerChat contact"}
    val initials=title.split(Regex("\\s+|(?<=[a-z])(?=[A-Z])")).filter{it.isNotBlank()}.take(2).joinToString(""){it.first().uppercase()}.ifBlank{"PC"}
    val status=when {
        failed->"Couldn't start call"
        incoming->"Incoming call"
        connected->"Connected · $time"
        state.voiceBusy&&!voice.active->"Preparing…"
        group&&room!=null&&room.participants.size<=1->"Waiting for participants"
        voice.mediaState==3L||voice.mediaState==4L->"Reconnecting audio…"
        voice.state in 3L..4L->"Connecting…"
        voice.state==1L->"Calling…"
        voice.state==5L->"Connecting audio…"
        else->"Connecting…"
    }
    val statusColor=if(connected)Color(0xFF22D3B3) else Color(0xFFB7C7D5)
    Surface(Modifier.fillMaxSize().testTag(tag("dm-call-fullscreen")),color=Color(0xFF08131D)) {
        Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
            Row(Modifier.fillMaxWidth().height(72.dp).background(Color(0xFF10202D)).padding(horizontal=16.dp),
                verticalAlignment=Alignment.CenterVertically) {
                IconButton(onClick=onMinimize,modifier=Modifier.testTag(tag("dm-call-minimize")).semantics {
                    contentDescription="Minimize call";role=Role.Button
                }) { Text("⌄",color=Color(0xFFD7E6F4),style=MaterialTheme.typography.headlineSmall) }
                Column(Modifier.weight(1f),horizontalAlignment=Alignment.CenterHorizontally) {
                    Text("PeerChat",fontWeight=FontWeight.SemiBold,color=Color.White)
                    Text(if(group)"Group voice call" else "Voice call",style=MaterialTheme.typography.labelMedium,color=Color(0xFFACC0D2))
                }
                Box(Modifier.size(48.dp))
            }
            Column(Modifier.weight(1f).fillMaxWidth().padding(horizontal=24.dp).verticalScroll(rememberScrollState()),
                horizontalAlignment=Alignment.CenterHorizontally,verticalArrangement=if(group)Arrangement.Top else Arrangement.Center) {
                Spacer(Modifier.height(8.dp))
                Box(Modifier.size(if(group)88.dp else 150.dp),contentAlignment=Alignment.Center) {
                    if(!connected&&!failed)Box(Modifier.size(if(group)86.dp else 148.dp).border(10.dp,Color(0x221F8BC7),CircleShape))
                    Box(Modifier.size(if(group)76.dp else 132.dp).border(2.dp,if(connected)Color(0xFF22D3B3)else Color(0x555C7992),CircleShape)
                        .padding(10.dp).background(Color(0xFF40576B),CircleShape),contentAlignment=Alignment.Center) {
                        Text(initials,style=MaterialTheme.typography.displaySmall,color=Color.White,fontWeight=FontWeight.Medium)
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text(title,style=if(group)MaterialTheme.typography.titleLarge else MaterialTheme.typography.headlineMedium,color=Color.White,textAlign=TextAlign.Center)
                Spacer(Modifier.height(8.dp))
                Text(status,style=MaterialTheme.typography.titleMedium,color=if(failed)MaterialTheme.colorScheme.error else statusColor,
                    modifier=Modifier.testTag(tag(if(connected)"dm-call-connected-status" else "dm-call-status")))
                if(group) {
                    room?.let { current ->
                        Text(current.name,Modifier.padding(top=8.dp),color=Color(0xFFB7C7D5))
                        Text("${current.participants.size} in room",style=MaterialTheme.typography.labelMedium,color=Color(0xFFB7C7D5))
                        current.participants.take(16).forEach { name -> Text("$name · Joined",style=MaterialTheme.typography.bodySmall,color=Color(0xFFB7C7D5)) }
                    }
                    if(voice.active)Text(voice.diagnostic,Modifier.padding(top=8.dp),style=MaterialTheme.typography.bodySmall,color=Color(0xFFB7C7D5))
                }
                if(failed) {
                    val detail=voice.platformDiagnostic.ifBlank { state.voiceError.orEmpty() }
                    if(detail.isNotBlank())Text(detail.take(160),Modifier.padding(top=10.dp),style=MaterialTheme.typography.bodySmall,
                        color=Color(0xFFB7C7D5),textAlign=TextAlign.Center)
                } else if(!state.voiceBusy&&voice.platformDiagnostic.isNotBlank()) {
                    Text(voice.platformDiagnostic.take(160),Modifier.padding(top=10.dp),style=MaterialTheme.typography.bodySmall,
                        color=Color(0xFFB7C7D5),textAlign=TextAlign.Center)
                }
            }
            TextButton(onClick=onDiagnostics,modifier=Modifier.align(Alignment.CenterHorizontally).testTag(tag("dm-call-diagnostics"))){Text("Open diagnostics")}
            if(failed) {
                Column(Modifier.fillMaxWidth().padding(horizontal=24.dp,vertical=18.dp),horizontalAlignment=Alignment.CenterHorizontally) {
                    Button(onClick=onRetry,modifier=Modifier.fillMaxWidth().height(56.dp).testTag(tag("dm-call-retry"))){Text("Try again")}
                    TextButton(onClick=onChat,modifier=Modifier.testTag(tag("dm-call-back-to-chat"))){Text("Back to chat")}
                }
            } else if(incoming&&target!=null) {
                Row(Modifier.fillMaxWidth().padding(horizontal=28.dp,vertical=28.dp),horizontalArrangement=Arrangement.spacedBy(18.dp)) {
                    OutlinedButton(onClick={dispatch(UiAction.ControlDmVoice(target,VoiceControl.Decline))},modifier=Modifier.weight(1f).height(60.dp).testTag(tag("dm-voice-decline"))){Text("Decline")}
                    Button(onClick={dispatch(UiAction.AnswerDmVoice(target))},enabled=!state.voiceBusy,modifier=Modifier.weight(1f).height(60.dp).testTag(tag("dm-voice-answer"))){Text("Answer")}
                }
            } else {
                Column(Modifier.fillMaxWidth().padding(horizontal=20.dp,vertical=14.dp),horizontalAlignment=Alignment.CenterHorizontally) {
                    OutlinedButton(onClick={noiseOpen=true},modifier=Modifier.testTag(tag("dm-call-noise-control")),shape=RoundedCornerShape(28.dp)) {
                        CallGlyph("settings",Color(0xFFD8E8F6),18.dp);Spacer(Modifier.width(8.dp));Text("Noise control · $noiseLabel")
                    }
                    Spacer(Modifier.height(14.dp))
                    Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceEvenly,verticalAlignment=Alignment.Top) {
                        CallControl(if(voice.muted)"mic-off" else "mic",if(voice.muted)"Unmute" else "Mute",voice.muted,tag("dm-call-mute"),enabled=target!=null&&voice.controlAvailable) {
                            target?.let{dispatch(UiAction.ControlDmVoice(it,VoiceControl.Mute,!voice.muted))}
                        }
                        CallControl("audio","Audio",false,tag("dm-call-audio"),enabled=target!=null,onClick={routesOpen=true})
                        CallControl("chat","Chat",false,tag("dm-call-chat"),subtitle="Return to chat",onClick=onChat)
                    }
                    val route=currentCallAudioRoute(context,voice)
                    Text(route,Modifier.padding(top=2.dp).testTag(tag("dm-call-audio-route")),style=MaterialTheme.typography.labelSmall,color=Color(0xFFB7C7D5))
                    Spacer(Modifier.height(14.dp))
                    if(state.voiceBusy&&target==null) {
                        Button(onClick={dispatch(UiAction.CancelVoicePreparation(state.voicePreparationToken,state.voiceUiIdentity))},
                            modifier=Modifier.fillMaxWidth().height(56.dp).testTag(tag("dm-call-cancel")),
                            colors=ButtonDefaults.buttonColors(containerColor=Color(0xFFD63C45),contentColor=Color.White)) { Text("Cancel call") }
                    } else if(target!=null&&voice.state in 1L..5L) {
                        Button(onClick={dispatch(UiAction.ControlDmVoice(target,VoiceControl.End))},enabled=!state.voiceBusy,
                            modifier=Modifier.fillMaxWidth().height(56.dp).testTag(tag("dm-call-end")),
                            colors=ButtonDefaults.buttonColors(containerColor=Color(0xFFD63C45),contentColor=Color.White)) { Text(if(group)"Leave call" else if(voice.state==1L)"Cancel call" else "End call") }
                    }
                }
            }
        }
    }
    if(routesOpen) {
        val routes=routeOptionsOverride ?: remember(routesOpen,context,routeRefresh){callAudioRoutes(context)}
        val permissionLauncher=rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()){granted->if(granted)routeRefresh++}
        AlertDialog(onDismissRequest={routesOpen=false},title={Text("Audio route")},text={
            Column(verticalArrangement=Arrangement.spacedBy(4.dp)) {
                routes.forEach { route->
                    TextButton(onClick={
                        routesOpen=false
                        target?.let{dispatch(UiAction.AudioRouteDmVoice(it,route.id))}
                    },modifier=Modifier.fillMaxWidth().testTag(tag("dm-audio-route-${route.id}"))) {
                        Text(if(route.detail.isBlank())route.title else "${route.title} · ${route.detail}",Modifier.weight(1f))
                    }
                }
                if(routes.isEmpty())Text("No phone or speaker route is currently available.",style=MaterialTheme.typography.bodySmall)
                if(android.os.Build.VERSION.SDK_INT>=31&&context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED)
                    TextButton(onClick={permissionLauncher.launch(android.Manifest.permission.BLUETOOTH_CONNECT)},modifier=Modifier.testTag(tag("dm-audio-route-bluetooth-permission"))){Text("Allow connected Bluetooth audio")}
                else if(routes.none{it.title=="Bluetooth"})Text("No connected Bluetooth communication device is available.",style=MaterialTheme.typography.bodySmall)
            }
        },confirmButton={TextButton({routesOpen=false}){Text("Close")}})
    }
    if(noiseOpen)AlertDialog(onDismissRequest={noiseOpen=false},title={Text("Noise control")},text={
        Column(verticalArrangement=Arrangement.spacedBy(4.dp)) {
            listOf("Off","Low","Medium","High").forEachIndexed{index,label->
                val selected=if(index==0)!noiseEnabled else noiseEnabled&&noiseLevel==index-1
                TextButton(onClick={
                    if(index==0)prefs.edit().putBoolean("automatic-noise-control",false).apply()
                    else prefs.edit().putBoolean("automatic-noise-control",true).putInt("automatic-noise-level",index-1).apply()
                    noiseOpen=false
                },modifier=Modifier.fillMaxWidth().testTag(tag("dm-noise-${label.lowercase()}"))) { Text((if(selected)"✓  " else "")+label,Modifier.weight(1f)) }
            }
            Text("Changes apply to the next call.",style=MaterialTheme.typography.bodySmall)
        }
    },confirmButton={TextButton({noiseOpen=false}){Text("Close")}})
}

@Composable private fun CallGlyph(name:String,color:Color,size:androidx.compose.ui.unit.Dp) {
    Canvas(Modifier.size(size)) {
        val w=this.size.width;val h=this.size.height;val sw=(w/13f).coerceAtLeast(1.4f)
        fun line(x1:Float,y1:Float,x2:Float,y2:Float){drawLine(color,androidx.compose.ui.geometry.Offset(x1*w,y1*h),androidx.compose.ui.geometry.Offset(x2*w,y2*h),sw,cap=StrokeCap.Round)}
        when(name) {
            "mic","mic-off" -> {
                drawRoundRect(color,androidx.compose.ui.geometry.Offset(w*.39f,h*.12f),androidx.compose.ui.geometry.Size(w*.22f,h*.48f),androidx.compose.ui.geometry.CornerRadius(w*.11f,h*.11f))
                val p=Path().apply{moveTo(w*.2f,h*.43f);cubicTo(w*.2f,h*.78f,w*.8f,h*.78f,w*.8f,h*.43f)}
                drawPath(p,color,style=Stroke(sw,cap=StrokeCap.Round));line(.5f,.78f,.5f,.92f);line(.31f,.92f,.69f,.92f)
                if(name=="mic-off"){line(.13f,.18f,.87f,.82f)}
            }
            "audio" -> {
                drawRoundRect(color,androidx.compose.ui.geometry.Offset(w*.12f,h*.38f),androidx.compose.ui.geometry.Size(w*.22f,h*.24f),androidx.compose.ui.geometry.CornerRadius(sw))
                val p=Path().apply{moveTo(w*.34f,h*.38f);lineTo(w*.63f,h*.17f);lineTo(w*.63f,h*.83f);lineTo(w*.34f,h*.62f);close()}
                drawPath(p,color)
                val wave=Path().apply{moveTo(w*.7f,h*.34f);cubicTo(w*.86f,h*.42f,w*.86f,h*.58f,w*.7f,h*.66f)}
                drawPath(wave,color,style=Stroke(sw,cap=StrokeCap.Round))
            }
            "chat" -> {
                drawRoundRect(color,androidx.compose.ui.geometry.Offset(w*.12f,h*.18f),androidx.compose.ui.geometry.Size(w*.76f,h*.58f),androidx.compose.ui.geometry.CornerRadius(w*.12f),style=Stroke(sw))
                val p=Path().apply{moveTo(w*.28f,h*.76f);lineTo(w*.25f,h*.9f);lineTo(w*.48f,h*.76f)}
                drawPath(p,color,style=Stroke(sw,cap=StrokeCap.Round))
            }
            else -> {
                drawCircle(color,w*.16f,androidx.compose.ui.geometry.Offset(w*.5f,h*.5f),style=Stroke(sw))
                for(i in 0..7){val a=i*Math.PI/4;val x=(.5+Math.cos(a)*.29).toFloat();val y=(.5+Math.sin(a)*.29).toFloat();
                    line((.5+Math.cos(a)*.19).toFloat(),(.5+Math.sin(a)*.19).toFloat(),x,y)}
            }
        }
    }
}

@Composable private fun CallControl(icon:String,label:String,selected:Boolean,
    tag:String,subtitle:String?=null,enabled:Boolean=true,onClick:()->Unit) {
    Column(horizontalAlignment=Alignment.CenterHorizontally) {
        FilledTonalIconButton(onClick=onClick,enabled=enabled,modifier=Modifier.size(64.dp).testTag(tag).semantics {
            contentDescription=if(subtitle.isNullOrBlank())label else "$label, $subtitle"
            role=Role.Button
            this.selected=selected
            stateDescription=if(label=="Mute"||label=="Unmute")if(selected)"Muted" else "Unmuted" else label
        },
            colors=IconButtonDefaults.filledTonalIconButtonColors(containerColor=if(selected)Color(0xFF36556A)else Color(0xFF1D3040))) {
            CallGlyph(icon,Color(0xFFD8E8F6),28.dp)
        }
        Text(label,Modifier.padding(top=6.dp),style=MaterialTheme.typography.labelLarge,color=Color.White)
        if(subtitle!=null)Text(subtitle,style=MaterialTheme.typography.labelSmall,color=Color(0xFF9FB4C7))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable internal fun VoiceCallCard(state:PeerChatUiState,dispatch:(UiAction)->Unit) {
    val voice=state.voice
    val target=voice.target
    val group=voice.group.isNotEmpty()
    val groupName=state.conversations.firstOrNull{it.id=="group:${voice.group}"}?.name.orEmpty()
    val roomName=state.conversations.firstOrNull{it.id=="group:${voice.group}"}?.voiceRooms?.firstOrNull{it.id==voice.room}?.name.orEmpty()
    Surface(color=MaterialTheme.colorScheme.primaryContainer,modifier=Modifier.fillMaxWidth().testTag(if(group)"group-voice-call" else "dm-voice-call")) {
        Column(Modifier.padding(horizontal=16.dp,vertical=10.dp),verticalArrangement=Arrangement.spacedBy(4.dp)) {
            Text(if(group)"${groupName.ifEmpty{"Group voice"}} · ${roomName.ifEmpty{"Voice room"}} · ${voice.diagnostic.take(160)}"
                else "${state.voicePeerName.ifEmpty { "Direct voice call" }} · ${voiceStatus(voice.state)}",style=MaterialTheme.typography.titleSmall)
            if(voice.platformDiagnostic.isNotBlank())Text(voice.platformDiagnostic.take(160),style=MaterialTheme.typography.bodySmall)
            if(voice.diagnostic.isNotBlank()&&voice.state==8L)Text(voice.diagnostic.take(160),style=MaterialTheme.typography.bodySmall)
            state.voiceError?.let{Text(it,style=MaterialTheme.typography.bodySmall,color=MaterialTheme.colorScheme.error)}
            if(target!=null)FlowRow(horizontalArrangement=Arrangement.spacedBy(6.dp)) {
                if(!group&&voice.direction==1L&&voice.state==2L){
                    Button({dispatch(UiAction.AnswerDmVoice(target))},enabled=!state.voiceBusy,modifier=Modifier.testTag("dm-voice-answer")){Text("Answer")}
                    TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.Decline))},enabled=!state.voiceBusy,modifier=Modifier.testTag("dm-voice-decline")){Text("Decline")}
                } else {
                    if(voice.state in 3L..5L){
                        TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.Mute,!voice.muted))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-mute" else "dm-voice-mute")){Text(if(voice.muted)"Unmute" else "Mute")}
                        TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.Deafen,!voice.deafened))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-deafen" else "dm-voice-deafen")){Text(if(voice.deafened)"Undeafen" else "Deafen")}
                        TextButton({dispatch(UiAction.SpeakerDmVoice(target,!voice.speaker))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-speaker" else "dm-voice-speaker")){Text(if(voice.speaker)"Speaker off" else "Speaker on")}
                    }
                    if(voice.state in 1L..5L)TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.End))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-leave" else "dm-voice-hangup")){Text(if(group)"Leave voice" else "Hang up")}
                }
            }
            if(state.voiceBusy)LinearProgressIndicator(Modifier.fillMaxWidth())
        }
    }
}

@Composable internal fun CallsPage(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Text("Calls", style = MaterialTheme.typography.headlineSmall)
        Text(if(state.demo) "Local media UI previews · microphone and speakers are never opened." else if(state.voiceAvailable)
            "Direct calls and available group voice rooms can be joined explicitly. Screen video is unavailable."
            else "Voice: ${state.voiceDiagnostic.take(160)} Screen video is unavailable.", style = MaterialTheme.typography.bodySmall)
        if(!state.demo&&state.voiceBusy)Text("Preparing call or waiting for microphone permission…",modifier=Modifier.testTag("dm-voice-progress"))
        if(!state.demo)state.voiceError?.let{Text(it,color=MaterialTheme.colorScheme.error,modifier=Modifier.testTag("dm-voice-error"))}
        Text("Voice rooms", style = MaterialTheme.typography.titleMedium)
        state.conversations.filter { it.voiceRooms.isNotEmpty() }.forEach { c ->
            OutlinedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(c.name, style = MaterialTheme.typography.titleMedium)
                    c.voiceRooms.forEach { room -> Text("${room.name} · ${room.participants.size} in voice${if (room.joined) " · In Voice" else ""}") }
                    TextButton({ dispatch(UiAction.Select(c.id)); dispatch(UiAction.Navigate(Route.Details)) }) { Text(if(state.demo)"Open room previews" else "Open voice rooms") }
                }
            }
        }
        Text(if(state.demo)"Recent calls · samples" else "Recent calls", style = MaterialTheme.typography.titleMedium)
        state.recentCalls.forEach { call -> Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.Call, null)
            Column { Text(call.name); Text(call.detail, color = if (call.missed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant) }
        } }
        if(state.demo) TextButton({ dispatch(UiAction.PreviewIncomingCall) }) { Text("Preview incoming DM call") }
        if (state.incomingCallPreview) OutlinedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Sammy · incoming call preview", style = MaterialTheme.typography.titleMedium)
                Text("No ringing, media or remote invite is active.")
                Row { Button(onClick = {}, enabled = false) { Text("Answer") }; TextButton({ dispatch(UiAction.PreviewIncomingCall) }) { Text("Dismiss preview") } }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable internal fun YouPage(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    var friendName by remember {mutableStateOf("")}
    val context=LocalContext.current
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Avatar("You", size = 64)
        Text("Your space", style = MaterialTheme.typography.headlineSmall)
        Text(state.account?.let {"${it.username} · ${it.accountId}"} ?: "No signed-in account")
        OutlinedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Device & identity", style = MaterialTheme.typography.titleMedium)
                Text(if(state.demo) "Device identity: Not connected" else state.diagnostics.substringBefore("\n","") + "\n" + state.diagnostics.lineSequence().firstOrNull {it.startsWith("Profile identity:")}.orEmpty())
                Text("Connected devices: Unavailable")
                Text(if(state.demo) "Demo only: no identity created." else "Profile and identity are owned by the native engine and Android Keystore.", style = MaterialTheme.typography.bodySmall)
            }
        }
        Text("Settings", style = MaterialTheme.typography.titleLarge)
        val voicePreferences = LocalContext.current.getSharedPreferences("voice-processing", android.content.Context.MODE_PRIVATE)
        var automaticNoiseControl by remember { mutableStateOf(voicePreferences.getBoolean("automatic-noise-control",true)) }
        Row(Modifier.fillMaxWidth(), verticalAlignment=Alignment.CenterVertically) {
            Text("Automatic Noise Control",Modifier.weight(1f))
            Switch(automaticNoiseControl, { value -> automaticNoiseControl=value;voicePreferences.edit().putBoolean("automatic-noise-control",value).apply() })
        }
        var automaticNoiseLevel by remember { mutableStateOf(voicePreferences.getInt("automatic-noise-level",1).coerceIn(0,2)) }
        Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
            listOf("Low","Medium","High").forEachIndexed { index,label ->
                FilterChip(automaticNoiseLevel==index,{automaticNoiseLevel=index;voicePreferences.edit().putInt("automatic-noise-level",index).apply()},enabled=automaticNoiseControl,label={Text(label)})
            }
        }
        Text("Medium is recommended. Applies to the next call. High may affect very quiet speech; disable to restore original processing.",style=MaterialTheme.typography.bodySmall)
        if(!state.demo){
            OutlinedButton({dispatch(UiAction.OpenTransferSettings)},Modifier.fillMaxWidth().testTag("open-transfer-settings")){Text("File Transfer Settings")}
            ArchiveDevicesSection(state,dispatch)
            DeviceIdentitySettingsSection(state,dispatch)
            Text("Offline encrypted mailbox storage",style=MaterialTheme.typography.titleMedium)
            Row(verticalAlignment=Alignment.CenterVertically){
                Switch(state.mailboxStorageAllowed,{dispatch(UiAction.MailboxStorage(it))})
                Text(if(state.mailboxStorageAllowed) "ON" else "OFF")
            }
            Text("On allows temporary encrypted server storage when a live connection is unavailable. Off keeps messages on the sender until a live route is available. Stored is not Delivered; only recipient acknowledgement confirms delivery.",style=MaterialTheme.typography.bodySmall)
            Text("Message notifications",style=MaterialTheme.typography.titleMedium)
            Row(verticalAlignment=Alignment.CenterVertically){
                Switch(state.notificationsEnabled,{dispatch(UiAction.NotificationSettings(it,state.notificationPreview))})
                Text("Enable notifications")
            }
            if(Build.VERSION.SDK_INT>=33)TextButton({(context as? Activity)?.requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS),7341)}){Text("Allow Android notifications")}
            FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp)){
                NotificationPreview.entries.forEach{choice->FilterChip(state.notificationPreview==choice,
                    {dispatch(UiAction.NotificationSettings(state.notificationsEnabled,choice))},label={Text(when(choice){NotificationPreview.SenderAndMessage->"Sender and message";NotificationPreview.SenderOnly->"Sender only";NotificationPreview.HideContent->"Hide content"})})}
            }
            Text("Push carries no content. Background delivery requires Remember Me and a configured push provider.",style=MaterialTheme.typography.bodySmall)
        }
        if(!state.demo && state.account!=null) {
            OutlinedTextField(friendName,{friendName=it.take(64)},label={Text("Add friend by username")},modifier=Modifier.fillMaxWidth())
            TextButton({dispatch(UiAction.RequestFriend(friendName))},enabled=friendName.isNotBlank()){Text("Send friend request")}
            state.incomingRequests.forEach {request->
                Text("Friend request: ${request.name}")
                Row {
                    TextButton({dispatch(UiAction.RespondFriend(request.accountId.removePrefix("account:").toLong(),true))}){Text("Accept")}
                    TextButton({dispatch(UiAction.RespondFriend(request.accountId.removePrefix("account:").toLong(),false))}){Text("Reject")}
                }
            }
        }
        Text("Appearance", style = MaterialTheme.typography.titleMedium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ThemeChoice.entries.forEach { theme -> FilterChip(state.theme == theme, { dispatch(UiAction.SetTheme(theme)) }, label = { Text(theme.name) }) }
        }
        Text("Appearance choice is session-local in this preview.", style = MaterialTheme.typography.bodySmall)
        OutlinedButton({ dispatch(UiAction.Navigate(Route.Server)) }) { Text("Advanced / server & trust") }
        if(!state.demo) {
            OutlinedButton({dispatch(UiAction.ResetNetworkSession)},Modifier.testTag("reset-network-session")) {Text("Reset Network Session")}
            Text("Disconnects PeerChat from the server and peers and clears temporary connection state. Your account, messages, identity, keys and settings are not deleted.",style=MaterialTheme.typography.bodySmall)
        }
        OutlinedButton({ dispatch(UiAction.Navigate(Route.Diagnostics)) }) { Text("Application diagnostics") }
        Text("PeerChat · ${com.peerchat.android.BuildConfig.VERSION_NAME}", color = MaterialTheme.colorScheme.onSurfaceVariant)
        state.notice?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
        TextButton({ dispatch(UiAction.Navigate(Route.Welcome)) }) { Text("Return to welcome") }
        if(!state.demo && state.account!=null) TextButton({dispatch(UiAction.SignOut)}) {Text("Sign out")}
    }
}
