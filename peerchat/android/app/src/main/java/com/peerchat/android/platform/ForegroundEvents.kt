package com.peerchat.android.platform

import com.peerchat.android.platform.tls.ServerChannel
import org.json.JSONObject
import java.util.concurrent.ArrayBlockingQueue

/** Execution adapter for the existing server subscription. No state authority,
 * delivery work, route selection or retry loop lives on this reader thread. */
internal class ForegroundEvents(accounts:ServerAccounts,private val onChange:(ForegroundEvents)->Unit={}):AutoCloseable {
    private val queue=ArrayBlockingQueue<JSONObject>(128)
    @Volatile private var output:EventOutputPump?=null
    @Volatile private var closed=false
    @Volatile private var channel:ServerChannel?=null
    @Volatile var connected=false
        private set
    @Volatile var finished=false
        private set
    @Volatile var failure:Exception?=null
        private set
    private val presenceSequence=PresenceSequence()
    private fun notifyChange(){try{onChange(this)}catch(_:Exception){/* A wake hint cannot tear down the event subscription. */}}
    init {Thread({
        try {
            val opened=accounts.openEvents()
            presenceSequence.reset(accounts.presenceEpoch)
            channel=opened
            if(closed){opened.close();return@Thread}
            output=EventOutputPump({bytes,maximum->opened.writeBounded(bytes,maximum,10000)}, {error->
                if(!closed)failure=error
                android.util.Log.i("PeerChatNetwork","stage=event_output_failed reason=${error.javaClass.simpleName}")
                try{opened.close()}catch(_:Exception){}
            },timing={elapsed->if(elapsed>=250)android.util.Log.i("PeerChatNetwork","stage=event_output elapsed_ms=$elapsed")})
            connected=true
            notifyChange()
            while(!closed){
                val event=JSONObject(opened.read(100000).toString(Charsets.UTF_8))
                val type=event.getString("type")
                require(type in setOf("friend_event","group_message","group_delivery","group_room_snapshot","group_room_result","group_media_signal","group_media_result"))
                if(type=="friend_event")require(event.getInt("version")==1)
                // Room/media notifications are existing server events. This
                // candidate cannot join/capture without the native receive engine;
                // unrelated room traffic must not tear down text messaging.
                if(type in setOf("group_room_snapshot","group_room_result","group_media_signal","group_media_result"))continue
                if(event.optString("event")=="presence_changed"&&!presenceSequence.accept(event.optString("serverEpoch"),event.optLong("accountId"),event.optLong("presenceRevision")))continue
                check(queue.offer(event)){"Event projection gap"}
                notifyChange()
            }
        }catch(error:Exception){if(!closed&&failure==null)failure=error}
        finally {connected=false;finished=true;output?.close();try{channel?.close()}catch(_:Exception){};notifyChange()}
    },"PeerChat authenticated server events").apply{isDaemon=true;start()}}
    fun poll():JSONObject?=queue.poll()
    fun hasPending():Boolean=queue.isNotEmpty()
    fun queueGroup(value:JSONObject):Boolean {
        require(value.optString("type") in setOf("group_send","group_ack"))
        return connected&&!closed&&output?.offer(value.toString().toByteArray(Charsets.UTF_8))==true
    }
    override fun close(){closed=true;connected=false;output?.close();try{channel?.close()}catch(_:Exception){}}
}
