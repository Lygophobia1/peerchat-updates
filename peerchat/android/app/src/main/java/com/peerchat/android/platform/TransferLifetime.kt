package com.peerchat.android.platform

import com.peerchat.android.application.AttachmentSnapshot

/** Execution admission only; never creates, retries or advances a transfer. */
internal class TransferLifetime(private val started:Long) {
    val ids=linkedSetOf<String>()
    private val preparing=linkedSetOf<String>()
    private val admitted=mutableMapOf<String,Long>()
    val preparingIds:Set<String> get()=preparing.toSet()
    fun add(id:String,now:Long=started):Boolean {
        if(!id.matches(Regex("[0-9a-f]{32}"))||ids.size>=64&&!ids.contains(id))return false
        ids.add(id);admitted.putIfAbsent(id,now);return true
    }
    fun addPreparation(id:String,now:Long=started):Boolean {
        if(!add(id,now))return false
        preparing.add(id);return true
    }
    fun finishPreparation(token:String,id:String?,now:Long):Boolean {
        if(!preparing.remove(token))return false
        ids.remove(token);admitted.remove(token)
        return id==null||add(id,now)
    }
    fun finished(rows:List<AttachmentSnapshot>,now:Long,activeHidden:Set<String> = emptySet()):Boolean {
        if(now-started>=BUDGET_MS)return true
        if(preparing.isNotEmpty()||ids.any{it in activeHidden})return false
        val byId=rows.associateBy{it.id}
        if(ids.any{byId[it]==null&&now-(admitted[it]?:started)<10_000})return false // allow projection to publish
        return ids.all{byId[it]?.state in setOf(4,5,6,8)||byId[it]==null}
    }
    companion object { const val BUDGET_MS=30L*60*1000 }
}
