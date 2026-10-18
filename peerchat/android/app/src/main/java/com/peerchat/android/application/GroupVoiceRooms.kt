package com.peerchat.android.application

import org.json.JSONObject
import java.util.UUID

data class GroupVoiceMember(val account:Long,val device:String,val participation:String,val name:String) {
    val key:String get()="$account:$device:$participation"
}
data class GroupVoiceRoom(val id:String,val name:String,val members:List<GroupVoiceMember>)
data class GroupVoiceRoomsSnapshot(val group:String,val epoch:String,val revision:Long,val roster:Long,val rooms:List<GroupVoiceRoom>)

/** Bounded, process-local projection of the existing authenticated event channel.
 * It grants no group membership: native verifies the independent group roster.
 * Channel replacement clears everything, including pending join authorization. */
internal class GroupVoiceRooms(private val now:()->Long={System.nanoTime()/1_000_000}) {
    private data class Request(val group:String,val operation:String,val expires:Long)
    private data class Incoming(val value:JSONObject,val expires:Long)
    private val states=linkedMapOf<String,GroupVoiceRoomsSnapshot>()
    private val requests=linkedMapOf<String,Request>()
    private val signals=ArrayDeque<Incoming>()
    private val results=linkedMapOf<String,String>()
    private val tracked=linkedSetOf<String>()
    fun reset(){states.clear();requests.clear();signals.clear();results.clear();tracked.clear()}
    fun snapshot(group:String)=states[group]
    fun snapshots()=states.values.toList()
    fun result(id:String)=results[id]?:if(requests.containsKey(id))"pending" else "unavailable"
    fun retain(groups:Set<String>){
        tracked.retainAll(groups);states.keys.retainAll(groups)
        requests.entries.removeAll{it.value.group !in groups}
        signals.removeAll{it.value.optString("groupId") !in groups}
    }
    fun request(group:String,operation:String,room:String="",send:(JSONObject)->Boolean):String? {
        prune();require(id(group,32)&&operation in setOf("snapshot","join_voice","leave_voice"))
        require(operation=="snapshot"||id(room,32))
        if(requests.size>=64||tracked.size>=128&&group !in tracked)return null
        if(operation=="snapshot")requests.entries.firstOrNull{it.value.group==group&&it.value.operation==operation}?.let{return it.key}
        val state=states[group]
        if(operation!="snapshot"&&state==null)return null
        val request=JSONObject().put("type","group_room").put("requestId",uuid()).put("groupId",group)
            .put("operation",operation).put("roomId",room).put("kind","").put("name","")
        state?.let{request.put("epoch",it.epoch).put("revision",it.revision).put("rosterGeneration",it.roster)}
        if(!send(request))return null
        tracked.add(group)
        return request.getString("requestId").also{requests[it]=Request(group,operation,now()+15_000)}
    }
    fun event(event:JSONObject,account:Long,device:String) {
        prune()
        val type=event.getString("type")
        if(type=="group_media_signal"){
            val group=event.getString("groupId")
            if(group !in tracked||event.getLong("targetAccountId")!=account||event.getString("targetDevice")!=device)return
            validateSignal(event)
            if(signals.size<128)signals.addLast(Incoming(event,now()+10_000))
            return
        }
        require(type in setOf("group_room_snapshot","group_room_result","group_media_result"))
        val request=if(type.endsWith("result"))requests.remove(event.getString("requestId"))?:return else null
        val state=event.optJSONObject("snapshot")?.let(::parse)
        val group=state?.group?:event.optString("groupId")
        if(group !in tracked||request!=null&&request.group!=group)return
        if(request!=null){
            val result=event.getString("state");require(result.matches(Regex("[a-z_]{1,80}")))
            if(results.size>=128)results.remove(results.keys.first())
            results[event.getString("requestId")]=result
            if(result=="group_not_authorized"){states.remove(group);signals.removeAll{it.value.optString("groupId")==group};return}
        }
        if(state!=null){
            val old=states[group]
            if(old==null||state.epoch==old.epoch&&state.revision>=old.revision&&state.roster>=old.roster)states[group]=state
        }
    }
    fun take(group:String,room:String,account:Long,device:String):List<JSONObject> {
        prune();val state=states[group]?:return emptyList();val current=state.rooms.singleOrNull{it.id==room}?:return emptyList()
        val output=mutableListOf<JSONObject>()
        repeat(signals.size){
            val row=signals.removeFirst();val s=row.value
            if(s.getString("groupId")!=group||s.getString("roomId")!=room){signals.addLast(row);return@repeat}
            if(s.getString("epoch")!=state.epoch||s.getLong("rosterGeneration")<state.roster)return@repeat
            if(s.getLong("revision")>state.revision||s.getLong("rosterGeneration")>state.roster){signals.addLast(row);return@repeat}
            val source=current.members.singleOrNull{it.account==s.getLong("sourceAccountId")&&it.device==s.getString("sourceDevice")&&it.participation==s.getString("sourceGeneration")}
            val local=current.members.singleOrNull{it.account==account&&it.device==device&&it.participation==s.getString("targetGeneration")}
            if(source!=null&&local!=null&&source.device!=local.device&&s.getLong("targetAccountId")==account&&s.getString("targetDevice")==device)output+=s
        }
        return output
    }
    fun send(group:String,room:String,account:Long,device:String,targetKey:String,route:String,generation:Long,kind:String,data:String,mid:String,send:(JSONObject)->Boolean):String? {
        prune();if(requests.size>=64)return null
        val state=states[group]?:return null;val current=state.rooms.singleOrNull{it.id==room}?:return null
        val local=current.members.singleOrNull{it.account==account&&it.device==device}?:return null
        val target=current.members.singleOrNull{it.key==targetKey&&it.device!=device}?:return null
        val value=JSONObject().put("type","group_media").put("requestId",uuid()).put("groupId",group).put("roomId",room)
            .put("epoch",state.epoch).put("revision",state.revision).put("rosterGeneration",state.roster)
            .put("sourceGeneration",local.participation).put("targetGeneration",target.participation)
            .put("targetAccountId",target.account).put("targetDevice",target.device)
            .put("routeId",route).put("mediaGeneration",generation).put("kind",kind).put("data",data).put("mid",mid)
        validatePayload(value)
        if(!send(value))return null
        return value.getString("requestId").also{requests[it]=Request(group,"media",now()+15_000)}
    }
    private fun prune(){
        val at=now();requests.entries.removeAll{entry->if(at>=entry.value.expires){
            if(results.size>=128)results.remove(results.keys.first());results[entry.key]="timeout";true
        }else false}
        signals.removeAll{at>=it.expires}
    }
    companion object {
        private fun uuid()=UUID.randomUUID().toString().replace("-","")
        private fun id(value:String,size:Int)=value.length==size&&value.all{it in '0'..'9'||it in 'a'..'f'}
        fun parse(value:JSONObject):GroupVoiceRoomsSnapshot {
            val group=value.getString("groupId");val epoch=value.getString("epoch");val revision=value.getLong("revision");val roster=value.getLong("rosterGeneration")
            require(id(group,32)&&id(epoch,32)&&revision>=0&&roster>0)
            val input=value.getJSONArray("rooms");require(input.length()<=16)
            val seen=mutableSetOf<String>();val rooms=mutableListOf<GroupVoiceRoom>()
            repeat(input.length()){i->
                val r=input.getJSONObject(i);val room=r.getString("roomId");val kind=r.getString("kind");val name=r.getString("name")
                require(id(room,32)&&seen.add(room)&&kind in setOf("voice","screen_share")&&name.length<=64)
                val members=r.getJSONArray("members");require(members.length()<=64)
                val devices=mutableSetOf<String>()
                val list=List(members.length()){n->val m=members.getJSONObject(n)
                    GroupVoiceMember(m.getLong("accountId"),m.getString("publicIdentity"),m.getString("mediaGeneration"),m.getString("username")).also{
                        require(it.account>0&&id(it.device,64)&&id(it.participation,32)&&it.name.length<=64&&devices.add(it.device))
                    }
                }
                if(kind=="voice")rooms+=GroupVoiceRoom(room,name,list)
            }
            return GroupVoiceRoomsSnapshot(group,epoch,revision,roster,rooms)
        }
        private fun validateSignal(s:JSONObject){
            require(id(s.getString("groupId"),32)&&id(s.getString("roomId"),32)&&id(s.getString("epoch"),32)&&s.getLong("revision")>=0&&s.getLong("rosterGeneration")>0)
            require(s.getLong("sourceAccountId")>0&&s.getLong("targetAccountId")>0&&id(s.getString("sourceDevice"),64)&&id(s.getString("targetDevice"),64))
            require(id(s.getString("sourceGeneration"),32)&&id(s.getString("targetGeneration"),32));validatePayload(s)
        }
        private fun validatePayload(s:JSONObject){
            val kind=s.getString("kind");val data=s.getString("data")
            require(id(s.getString("routeId"),32)&&s.getLong("mediaGeneration")>0&&s.getString("mid").length<=32&&kind in setOf("offer","answer","candidate","close"))
            require(data.toByteArray(Charsets.UTF_8).size<=if(kind=="candidate")4096 else 32768)
            require(if(kind=="close")data.isEmpty() else data.isNotBlank())
        }
    }
}
