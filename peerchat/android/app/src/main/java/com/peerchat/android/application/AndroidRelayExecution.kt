package com.peerchat.android.application

import com.peerchat.android.ReliableBridge
import com.peerchat.android.platform.ServerAccounts
import com.peerchat.android.platform.tls.ServerChannel
import com.peerchat.android.platform.tls.RelayFrames

// Physical execution adapter analogous to Desktop RelayTransport. Native owns
// TLS/Hello/trust, route selection, retry, ACK/dedupe/history. No reconnect loop.
internal class AndroidRelayExecution(private val profile: Long,private val request: RelayExecution,
    private val accounts: ServerAccounts,private val remoteAccount: Long) : AutoCloseable {
    @Volatile private var stopping=false
    @Volatile private var channel: ServerChannel?=null
    @Volatile var attached=false
        private set
    @Volatile var finished=false
        private set
    private var worker: Thread?=null
    fun start() {
        check(worker==null)
        worker=Thread({run()},"PeerChat opaque relay").apply {isDaemon=true;start()}
    }
    private fun run() {
        var writer: Thread?=null
        try {
            val relay=accounts.openRelay(remoteAccount,request.peerId)
            channel=relay.channel
            if(stopping)return
            val peer=request.peerId.toByteArray(Charsets.US_ASCII)
            check(ReliableBridge.open(profile,request.attempt,relay.incoming,peer)==request.attempt)
            attached=true
            check(ReliableBridge.exchange(profile,request.attempt,ReliableBridge.RECEIVE,ByteArray(0))!=null)
            val frames=RelayFrames(relay.sessionId)
            writer=Thread({
                try {
                    while(!stopping) {
                        val bytes=checkNotNull(ReliableBridge.exchange(profile,request.attempt,ReliableBridge.OUTPUT,ByteArray(0)))
                        if(bytes.isNotEmpty())relay.channel.write(frames.wrap(bytes),43+65536)
                        else Thread.sleep(10) // output readiness poll, never a reconnect decision
                    }
                } catch(_: Exception) {try {relay.channel.close()}catch(_: Exception){}}
            },"PeerChat relay output").apply {isDaemon=true;start()}
            while(!stopping) {
                val bytes=frames.unwrap(relay.channel.read(43+65536))
                check(ReliableBridge.exchange(profile,request.attempt,ReliableBridge.RECEIVE,bytes)!=null)
            }
        } catch(_: Exception) {
            // No payload/session-token/exception-message logging. Native route
            // retirement and bounded retry stay with the current owner.
        } finally {
            val retry=!stopping;stopping=true
            try {channel?.close()}catch(_: Exception){}
            if(writer!==Thread.currentThread())writer?.join(1500)
            ReliableBridge.close(profile,request.attempt,retry)
            finished=true
        }
    }
    override fun close() {
        stopping=true
        try {channel?.close()}catch(_: Exception){}
        ReliableBridge.close(profile,request.attempt,false)
        // Do not join while holding any native/application gate.
    }
}
