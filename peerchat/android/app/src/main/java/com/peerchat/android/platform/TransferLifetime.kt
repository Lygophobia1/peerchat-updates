package com.peerchat.android.platform

import com.peerchat.android.application.AttachmentSnapshot

/** Execution admission only; never creates, retries or advances a transfer. */
internal class TransferLifetime(private val started:Long) {
    val ids=linkedSetOf<String>()
    fun add(id:String):Boolean {
        if(!id.matches(Regex("[0-9a-f]{32}"))||ids.size>=64&&!ids.contains(id))return false
        ids.add(id);return true
    }
    fun finished(rows:List<AttachmentSnapshot>,now:Long):Boolean {
        if(now-started>=BUDGET_MS)return true
        val byId=rows.associateBy{it.id}
        if(ids.any{byId[it]==null}&&now-started<10_000)return false // allow projection to publish
        return ids.all{byId[it]?.state in setOf(4,5,6,8)||byId[it]==null}
    }
    companion object { const val BUDGET_MS=30L*60*1000 }
}
