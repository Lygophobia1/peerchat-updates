package com.peerchat.android.platform

import com.peerchat.android.platform.tls.ServerChannel
import org.json.JSONObject
import java.util.concurrent.ArrayBlockingQueue

/** Execution adapter for the existing server subscription. No state authority,
 * delivery work, route selection or retry loop lives on this reader thread. */
internal class ForegroundEvents(accounts:ServerAccounts):AutoCloseable {
    private val queue=ArrayBlockingQueue<JSONObject>(128)
    @Volatile private var closed=false
    @Volatile private var channel:ServerChannel?=null
    @Volatile var connected=false
        private set
    @Volatile var finished=false
        private set
    @Volatile var failure:Exception?=null
        private set
    private var nextHeartbeat=0L
    private val presenceSequence=PresenceSequence()
    init {Thread({
        try {
            val opened=accounts.openEvents()
            presenceSequence.reset(accounts.presenceEpoch)
            channel=opened
            if(closed){opened.close();return@Thread}
            connected=true
            while(!closed){
                val event=JSONObject(opened.read(100000).toString(Charsets.UTF_8))
                require(event.getInt("version")==1&&event.getString("type")=="friend_event")
                if(event.optString("event")=="presence_changed"&&!presenceSequence.accept(event.optString("serverEpoch"),event.optLong("accountId"),event.optLong("presenceRevision")))continue
                check(queue.offer(event)){"Event projection gap"}
            }
        }catch(error:Exception){if(!closed)failure=error}
        finally {connected=false;finished=true;try{channel?.close()}catch(_:Exception){}}
    },"PeerChat authenticated server events").apply{isDaemon=true;start()}}
    fun poll():JSONObject?=queue.poll()
    fun heartbeat(now:Long){
        if(!connected||now<nextHeartbeat)return
        channel?.write(JSONObject().put("type","event_heartbeat").toString().toByteArray(Charsets.UTF_8),4096)
        nextHeartbeat=now+20000
    }
    override fun close(){closed=true;connected=false;try{channel?.close()}catch(_:Exception){}}
}
