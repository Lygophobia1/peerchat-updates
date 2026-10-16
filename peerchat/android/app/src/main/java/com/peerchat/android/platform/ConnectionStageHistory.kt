package com.peerchat.android.platform

/** Bounded, in-memory metadata only. Never accept response bodies or exception
 * messages; these rows are safe for local diagnostics, not session authority. */
internal class ConnectionStageHistory(private val wall:()->Long=System::currentTimeMillis,
    private val monotonic:()->Long=System::nanoTime) {
    private val rows=ArrayDeque<String>()
    private var sequence=0L
    private var lastSuccess="none";private var lastFailure="none"
    @Synchronized private fun add(row:String){if(rows.size==64)rows.removeFirst();rows.addLast(row)}
    fun <T> measure(operation:String,block:()->T):T {
        val op=operation(operation);val id=synchronized(this){++sequence};val started=monotonic()
        add("network_event=$id utc_ms=${wall()} operation=$op stage=started")
        try{return block().also{
            val value="network_event=$id utc_ms=${wall()} operation=$op stage=complete elapsed_ms=${elapsed(started)}"
            synchronized(this){lastSuccess=value;add(value)}
        }}catch(failure:Exception){
            val value="network_event=$id utc_ms=${wall()} operation=$op stage=failed elapsed_ms=${elapsed(started)} reason=${reason(failure)}"
            synchronized(this){lastFailure=value;add(value)};throw failure
        }
    }
    private fun elapsed(start:Long)=((monotonic()-start).coerceAtLeast(0))/1_000_000
    @Synchronized fun snapshot():String="network_last_success=$lastSuccess\nnetwork_last_failure=$lastFailure\n"+rows.joinToString("\n")
    companion object {
        private val operations=setOf("remember_challenge","remember_resume","remember_revoke","remember_enroll_challenge","remember_enroll","session_info","presence","friend_state","messaging_bootstrap","push_register","push_hint","login","logout","register","search","people_search","friend_request","friend_respond","friend_cancel","group_state","group_invite_state","group_register","group_invite_account","group_invite_respond","archive_descriptor_register","archive_grant_register","archive_authorization_state","mailbox_register","mailbox_revoke","mailbox_keys","mailbox_store","mailbox_fetch","mailbox_ack","mailbox_receipts","mailbox_eligibility_snapshot","mailbox_eligibility_validate","events","relay_attach")
        private fun operation(value:String)=if(value in operations)value else "other"
        private fun reason(failure:Exception):String=when {
            failure is ServerRejected&&failure.code in setOf("session_invalid","session_superseded","relay_duplicate_attachment","relay_pair_timeout","relay_not_authorized","device_session_mismatch","device_account_mismatch")->failure.code
            failure is ServerRejected->"server_rejected"
            failure is java.net.SocketTimeoutException->"transport_timeout"
            failure is java.io.IOException->"transport_io"
            else->"client_validation"
        }
    }
}
