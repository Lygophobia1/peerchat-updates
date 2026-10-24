package com.peerchat.android.ui

import com.peerchat.android.application.*
import java.util.Locale
import java.util.TimeZone

/** Presentation reuse only. Never suppresses native group discovery/admission. */
internal class GroupUiProjectionCache {
    private data class Scope(val account:Long,val identity:String,val authenticated:Boolean,val generation:Long)
    private data class Names(val username:String,val friends:List<Pair<Long,String>>,val locale:String,val zone:String)
    private data class Voice(val joined:Boolean,val room:String,val epoch:String,val muted:Boolean,val deafened:Boolean)
    private data class Key(val group:GroupConversationSnapshot,val names:Names,val rooms:GroupVoiceRoomsSnapshot?,val voice:Voice)
    private var scope:Scope?=null
    private var names=Names("",emptyList(),"","")
    private val entries=mutableMapOf<String,Pair<Key,ConversationUi>>()
    fun prepare(snapshot:MessagingSnapshot,generation:Long) {
        val next=Scope(snapshot.accountId,snapshot.localPeerId,snapshot.authenticated,generation)
        if(next!=scope){entries.clear();scope=next}
        names=Names(snapshot.username,snapshot.conversations.map{it.friend.accountId to it.friend.username},Locale.getDefault().toLanguageTag(),TimeZone.getDefault().id)
        entries.keys.retainAll(snapshot.groups.map{it.id}.toSet())
    }
    fun project(group:GroupConversationSnapshot,rooms:GroupVoiceRoomsSnapshot?,voice:VoiceSnapshot,build:()->ConversationUi):ConversationUi {
        val joined=voice.active&&voice.group==group.id
        val state=if(joined)Voice(true,voice.room,voice.epoch,voice.muted,voice.deafened) else Voice(false,"","",false,false)
        val key=Key(group,names,rooms,state)
        entries[group.id]?.takeIf{it.first==key}?.let{return it.second}
        return build().also{entries[group.id]=key to it}
    }
}
