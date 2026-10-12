package com.peerchat.android.application

data class AttachmentSnapshot(val id:String,val peer:String,val name:String,val size:Long,
    val state:Int,val outgoing:Boolean,val done:Long,val created:Long,val error:String,val localPath:String) {
    val complete get()=state==4
    val awaitingAcceptance get()=state==2&&!outgoing
    val active get()=state in listOf(0,1,2,3,7)
}
internal fun attachmentSnapshots(bytes:ByteArray):List<AttachmentSnapshot> {
    val r=NativeRecord(bytes,30);val count=r.number();require(count<=64)
    val rows=List(count.toInt()){
        val id=r.text();val peer=r.text();val name=r.text();val size=r.number();val state=r.number();val outgoing=r.number();val done=r.number();val created=r.number();val error=r.text();val path=r.text()
        require(id.matches(Regex("[0-9a-f]{32}"))&&peer.matches(Regex("[0-9a-f]{64}"))&&name.toByteArray().size<=240&&size<=256L*1024*1024&&state in 0..8&&outgoing in 0..1&&done<=size&&error.length<=256)
        require(path.isEmpty()||state==4L&&path.startsWith("/")&&!path.contains('\u0000'))
        AttachmentSnapshot(id,peer,name,size,state.toInt(),outgoing==1L,done,created,error,path)
    };r.end();require(rows.map{it.id}.distinct().size==rows.size);return rows
}
