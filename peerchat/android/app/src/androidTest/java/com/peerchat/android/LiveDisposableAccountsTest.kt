package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.AttachmentSendMode
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.net.InetSocketAddress
import java.net.Socket

/** Opt-in REAL server/application test. Secrets arrive in memory over an ADB
 * loopback tunnel. Never run against the production package or personal profiles.
 * No fixture trust injection, server reset, credential logging or account deletion.
 * Two Android application owners are NOT Windows interoperability/media proof. */
@RunWith(AndroidJUnit4::class)
class LiveDisposableAccountsTest {
    @Test fun authenticatedOwnersMessagingAndRoomReadiness() = runBlocking {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        val port=InstrumentationRegistry.getArguments().getString("disposableProbePort")?.toIntOrNull()
        assumeTrue("Explicit isolated live-account probe required",port!=null)
        check(context.packageName=="com.peerchat.android.voiceintegration")
        check(android.os.Build.FINGERPRINT.contains("generic") || android.os.Build.MODEL.contains("sdk"))
        val input=Socket().use {socket->
            socket.connect(InetSocketAddress("127.0.0.1",port!!),5000);socket.soTimeout=5000
            val stream=DataInputStream(socket.getInputStream());val count=stream.readInt()
            require(count in 1..4096)
            JSONObject(ByteArray(count).also{stream.readFully(it)}.toString(Charsets.UTF_8))
        }
        val endpoint=ServerEndpoint(input.getString("host"),input.getInt("port"),input.getString("pin"))
        require(endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))
        require(input.getString("first")=="codex"&&input.getString("second")=="codex1")
        val first=MessagingBackend(context,"test-live-codex")
        val second=MessagingBackend(context,"test-live-codex1")
        var stage="login_first"
        fun evidence(value:String) {
            println(value)
            instrumentation.sendStatus(2,android.os.Bundle().apply{putString("stream",value+"\n")})
        }
        suspend fun await(label:String,timeout:Long=60000,predicate:()->Boolean) {
            stage=label;val start=System.nanoTime()
            while(!predicate()) {
                if((System.nanoTime()-start)/1_000_000>timeout)throw AssertionError("Bounded stage timeout: $label")
                delay(100)
            }
            evidence("LIVE_STAGE $label elapsed_ms=${(System.nanoTime()-start)/1_000_000}")
        }
        fun peer(owner:MessagingBackend,account:Long)=owner.state.value.conversations.single{it.friend.accountId==account}
        fun retained(label:String,owner:MessagingBackend) {
            val state=owner.state.value
            evidence("LIVE_OWNER $label authenticated=${state.authenticated} presence=${state.serverPresenceLive} rooms=${state.voiceRooms.size}")
            state.conversations.filter { it.friend.username in setOf("codex","codex1") }.forEach { evidence("LIVE_FRIEND $label account=${it.friend.accountId} selected=${it.friend.peerId} known=${it.friend.knownPeerIds.joinToString(",")} ready=${it.connection.ready}") }
            state.conversations.filter{it.friend.username in setOf("codex","codex1")}.flatMap{it.messages}.filter{it.text.startsWith("PC-LIVE-DM-v1:")}.forEach{
                evidence("LIVE_RETAINED_DM $label id=${it.id} sender=${it.senderDevice} recipient=${it.recipientDevice} unresolved=${it.deliveryTargets?.selectionUnresolved} targets=${it.deliveryTargets?.targets?.joinToString(","){t->t.deviceId+":"+t.status.name}}")
            }
            evidence("LIVE_VERIFICATION $label pending_count=${state.deviceVerifications.size}")
            state.conversations.flatMap{it.attachments}.forEach{
                val error=when(it.error){
                    ""->"none"
                    "Attachment response failed validation or protected storage"->"native_reply_validation_or_storage"
                    "Cache connection unavailable; durable attachment data retained"->"cache_connection_unavailable"
                    "Cache request timed out; durable attachment data retained"->"cache_request_timeout"
                    else->it.error.removePrefix("Attachment response failed validation or protected storage: ").takeIf{value->value.matches(Regex("[a-z_]{1,100}"))}?:"non_code_redacted"
                }
                evidence("LIVE_TRANSFER $label id=${it.id} state=${it.state} done=${it.done} size=${it.size} cached=${it.cachePhase.filter{c->c.isLetterOrDigit()||c=='_'}.take(80)} error=$error")
            }
            state.transferDiagnostics.lineSequence().filter{it.startsWith("native_transfer_request_failure")}.forEach{line->
                evidence("LIVE_NATIVE $label "+Regex("(?:operation|stage|reason|input_bytes)=[a-z_0-9-]+").findAll(line).take(4).joinToString(" "){it.value})
            }
        }
        try {
            if(input.optBoolean("inspect")) {
                input.remove("firstPassword");input.remove("secondPassword")
                check(first.restoreRemembered(endpoint)&&second.restoreRemembered(endpoint))
                await("inspect_presence"){first.state.value.serverPresenceLive&&second.state.value.serverPresenceLive}
                await("inspect_own_inventory"){second.state.value.ownDevices!=null}
                // Read the authenticated own-account inventory, not another
                // account's registration or any secrets. No network under gate.
                val accountsField=MessagingBackend::class.java.getDeclaredField("accounts").apply{isAccessible=true}
                val owner=accountsField.get(second) as com.peerchat.android.platform.ServerAccounts
                val active=owner.session
                val inventory=owner.ownDevices()
                check(owner.session===active&&inventory.getLong("accountId")==second.state.value.accountId)
                val rows=inventory.getJSONArray("devices")
                for(i in 0 until rows.length()) {
                    val row=rows.getJSONObject(i);val identity=row.getString("publicIdentity")
                    require(identity.matches(Regex("[0-9a-f]{64}")))
                    fun safe(key:String)=row.optString(key,"unavailable").take(128).replace(Regex("[^A-Za-z0-9_. -]"),"_")
                    evidence("LIVE_OWN_DEVICE second identity=$identity state=${safe("state")} current=${row.getBoolean("currentDevice")} online=${row.getBoolean("online")} created_utc=${row.getLong("createdUtc")} platform=${safe("platform")} version=${safe("version")} cache=${safe("cacheCapability")}")
                }
                // Presence of a protected contact record only; never decrypt,
                // export, or alter identity/key material for this inspection.
                val gate=MessagingBackend::class.java.getDeclaredField("gate").apply{isAccessible=true}.get(first)
                synchronized(gate) {
                    val profile=MessagingBackend::class.java.getDeclaredField("profile").apply{isAccessible=true}.get(first)
                    val platform=NativeProfile::class.java.getDeclaredField("platform").apply{isAccessible=true}.get(profile)
                    val db=platform.javaClass.getDeclaredField("database").apply{isAccessible=true}.get(platform) as android.database.sqlite.SQLiteDatabase
                    for(i in 0 until rows.length()) {
                        val identity=rows.getJSONObject(i).getString("publicIdentity")
                        db.rawQuery("SELECT count(*) FROM objects WHERE k=?",arrayOf("contact/$identity")).use { cursor->
                            check(cursor.moveToFirst())
                            evidence("LIVE_CONTACT_RECORD first device=$identity exists=${cursor.getLong(0)>0}")
                        }
                    }
                }
                delay(5000)
                retained("first",first);retained("second",second)
                return@runBlocking
            }
            val start=System.nanoTime()
            val a=first.signInRemembered(endpoint,input.getString("first"),input.getString("firstPassword"),true)
            input.remove("firstPassword")
            evidence("LIVE_STAGE login_first elapsed_ms=${(System.nanoTime()-start)/1_000_000}")
            stage="login_second"
            val b=second.signInRemembered(endpoint,input.getString("second"),input.getString("secondPassword"),true)
            input.remove("secondPassword")
            await("authenticated_presence"){first.state.value.serverPresenceLive&&second.state.value.serverPresenceLive}
            if(first.state.value.conversations.none{it.friend.accountId==b.accountId}) {
                stage="friend_request"
                if(second.state.value.incomingRequests.none{it.accountId==a.accountId}&&first.state.value.outgoingRequests.none{it.accountId==b.accountId})first.requestFriend("codex1")
                await("friend_request_received"){second.state.value.incomingRequests.any{it.accountId==a.accountId}}
                second.respondFriend(a.accountId,true)
            }
            await("authenticated_friendship"){first.state.value.conversations.any{it.friend.accountId==b.accountId}&&second.state.value.conversations.any{it.friend.accountId==a.accountId}}
            val marker="integration-"+java.util.UUID.randomUUID().toString()
            stage="send_first"
            val id=first.send(peer(first,b.accountId).friend.peerId,marker)
            await("first_durable_receive"){peer(second,a.accountId).messages.any{it.id==id}}
            await("first_device_ack"){peer(first,b.accountId).messages.any{it.id==id&&it.deliveryTargets?.targets?.any{t->t.deviceId==second.state.value.localPeerId&&t.status.name=="Acknowledged"}==true}}
            stage="send_second"
            val reply=second.send(peer(second,a.accountId).friend.peerId,marker+"-reply")
            await("second_durable_receive"){peer(first,b.accountId).messages.any{it.id==reply}}
            await("second_device_ack"){peer(second,a.accountId).messages.any{it.id==reply&&it.deliveryTargets?.targets?.any{t->t.deviceId==first.state.value.localPeerId&&t.status.name=="Acknowledged"}==true}}
            suspend fun attachment(sender:MessagingBackend,recipient:MessagingBackend,target:Long,sourceAccount:Long,mode:AttachmentSendMode) {
                val label="attachment_${mode.name}_${if(sender===first)"forward" else "reverse"}"
                stage=label+"_offer"
                val directory=java.io.File(context.cacheDir,"diagnostics").apply{mkdirs()}
                val file=java.io.File(directory,"integration-${java.util.UUID.randomUUID()}.txt")
                val content=(marker+"-payload-").repeat(32)
                file.writeText(content)
                val uri=androidx.core.content.FileProvider.getUriForFile(context,"${context.packageName}.diagnostics",file)
                val transfer=sender.sendAttachment(peer(sender,target).friend.peerId,uri,mode)
                await(label+"_offer_received",90000){peer(recipient,sourceAccount).attachments.any{it.id==transfer}}
                val offered=peer(recipient,sourceAccount).attachments.single{it.id==transfer}
                if(offered.awaitingAcceptance)recipient.attachmentAction(transfer,true)
                await(label+"_verified",90000){peer(recipient,sourceAccount).attachments.any{it.id==transfer&&it.complete}}
                assertEquals(content,recipient.attachmentText(transfer))
                evidence("LIVE_PAYLOAD $label verified_equal=true")
            }
            for(mode in listOf(AttachmentSendMode.Direct,AttachmentSendMode.Cached)) {
                attachment(first,second,b.accountId,a.accountId,mode)
                attachment(second,first,a.accountId,b.accountId,mode)
            }
            stage="group_create"
            val groupName="Integration "+java.util.UUID.randomUUID().toString().take(8)
            first.createGroup(groupName)
            await("group_created"){first.state.value.groups.any{it.name==groupName&&it.ready}}
            val group=first.state.value.groups.single{it.name==groupName}.id
            first.inviteGroup(group,b.accountId)
            await("group_invitation"){second.state.value.groupInvitations.any{it.group==group}}
            second.respondGroup(second.state.value.groupInvitations.single{it.group==group}.id,true)
            await("group_member_ready"){second.state.value.groups.any{it.id==group&&it.ready}}
            // New groups legitimately have no rooms until an authorized client
            // creates one. Empty authenticated snapshots are readiness evidence.
            await("both_room_snapshots"){first.state.value.voiceRooms.any{it.group==group}&&second.state.value.voiceRooms.any{it.group==group}}
            stage="group_text_send"
            val groupMessage=first.sendGroup(group,marker+"-group")
            await("group_text_received"){second.state.value.groups.any{g->g.id==group&&g.messages.any{it.id==groupMessage}}}
            stage="background_grace"
            first.setInUse(false,retainConnectionsUntilNanos=System.nanoTime()+60_000_000_000)
            delay(1000)
            assertTrue(first.state.value.authenticated)
            first.setInUse(true)
            stage="network_quiescence"
            first.setInUse(false)
            assertTrue(first.awaitQuiescence(30000))
            assertFalse(first.state.value.authenticated)
            first.setInUse(true)
            assertTrue(first.restoreRemembered(endpoint))
            await("reconnected_presence"){first.state.value.serverPresenceLive}
            await("durable_history_after_reconnect"){peer(first,b.accountId).messages.any{it.id==reply}}
            evidence("LIVE_RESULT authenticated bidirectional device ACK, group text, room snapshot and reconnect passed; media not tested")
        } catch(failure:Throwable) {
            retained("first",first);retained("second",second)
            // Do not propagate provider messages/causes containing response data.
            throw AssertionError("Live integration failed stage=$stage type=${failure.javaClass.simpleName}")
        } finally {input.remove("firstPassword");input.remove("secondPassword");first.close();second.close()}
    }
}
