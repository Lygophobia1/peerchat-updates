package com.peerchat.android.application

internal enum class RelayStage { Created,RelayRendezvous,NativePeerTlsOpen,PeerTlsReceive,AuthenticatedPeer,Cleanup,Closed }

/** Metadata only, eight transitions per attempt. Observes existing execution;
 * it never authorizes, schedules, retries or closes a transport. */
internal class RelayProgress(private val wall:()->Long=System::currentTimeMillis,
    private val monotonic:()->Long=System::nanoTime) {
    private val started=monotonic();private var entered=started
    private val startedUtc=wall();private var transitionUtc=startedUtc
    private var stage=RelayStage.Created;private var completed=RelayStage.Created
    private val transitions=ArrayDeque<String>()
    @Synchronized fun enter(next:RelayStage){
        if(stage==next||stage==RelayStage.Closed)return
        val now=monotonic();transitionUtc=wall()
        if(transitions.size==8)transitions.removeFirst()
        transitions.addLast("${stage.name}:${((now-entered).coerceAtLeast(0))/1_000_000}ms")
        completed=stage;stage=next;entered=now
    }
    @Synchronized fun snapshot():String {
        val now=monotonic()
        return "stage=${stage.name} elapsed_ms=${((now-started).coerceAtLeast(0))/1_000_000} stage_elapsed_ms=${((now-entered).coerceAtLeast(0))/1_000_000} started_utc_ms=$startedUtc last_transition_utc_ms=$transitionUtc last_completed_stage=${completed.name} completed_stages=${transitions.joinToString(",")}" 
    }
}
