package com.peerchat.android.application

import com.peerchat.android.GroupBridge
import com.peerchat.android.platform.ServerAccounts
import com.peerchat.android.platform.AccountSession
import org.json.JSONObject
import java.util.Base64

data class GroupMessageSnapshot(val id:String,val device:String,val account:Long,val timestamp:Long,val order:Long,val sequence:Long,val status:String,val text:String)
internal val groupMessageOrder=compareBy<GroupMessageSnapshot>{it.timestamp}.thenBy{it.device}.thenBy{it.id}
internal fun groupHistoryBefore(messages:List<GroupMessageSnapshot>):Long=messages.filter{it.sequence>0}.minWithOrNull(groupMessageOrder)?.sequence?:Long.MAX_VALUE
data class GroupMemberSnapshot(val account:Long,val device:String,val role:Long,val invited:Boolean)
data class GroupConversationSnapshot(val id:String,val legacyId:String,val epoch:Long,val name:String,val ready:Boolean,val members:Long,
    val messages:List<GroupMessageSnapshot> = emptyList(),val unread:Int=0,val memberRows:List<GroupMemberSnapshot> = emptyList(),val hasOlder:Boolean=true,val attachments:List<AttachmentSnapshot> = emptyList())
data class GroupInvitationSnapshot(val id:String,val group:String,val inviter:String)

/** Execution of existing server RPCs and opaque native group commands. No
 * membership persistence, cryptography, key custody or peer routing lives here. */
internal class GroupExecution(private val native:(Long,Int,ByteArray)->ByteArray?={h,o,b->GroupBridge.request(h,o,b)}) {
    internal var server:(ServerAccounts,String,JSONObject)->JSONObject={owner,operation,body->owner.request(operation,body)}
    internal class PreparedState internal constructor(
        val owner:ServerAccounts,val session:AccountSession,val handle:Long,val account:Long,val generation:Long,
        val groups:List<PreparedGroup>,val invitations:List<GroupInvitationSnapshot>
    )
    internal class PreparedGroup internal constructor(val id:String,val legacy:String,val roster:Long,val distributor:String,
        val proof:ByteArray,val approvals:List<Pair<Long,ByteArray>>,val members:List<PreparedMember>)
    internal class PreparedMember internal constructor(val account:Long,val device:String,val role:Long,val invited:Long)
    var diagnostic="Group authorization not loaded";private set
    var invitations=emptyList<GroupInvitationSnapshot>();private set
    private val history=mutableMapOf<String,List<GroupMessageSnapshot>>()
    private val unread=mutableMapOf<String,Int>()
    private val older=mutableMapOf<String,Boolean>()
    private val referenceCursors=mutableMapOf<String,Long>()
    private var historyRevision=-1L
    private val historyRefresh=linkedSetOf<String>()
    private fun command(handle:Long,op:Int,account:Long,generation:Long,write:(NativeRecordWriter)->Unit={}):NativeRecord{
        val w=NativeRecordWriter(27);w.text("PeerChat/AndroidGroups/v1");w.number(op.toLong());w.number(account);w.number(generation);write(w)
        val r=NativeRecord(checkNotNull(native(handle,op,w.bytes())),27);require(r.text()=="PeerChat/AndroidGroups/v1"&&r.number()==op.toLong());return r
    }
    private fun decode(text:String,max:Int):ByteArray {require(text.length<=((max+2)/3)*4);return Base64.getDecoder().decode(text).also{require(it.size<=max)}}
    fun fetch(owner:ServerAccounts,handle:Long,account:Long,generation:Long):PreparedState {
        val session=checkNotNull(owner.session);require(account==session.accountId&&generation>0)
        val reply=server(owner,"group_state",JSONObject().put("publicIdentity",owner.device));val rows=reply.getJSONArray("groups");require(rows.length()<=128)
        val groups=List(rows.length()){i->val row=rows.getJSONObject(i)
            val approvals=row.optJSONArray("deviceApprovals");require((approvals?.length()?:0)<=32)
            val preparedApprovals=if(approvals==null)emptyList()else List(approvals.length()){n->val a=approvals.getJSONObject(n);a.getLong("appliedGeneration") to decode(a.getString("proof"),4096)}
            val members=row.getJSONArray("members");require(members.length()<=256)
            val preparedMembers=List(members.length()){n->val d=members.getJSONObject(n)
                PreparedMember(d.getLong("accountId"),d.getString("publicIdentity"),when(d.getString("role")){"owner"->3;"admin"->2;"member"->1;else->error("Invalid group role")},
                    when(d.getString("state")){"member"->0;"invited"->1;else->error("Invalid group membership")})}
            PreparedGroup(row.getString("groupId"),row.getString("legacyGroupId"),row.getLong("rosterGeneration"),row.getString("distributorIdentity"),
                decode(row.optString("creationProof"),2048),preparedApprovals,preparedMembers)
        }
        val invites=server(owner,"group_invite_state",JSONObject()).getJSONArray("invites");require(invites.length()<=128)
        val preparedInvites=List(invites.length()){i->val row=invites.getJSONObject(i);GroupInvitationSnapshot(row.getString("inviteId"),row.getString("groupId"),row.getString("inviterUsername")).also{require(it.id.matches(Regex("[0-9a-f]{32}"))&&it.group.matches(Regex("[0-9a-f]{32}"))&&it.inviter.length<=64)}}
        check(owner.session===session){"Group fetch session changed"}
        return PreparedState(owner,session,handle,account,generation,groups,preparedInvites)
    }
    fun apply(handle:Long,account:Long,generation:Long,state:PreparedState){
        check(handle==state.handle&&account==state.account&&generation==state.generation&&state.owner.session===state.session){"Group fetch scope changed"}
        command(handle,1,account,generation){w->w.number(state.groups.size.toLong());state.groups.forEach{row->
            w.text(row.id);w.text(row.legacy);w.number(row.roster);w.text(row.distributor)
            val evidence=NativeRecordWriter(5);evidence.text("PeerChat/group-roster-evidence/v1");evidence.blob(row.proof)
            evidence.number(row.approvals.size.toLong());row.approvals.forEach{(epoch,proof)->evidence.number(epoch);evidence.blob(proof)};w.blob(evidence.bytes())
            w.number(row.members.size.toLong());row.members.forEach{d->w.number(d.account);w.text(d.device);w.number(d.role);w.number(d.invited)}
        }}.end()
        invitations=state.invitations
        diagnostic="Authenticated group snapshots=${state.groups.size} invitations=${invitations.size}; native membership/key checks active"
    }
    fun refresh(owner:ServerAccounts,handle:Long,account:Long,generation:Long){
        apply(handle,account,generation,fetch(owner,handle,account,generation))
    }
    fun snapshot(handle:Long,account:Long,generation:Long):List<GroupConversationSnapshot>{
        val r=command(handle,2,account,generation);val count=r.number();require(count<=128)
        val groups=List(count.toInt()){GroupConversationSnapshot(r.text(),r.text(),r.number(),r.text(),r.number()==1L,r.number()).let{g->
            require(g.members<=256);val members=List(g.members.toInt()){GroupMemberSnapshot(r.number(),r.text(),r.number(),r.number()==1L)}
            g.copy(messages=history[g.id]?:emptyList(),unread=unread[g.id]?:0,memberRows=members,hasOlder=older[g.id]?:true)
        }}.also{
            val ids=it.map{g->g.id}.toSet()
            if(r.hasRemaining()){
                require(r.text()=="PeerChat/GroupHistoryRevision/v1");val revision=r.number()
                if(revision!=historyRevision){historyRefresh.addAll(ids);historyRevision=revision}
            }
            r.end();history.keys.retainAll(ids);unread.keys.retainAll(ids);older.keys.retainAll(ids);referenceCursors.keys.retainAll(ids);historyRefresh.retainAll(ids)
        }
        // Bounded preview/history readout from native storage, one group
        // per pass. No unbounded eager history load and no membership cache DB.
        groups.filter{!history.containsKey(it.id)||it.id in historyRefresh}.take(1).forEach{load(handle,account,generation,it.id);historyRefresh.remove(it.id)}
        return groups.map{it.copy(messages=history[it.id]?:emptyList(),hasOlder=older[it.id]?:true)}
    }
    fun load(handle:Long,account:Long,generation:Long,id:String,before:Long=Long.MAX_VALUE,markRead:Boolean=false):List<GroupMessageSnapshot>{
        val r=command(handle,3,account,generation){it.text(id);it.number(before);it.text("PeerChat/GroupReferencePage/v1");it.number(referenceCursors[id]?:0)};val count=r.number();require(count<=8)
        val page=List(count.toInt()){GroupMessageSnapshot(r.text(),r.text(),r.number(),r.number(),r.number(),r.number(),r.text(),r.text())}.toMutableList()
        val ready=if(r.hasRemaining()){require(r.text()=="PeerChat/GroupHistoryPage/v1");r.number().also{require(it<=1)}==1L}else true
        var referenceMore=false
        if(r.hasRemaining()){require(r.text()=="PeerChat/GroupReferencePage/v1");val cursor=r.number();val more=r.number();val references=r.number();require(cursor>=(referenceCursors[id]?:0)&&more<=1&&references<=8);referenceMore=more==1L
            repeat(references.toInt()){val message=r.text();val attachment=r.text();val device=r.text();val sender=r.number();val epoch=r.number();val size=r.number();require(message.matches(Regex("[0-9a-f]{32}"))&&attachment.matches(Regex("[0-9a-f]{32}"))&&device.matches(Regex("[0-9a-f]{64}"))&&sender>0&&epoch>0&&size>=0)
                page.add(GroupMessageSnapshot(message,device,sender,0,0,0,"attachment_reference","Encrypted attachment reference\nPrivate metadata and original time/order unavailable. Download requires existing device authorization; cached data may have expired."))}
            referenceCursors[id]=cursor
        };r.end()
        if(!ready){older[id]=true;historyRefresh.add(id);diagnostic="Preparing protected group history ordering";return emptyList()}
        val merged=(page+(history[id]?:emptyList())).distinctBy{it.id}.sortedWith(groupMessageOrder)
        history[id]=if(before==Long.MAX_VALUE)merged.takeLast(200)else merged.take(200)
        older[id]=count==8L||referenceMore;if(markRead)unread[id]=0;return page
    }
    fun create(owner:ServerAccounts,handle:Long,account:Long,generation:Long,name:String,username:String){
        val r=command(handle,4,account,generation){it.text(name);it.text(username)};val proof=r.bytes();r.end()
        owner.request("group_register",JSONObject().put("publicIdentity",owner.device).put("createProof",Base64.getEncoder().encodeToString(proof)))
    }
    fun invite(owner:ServerAccounts,id:String,epoch:Long,target:Long){owner.request("group_invite_account",JSONObject().put("publicIdentity",owner.device).put("groupId",id).put("rosterGeneration",epoch.toString()).put("targetAccountId",target.toString()))}
    fun respond(owner:ServerAccounts,id:String,accept:Boolean){require(invitations.any{it.id==id});owner.request("group_invite_respond",JSONObject().put("publicIdentity",owner.device).put("inviteId",id).put("action",if(accept)"accept" else "decline"))}
    fun send(handle:Long,account:Long,generation:Long,id:String,text:String,queue:(JSONObject)->Boolean):String{
        val r=command(handle,5,account,generation){it.text(id);it.text(text)};val result=r.text();check(result=="accepted"){"Native group send rejected"}
        val message=r.text();val epoch=r.number();val wire=r.bytes();r.end()
        // Native creation above is the durable commit. A later transport or
        // readout failure must not tell the composer that no message was saved.
        try{
            val queued=queue(JSONObject().put("type","group_send").put("groupId",id).put("messageId",message).put("rosterGeneration",epoch.toString()).put("envelope",Base64.getEncoder().encodeToString(wire)))
            status(handle,account,generation,id,message,if(queued)"Awaiting server acceptance (online recipients only)" else "Not submitted; server event channel unavailable",0)
            load(handle,account,generation,id)
        }catch(_:Exception){diagnostic="Group message durably created; submission/readout pending"}
        return message
    }
    fun retry(handle:Long,account:Long,generation:Long,id:String,message:String,queue:(JSONObject)->Boolean){
        val r=command(handle,8,account,generation){it.text(id);it.text(message)}
        require(r.text()==message);val epoch=r.number();val wire=r.bytes();r.end()
        check(queue(JSONObject().put("type","group_send").put("groupId",id).put("messageId",message).put("rosterGeneration",epoch.toString()).put("envelope",Base64.getEncoder().encodeToString(wire))))
        status(handle,account,generation,id,message,"Awaiting server acceptance (online recipients only)",0)
        load(handle,account,generation,id)
    }
    private fun status(handle:Long,account:Long,generation:Long,id:String,message:String,state:String,stage:Long){command(handle,7,account,generation){it.text(id);it.text(message);it.text(state);it.number(stage)}.end()}
    fun event(event:JSONObject,handle:Long,account:Long,generation:Long,queue:(JSONObject)->Boolean):Boolean{
        val type=event.optString("type");if(type!in setOf("group_message","group_delivery"))return false
        val id=event.getString("groupId");val message=event.getString("messageId");require(id.matches(Regex("[0-9a-f]{32}"))&&message.matches(Regex("[0-9a-f]{32}")))
        if(type=="group_message"){
            val wire=decode(event.getString("envelope"),65790)
            val r=command(handle,6,account,generation){it.text(id);it.text(message);it.blob(wire)};val result=r.text();r.end()
            val ack=when(result){"accepted"->"accepted";"duplicate_message"->"duplicate_message";else->"rejected"}
            queue(JSONObject().put("type","group_ack").put("groupId",id).put("messageId",message).put("state",ack))
            if(result=="accepted"){val previous=unread[id]?:0;load(handle,account,generation,id);unread[id]=(previous+1).coerceAtMost(999)}
        }else{
            val state=event.getString("state");val stage=when(state){"recipient_acknowledged"->2L;"server_accepted_online_only","duplicate_server_request"->1L;else->0L}
            val safe=when(state){"recipient_acknowledged"->{val counts=listOf("acceptedCount","duplicateCount","rejectedCount","sessionCount").map{event.getLong(it).also{n->require(n in 0..64)}};"Recipients: ${counts[0]} accepted, ${counts[1]} duplicates, ${counts[2]} rejected / ${counts[3]} sessions"}
                "server_accepted_online_only","duplicate_server_request"->"Server accepted (online recipients only; not E2E delivery)"
                "delivery_unknown_after_disconnect"->"Delivery unknown after disconnect"
                else->"Group delivery rejected or unavailable"}
            status(handle,account,generation,id,message,safe,stage);if(history.containsKey(id))load(handle,account,generation,id)
        }
        return true
    }
    fun resetPending(handle:Long,account:Long,generation:Long){command(handle,13,account,generation).end()}
    fun submitPending(handle:Long,account:Long,generation:Long,queue:(JSONObject)->Boolean):Boolean {
        val r=command(handle,12,account,generation);val more=r.number()==1L;require(r.number()<=8);val count=r.number();require(count<=4)
        val batch=List(count.toInt()) {val id=r.text();val message=r.text();val epoch=r.number();val wire=r.bytes()
            Triple(id,message,JSONObject().put("type","group_send").put("groupId",id).put("messageId",message).put("rosterGeneration",epoch.toString()).put("envelope",Base64.getEncoder().encodeToString(wire)))};r.end()
        try {
            for((id,message,event) in batch){if(!queue(event)){resetPending(handle,account,generation);return true}
                status(handle,account,generation,id,message,"Awaiting server acceptance (online recipients only)",0)}
            return more
        }catch(failure:Exception){try{resetPending(handle,account,generation)}catch(_:Exception){};throw failure}
    }
    fun discardLocalHistory(id:String,ids:Set<String>?,hidden:(String)->Boolean={false}) {
        if(ids==null){history.remove(id);unread[id]=0}else history[id]=history[id].orEmpty().filter{it.id !in ids&&!hidden(it.id)}
        referenceCursors.remove(id);older[id]=true;historyRefresh.add(id)
    }
    fun reset(){invitations=emptyList();history.clear();unread.clear();older.clear();referenceCursors.clear();historyRefresh.clear();historyRevision=-1;diagnostic="Group authorization not loaded"}
    fun controlSettled(handle:Long,account:Long,generation:Long,attempt:Long):Boolean{
        val r=command(handle,9,account,generation){it.number(attempt)};val settled=r.number()==1L;r.end();return settled
    }
    fun finishControl(handle:Long,account:Long,generation:Long,attempt:Long):Boolean{
        val r=command(handle,10,account,generation){it.number(attempt)};val finished=r.number()==1L;r.end();return finished
    }
    fun incomingControl(event:JSONObject,handle:Long,account:Long,generation:Long){
        command(handle,11,account,generation){it.text(event.getString("groupId"));it.number(event.getLong("rosterGeneration"));it.number(event.getLong("accountId"));it.text(event.getString("publicIdentity"))}.end()
    }
    fun invalidate(handle:Long,account:Long,generation:Long){
        command(handle,0,account,generation).end();invitations=emptyList();diagnostic="Group authorization invalidated; protected history retained"
    }
}
