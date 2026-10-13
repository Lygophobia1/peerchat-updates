package com.peerchat.android.application

import org.json.JSONObject

// Public metadata execution on the existing MessagingBackend worker/gate.
// Native Enrollment alone verifies identity, signatures, consent and permissions.
internal class ArchiveEnrollmentExecution(
    private val native: (Int,ByteArray)->ByteArray,
    private val rpc: (String,JSONObject)->JSONObject
) {
    private var revision=0L
    private fun command(operation:Int,fields:(NativeRecordWriter)->Unit = {}):ByteArray =
        NativeRecordWriter(31).also {it.text("PeerChat/ArchiveApplication/v1");it.number(1);it.number(operation.toLong());fields(it)}.bytes()
    private fun invalidate(){native(9,command(9))}
    fun authorizationDiagnostics(account:Long,generation:Long):String {
        val r=NativeRecord(native(22,command(22){it.number(account);it.number(generation)}),31)
        require(r.text()=="PeerChat/ArchiveApplication/v1"&&r.number()==1L&&r.number()==22L)
        val text=r.text();require(text.length<=49152);r.end();return text
    }
    fun refreshCurrent(account:Long,generation:Long,device:String):ByteArray {
        try{refreshState(account,generation,device);return native(3,command(3))}catch(error:Exception){invalidate();throw error}
    }
    fun startSync(account:Long,generation:Long,source:String){native(12,command(12){w->w.number(account);w.number(generation);w.text(source);w.number(1)})}
    fun pauseSync(account:Long,generation:Long){native(14,command(14){w->w.number(account);w.number(generation)})}
    fun incoming(account:Long,generation:Long,scope:ArchiveRelayScope){native(15,command(15){w->w.number(account);w.number(generation);w.blob(scope.binding())})}
    fun progress(account:Long,generation:Long):ArchiveSyncProgress {
        val r=NativeRecord(native(23,command(23){w->w.number(account);w.number(generation)}),31)
        require(r.text()=="PeerChat/ArchiveApplication/v1"&&r.number()==1L&&r.number()==23L)
        val state=r.text();val processed=r.number();val conversations=r.number();val complete=r.number();val running=r.number();val source=r.text();val attachments=r.number()
        val received=r.number();val revision=r.number();val success=r.number();val scan=r.number();val enabled=r.number()
        var deferred=0L;var rejected=0L;if(r.hasRemaining()){require(r.text()=="PeerChat/ArchiveScanAccounting/v1");deferred=r.number();rejected=r.number();require(deferred>=0&&rejected>=0)}
        var activity=-1;var outcome=0;var at=0L;var imported=0L;var completed=0L;var unresolved=0L;var resultSource="";var provided=0L;var providers=emptyList<String>()
        if(r.hasRemaining()){require(r.text()=="PeerChat/ArchiveStatus/v1");activity=r.number().toInt();outcome=r.number().toInt();require(activity in 0..6&&outcome in 0..4);at=r.number();imported=r.number();completed=r.number();unresolved=r.number();resultSource=r.text();provided=r.number();val count=r.number();require(count in 0..8&&at in 0..253402300799999L&&imported>=0&&completed>=0&&unresolved>=0&&provided>=0&&(resultSource.isEmpty()||resultSource.matches(Regex("[0-9a-f]{64}"))));providers=List(count.toInt()){r.text().also{require(it.matches(Regex("[0-9a-f]{64}")))}};require(providers.distinct().size==providers.size)}
        require(state.length<=128&&complete<=1&&running<=1&&enabled<=1&&success<=253402300799999L&&(source.isEmpty()||source.matches(Regex("[0-9a-f]{64}"))));r.end()
        return ArchiveSyncProgress(state,processed,conversations,complete==1L,running==1L,source,attachments,received,revision,success,scan,enabled==1L,generation,deferred,rejected,activity,outcome,at,imported,completed,unresolved,resultSource,provided,providers)
    }
    fun devices(account:Long,generation:Long,base:ByteArray):List<ArchiveDeviceSnapshot>{
        val rows=archiveDeviceSnapshots(base)
        val r=NativeRecord(native(21,command(21){it.number(account);it.number(generation);it.number(2)}),31)
        require(r.text()=="PeerChat/ArchiveApplication/v1"&&r.number()==1L&&r.number()==21L&&r.number()==rows.size.toLong())
        val grants=mutableMapOf<String,Pair<Boolean,Boolean>>()
        repeat(rows.size){val peer=r.text();val read=r.number();val serve=r.number();require(read<=1&&serve<=1&&rows.any{it.peerId==peer}&&!grants.containsKey(peer));grants[peer]=(read==1L) to (serve==1L)}
        require(r.text()=="PeerChat/ArchiveDeviceApproval/v2"&&r.number()==rows.size.toLong())
        fun flag()=r.number().also{require(it<=1)}==1L
        val approval=mutableMapOf<String,ArchiveDeviceSnapshot>()
        repeat(rows.size){
            val peer=r.text();val row=rows.single{it.peerId==peer};require(!approval.containsKey(peer))
            val value=row.copy(grantedRead=grants.getValue(peer).first,grantedServe=grants.getValue(peer).second,
                approvalKnown=true,mayAuthorize=flag(),pairAuthorized=flag(),localGrantPresent=flag(),localGrantRead=flag(),localGrantServe=flag(),maySetUp=flag(),approvalIssuer=r.text())
            require(value.localGrantPresent==(value.localGrantRead||value.localGrantServe))
            require(!value.maySetUp||value.local&&!value.mayAuthorize&&!value.localGrantPresent)
            require(value.approvalIssuer.isEmpty()||rows.any{it.peerId==value.approvalIssuer})
            approval[peer]=value
        }
        r.end();return rows.map{approval.getValue(it.peerId)}
    }
    // Same scoped native operations as Windows. Refresh/render never calls these.
    fun prepareFirstDevice(account:Long,generation:Long,local:String,confirmed:Boolean){
        if(!confirmed)return
        val row=devices(account,generation,native(3,command(3))).single{it.local&&it.peerId==local}
        check(row.approvalKnown&&row.maySetUp){"First-device setup is not eligible; retained history authorization is unchanged."}
        consent(account,generation,4,local,0,true)
        consent(account,generation,5,local,3,true)
    }
    fun confirmExistingIssuer(account:Long,generation:Long,issuer:String,confirmed:Boolean){
        if(!confirmed)return
        val row=devices(account,generation,native(3,command(3))).single{it.local}
        check(row.approvalKnown&&row.approvalIssuer==issuer){"This registered authorizer is not currently eligible for approval."}
        consent(account,generation,4,issuer,0,true)
    }
    private fun proof(value:String,max:Int):ByteArray {
        require(value.length<=((max+2)/3)*4)
        return java.util.Base64.getDecoder().decode(value).also {require(it.size in 1..max)}
    }
    private fun request(operation:String,fields:JSONObject=JSONObject()):JSONObject =
        rpc(operation,fields).also {require(it.getBoolean("ok")&&it.getLong("archiveVersion")==1L)}
    private fun refreshState(account:Long,generation:Long,device:String){
        val reply=request("archive_authorization_state")
        require(reply.getLong("accountId")==account)
        val epoch=reply.getString("serverEpoch");require(epoch.isNotEmpty()&&epoch.length<=128)
        val devices=reply.getJSONArray("devices");val grants=reply.getJSONArray("grants")
        require(devices.length()<=32&&grants.length()<=64&&revision<Long.MAX_VALUE)
        revision=maxOf(revision+1,System.nanoTime())
        native(2,command(2){w->
            w.number(account);w.number(generation);w.number(revision);w.text(device);w.text(epoch)
            w.number(devices.length().toLong())
            repeat(devices.length()){w.blob(proof(devices.getJSONObject(it).getString("descriptor"),1024))}
            w.number(grants.length().toLong())
            repeat(grants.length()){
                val row=grants.getJSONObject(it);w.blob(proof(row.getString("grant"),2048))
                require(row.get("currentlyEligible") is Boolean);w.number(if(row.getBoolean("currentlyEligible"))1 else 0)
            }
        })
    }
    fun refresh(account:Long,generation:Long,device:String):ByteArray {
        try{
            invalidate();require(account>0&&generation>0&&device.matches(Regex("[0-9a-f]{64}")))
            val descriptor=native(1,command(1));require(descriptor.size in 1..1024)
            require(request("archive_descriptor_register",JSONObject().put("proof",java.util.Base64.getEncoder().encodeToString(descriptor))).getString("state")=="accepted")
            refreshState(account,generation,device)
            return native(3,command(3))
        }catch(error:Exception){invalidate();throw error}
    }
    fun publishPending(account:Long,generation:Long,device:String):Int {
        try{
            refresh(account,generation,device)
            val record=NativeRecord(native(8,command(8)),31)
            require(record.text()=="PeerChat/ArchiveApplication/v1"&&record.number()==1L&&record.number()==8L)
            val count=record.number();require(count<=32)
            val proofs=List(count.toInt()){record.bytes().also {require(it.size in 1..2048)}};record.end()
            var sent=0
            proofs.take(4).forEach {
                require(request("archive_grant_register",JSONObject().put("proof",java.util.Base64.getEncoder().encodeToString(it))).getString("state")=="accepted");sent++
            }
            refreshState(account,generation,device);return sent
        }catch(error:Exception){invalidate();throw error}
    }
    fun consent(account:Long,generation:Long,action:Int,device:String,capabilities:Long,consent:Boolean):ByteArray {
        require(account>0&&generation>0&&action in 4..7&&device.matches(Regex("[0-9a-f]{64}"))&&capabilities in 0..3)
        return native(10,command(10){w->w.number(account);w.number(generation);w.number(action.toLong());w.text(device);if(action==5)w.number(capabilities);w.number(if(consent)1 else 0)})
    }
    fun historySync(account:Long,generation:Long,enabled:Boolean?=null):Boolean {
        val r=NativeRecord(native(11,command(11){w->w.number(account);w.number(generation);w.number(if(enabled!=null)1 else 0);if(enabled!=null)w.number(if(enabled)1 else 0)}),31)
        require(r.text()=="PeerChat/ArchiveApplication/v1"&&r.number()==1L&&r.number()==11L)
        val value=r.number();require(value<=1);r.end();return value==1L
    }
}
