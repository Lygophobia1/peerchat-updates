package com.peerchat.android.application

import com.peerchat.android.ReliableBridge
import com.peerchat.android.platform.ServerAccounts
import com.peerchat.android.platform.tls.ServerChannel
import com.peerchat.android.platform.tls.RelayFrames

// Physical execution adapter analogous to Desktop RelayTransport. Native owns
// TLS/Hello/trust, route selection, retry, ACK/dedupe/history. No reconnect loop.
internal class AndroidRelayExecution(private val profile: Long,private val request: RelayExecution,
    private val accounts: ServerAccounts,private val remoteAccount: Long) : AutoCloseable {
    val requestPeerId:String get()=request.peerId
    @Volatile var failureDiagnostic:String?=null
        private set
    val groupScope:GroupRelayScope? get()=request.group
    @Volatile private var stopping=false
    @Volatile private var channel: ServerChannel?=null
    @Volatile var attached=false
        private set
    @Volatile var finished=false
        private set
    @Volatile var lastOutputIdleNanos=0L
        private set
    private var worker: Thread?=null
    private val progress=RelayProgress()
    private val ioTimings=RelayIoTimings()
    fun diagnostics():String="relay_attempt=${request.attempt} remote_account=$remoteAccount peer_device=${request.peerId} purpose="+
        (if(request.archive!=null)"archive_history" else if(request.group!=null)"group_control" else "friend")+
        " attached=$attached finished=$finished ${progress.snapshot()} ${ioTimings.snapshot()} failure=${failureDiagnostic?:"none"}"
    fun observedAuthenticatedPeer(){progress.enter(RelayStage.AuthenticatedPeer)}
    fun start() {
        check(worker==null)
        worker=Thread({run()},"PeerChat opaque relay").apply {isDaemon=true;start()}
    }
    private fun run() {
        var writer: Thread?=null
        var stage="relay_rendezvous"
        progress.enter(RelayStage.RelayRendezvous)
        try {
            android.util.Log.i("PeerChatWake","stage=relay_attempt_started account=$remoteAccount device=${request.peerId} attempt=${request.attempt}")
            val relay=accounts.openRelay(remoteAccount,request.peerId,request.group?.fields()?:request.archive?.fields(),request.attempt) {opened->
                channel=opened
                if(stopping){opened.close();throw java.io.IOException("Relay cancelled before pairing")}
            }
            stage="native_peer_tls_open"
            progress.enter(RelayStage.NativePeerTlsOpen)
            android.util.Log.i("PeerChatWake","stage=relay_rendezvous_ready account=$remoteAccount device=${request.peerId} attempt=${request.attempt}")
            channel=relay.channel
            if(stopping)return
            val peer=request.peerId.toByteArray(Charsets.US_ASCII)
            check(ReliableBridge.open(profile,request.attempt,relay.incoming,peer)==request.attempt)
            attached=true
            stage="peer_tls_receive"
            progress.enter(RelayStage.PeerTlsReceive)
            check(ReliableBridge.exchange(profile,request.attempt,ReliableBridge.RECEIVE,ByteArray(0))!=null)
            val frames=RelayFrames(relay.sessionId)
            writer=Thread({
                try {
                    var nextStatusAt=0L
                    while(!stopping) {
                        val bytes=ioTimings.measure(RelayIoStage.NativeOutput){checkNotNull(ReliableBridge.exchange(profile,request.attempt,ReliableBridge.OUTPUT,ByteArray(0)))}
                        if(bytes.isNotEmpty())ioTimings.measure(RelayIoStage.SocketWrite){relay.channel.write(frames.wrap(bytes),43+65536)}
                        else {
                            val now=System.nanoTime()
                            if(now>=nextStatusAt){
                                check(nativeRelayOpen(ReliableBridge.exchange(profile,request.attempt,ReliableBridge.STATUS,ByteArray(0))))
                                nextStatusAt=now+100_000_000L
                            }
                            lastOutputIdleNanos=now
                            Thread.sleep(10)
                        }
                    }
                } catch(failure: Exception) {failureDiagnostic="purpose="+(if(request.archive!=null)"archive_history" else if(request.group!=null)"group_control" else "friend")+" stage=relay_output reason="+relayFailureCode(failure);try {relay.channel.close()}catch(_: Exception){}}
            },"PeerChat relay output").apply {isDaemon=true;start()}
            while(!stopping) {
                val bytes=frames.unwrap(ioTimings.measure(RelayIoStage.SocketRead){relay.channel.read(43+65536)})
                ioTimings.measure(RelayIoStage.NativeReceive){check(ReliableBridge.exchange(profile,request.attempt,ReliableBridge.RECEIVE,bytes)!=null)}
            }
        } catch(failure: Exception) {
            if(!stopping&&failureDiagnostic==null)failureDiagnostic="purpose="+(if(request.archive!=null)"archive_history" else if(request.group!=null)"group_control" else "friend")+" stage=$stage reason="+relayFailureCode(failure)
            android.util.Log.i("PeerChatWake","stage="+stage+" attempt=${request.attempt} result="+failure.javaClass.simpleName)
            if(request.archive!=null)android.util.Log.w("PeerChatArchive","event=attempt_failed attempt=${request.attempt} role="+
                (if(request.archive.source==request.peerId)"reader" else "source")+" stage=$stage reason="+
                (if(failure is com.peerchat.android.platform.ServerRejected&&failure.code in setOf("relay_duplicate_attachment","relay_pair_timeout","relay_not_authorized","session_invalid"))failure.code else failure.javaClass.simpleName))
            // No payload/session-token/exception-message logging. Native route
            // retirement and bounded retry stay with the current owner.
        } finally {
            progress.enter(RelayStage.Cleanup)
            val retry=!stopping;stopping=true
            try {channel?.close()}catch(_: Exception){}
            if(writer!==Thread.currentThread())writer?.join(1500)
            ReliableBridge.close(profile,request.attempt,retry)
            if(request.archive!=null)android.util.Log.i("PeerChatArchive","event=attempt_cleanup attempt=${request.attempt} socket_closed=true retry=$retry")
            finished=true
            progress.enter(RelayStage.Closed)
        }
    }
    override fun close() {
        stopping=true
        try {channel?.close()}catch(_: Exception){}
        ReliableBridge.close(profile,request.attempt,false)
        // Do not join while holding any native/application gate.
    }
}

internal fun nativeRelayOpen(status:ByteArray?):Boolean=status?.size==2&&status[1].toInt()==0

// Fixed classification only; no server bodies, credentials or exception messages.
internal fun relayFailureCode(failure:Exception):String = when {
    failure is com.peerchat.android.platform.ServerRejected&&failure.code in setOf("relay_duplicate_attachment","relay_pair_timeout","relay_not_authorized","session_invalid","session_superseded")->failure.code
    failure is java.net.SocketTimeoutException->"transport_timeout"
    failure is java.io.IOException->"transport_io_failure"
    failure is IllegalStateException->"native_or_relay_admission_rejected"
    else->"relay_execution_failed"
}
