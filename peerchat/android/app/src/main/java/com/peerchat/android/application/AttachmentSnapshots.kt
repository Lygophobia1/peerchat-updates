package com.peerchat.android.application

data class AttachmentSnapshot(val id:String,val peer:String,val name:String,val size:Long,
    val state:Int,val outgoing:Boolean,val done:Long,val created:Long,val error:String,val localPath:String,
    val statisticsKnown:Boolean=false,val statisticsBytes:Long=0,val durationMs:Long=0,val rateBits:Long=0,val etaReliable:Boolean=false,val cachePhase:String="",val recipients:List<AttachmentRecipientSnapshot> = emptyList(),val groupId:String="",val thumbnail:String="") {
    val complete get()=state==4
    val awaitingAcceptance get()=state==2&&!outgoing
    val active get()=state in listOf(0,1,2,3,7)
    val awaitingUploadStart get()=outgoing&&active&&done==0L&&cachePhase.isNotEmpty()&&state!=7
    val statisticsText:String get(){
        if(awaitingUploadStart)return "File size: $size bytes · Upload has not started"
        fun decimal(n:Double)=String.format(java.util.Locale.ROOT,"%.2f",n)
        val percent=if(size>0)(100.0*done/size).coerceIn(0.0,100.0)else if(complete)100.0 else 0.0
        val average=if(statisticsKnown&&durationMs>0)decimal(statisticsBytes*8000.0/durationMs/1000000)+" Mbps" else "unavailable"
        val bytes="$done / $size bytes · ${decimal(percent)}%"
        if(complete)return "$bytes\nAverage $average · Duration "+if(statisticsKnown)"${decimal(durationMs/1000.0)} s" else "unavailable"
        val eta=if(state==3&&etaReliable&&rateBits>0&&done<size)kotlin.math.ceil((size-done)*8.0/rateBits).toLong().takeIf{it in 1..604800}?.let{" · ETA $it s"}?:"" else ""
        val current=if(statisticsKnown)decimal(if(state==3)rateBits/1000000.0 else 0.0)+" Mbps" else "unavailable"
        return "$bytes\nCurrent $current · Average $average$eta"
    }
}
internal fun attachmentSnapshots(bytes:ByteArray):List<AttachmentSnapshot> {
    val r=NativeRecord(bytes,30);val count=r.number();require(count<=64)
    val rows=List(count.toInt()){
        val id=r.text();val peer=r.text();val name=r.text();val size=r.number();val state=r.number();val outgoing=r.number();val done=r.number();val created=r.number();val error=r.text();val path=r.text()
        require(id.matches(Regex("[0-9a-f]{32}"))&&peer.matches(Regex("[0-9a-f]{64}"))&&name.toByteArray().size<=240&&size<=5L*1024*1024*1024&&state in 0..8&&outgoing in 0..1&&done<=size&&error.length<=256)
        require(path.isEmpty()||state==4L&&path.startsWith("/")&&!path.contains('\u0000'))
        AttachmentSnapshot(id,peer,name,size,state.toInt(),outgoing==1L,done,created,error,path)
    };require(rows.map{it.id}.distinct().size==rows.size)
    if(!r.hasRemaining())return rows
    require(r.text()=="PeerChat/TransferStatisticsSnapshot/v1"&&r.number()==rows.size.toLong())
    val measured=mutableMapOf<String,AttachmentSnapshot>();repeat(rows.size){
        val id=r.text();val row=rows.single{it.id==id};require(!measured.containsKey(id));val known=r.number();val bytes=r.number();val duration=r.number();val rate=r.number();val eta=r.number()
        require(known in 0..1&&eta in 0..1&&bytes<=row.size&&duration<=604800000&&rate<=row.size*8000)
        measured[id]=row.copy(statisticsKnown=known==1L,statisticsBytes=bytes,durationMs=duration,rateBits=rate,etaReliable=eta==1L)
    }
    if(r.hasRemaining()){
        require(r.text()=="PeerChat/DMCachePhases/v1");val phases=r.number();require(phases<=rows.size)
        val seen=mutableSetOf<String>();repeat(phases.toInt()){
            val id=r.text();require(seen.add(id));val row=measured.getValue(id);val phase=r.text()
            require(phase in setOf("Recipients verified saved","Notice stored; safe to disconnect","Upload complete; storing notices",
                "Checking cached upload authorization","Preparing encrypted file","Uploading encrypted file","Offer delivered; uploading encrypted file","Accepted; sender is preparing the cached file",
                "Verified saved","Downloaded; verifying","Cached file offered","Downloading encrypted file"))
            measured[id]=row.copy(cachePhase=phase)
        }
    }
    if(r.hasRemaining()){
        require(r.text()=="PeerChat/AttachmentThumbnails/v1");val count=r.number();require(count<=rows.size)
        val seen=mutableSetOf<String>();repeat(count.toInt()){val id=r.text();require(seen.add(id));val bytes=r.bytes();require(bytes.size<=2048);measured[id]=measured.getValue(id).copy(thumbnail=java.util.Base64.getEncoder().encodeToString(bytes))}
    }
    r.end();return rows.map{measured.getValue(it.id)}
}


data class AttachmentRecipientSnapshot(val account:Long,val device:String,val state:String,val done:Long,val total:Long,val error:String)
internal fun groupAttachmentSnapshots(text:String):List<AttachmentSnapshot>{
    require(text.toByteArray().size<=2*1024*1024)
    val items=org.json.JSONObject(text).getJSONArray("items");require(items.length()<=128)
    return List(items.length()){i->val row=items.getJSONObject(i)
        val id=row.getString("id");val name=row.getString("name");val size=row.getLong("size");val total=row.getLong("transferSize")
        require(id.matches(Regex("[0-9a-f]{32}"))&&size in 0..1536L*1024*1024&&total>=0&&name.toByteArray().size<=240)
        val sending=row.getBoolean("sending");val phase=row.getString("state");val verified=row.getBoolean("plaintextVerified")
        val state=when(phase){"canceled"->5;"failed"->6;"expired"->8;"complete"->4;"preparing"->0;"paused"->7;"available"->if(sending)3 else 2;else->if(!sending&&!verified&&phase=="offered")2 else 3}
        val path=row.getString("path");require(path.isEmpty()||path.startsWith("/")&&!path.contains('\u0000')&&(sending||verified))
        val recipients=row.getJSONArray("recipients");require(recipients.length()<=256)
        val progress=List(recipients.length()){n->val recipient=recipients.getJSONObject(n);val done=recipient.getLong("done");require(done in 0..total)
            AttachmentRecipientSnapshot(recipient.getLong("account"),recipient.getString("device"),recipient.getString("state"),done,total,recipient.getString("error"))}
        val done=row.getLong("done");require(done in 0..total)
        AttachmentSnapshot(id,row.getString("senderPeer"),name,size,if(verified)4 else state,sending,
            if(verified||phase=="complete")size else if(total>0)(done.toDouble()/total*size).toLong().coerceIn(0,size)else 0,
            row.getLong("time"),row.getString("error"),path,cachePhase=when(phase){"verifying_offer"->"Verifying encrypted offer";"preparing"->"Preparing encrypted group file";"waiting_for_upload"->"Accepted; waiting for the sender to finish uploading";"available"->if(sending)"Group file uploaded" else "Group file offered";"uploading"->"Uploading encrypted group file";"downloading"->"Downloading encrypted group file";"verifying"->"Verifying downloaded file";"complete"->if(sending)"Recipients verified saved" else "Verified saved";"canceled"->"Canceled";"paused"->"Paused";"expired"->"Cached file expired";else->"Group cached transfer"},recipients=progress,groupId=row.getString("group"),thumbnail=row.optString("thumbnail").let{require(it.length<=2732);if(it.isNotEmpty())require(java.util.Base64.getDecoder().decode(it).size<=2048);it})
    }
}
