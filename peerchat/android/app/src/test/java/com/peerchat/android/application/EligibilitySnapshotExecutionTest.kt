package com.peerchat.android.application

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.ByteArrayInputStream
import java.io.DataInputStream

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class EligibilitySnapshotExecutionTest {
    private val device="a".repeat(64)
    private val target="b".repeat(64)
    private fun reply()=JSONObject().put("state","ready").put("snapshotVersion",1)
        .put("senderAccountId",10).put("senderDevice",device).put("recipientAccountId",20)
        .put("snapshotId","c".repeat(32)).put("snapshotSequence",7)
        .put("issuedUtcMs",1000).put("freshUntilUtcMs",121000)
        .put("candidates",JSONArray().put(JSONObject().put("device",target).put("online",true).put("binding","")))
    private fun rejected(value:JSONObject) {
        try {EligibilitySnapshotExecution.encode(value,10,3,device,20);fail("Untrusted snapshot was accepted")}
        catch(_:IllegalArgumentException){}
    }
    @Test fun exactBoundedWireIncludesAuthenticatedScopeAndGeneration() {
        val wire=EligibilitySnapshotExecution.encode(reply(),10,3,device,20)
        val prefix="PeerChat/mailbox-eligibility-snapshot/v1\u0000".toByteArray(Charsets.US_ASCII)
        assertArrayEquals(prefix,wire.copyOfRange(0,prefix.size))
        val input=DataInputStream(ByteArrayInputStream(wire,prefix.size,wire.size-prefix.size))
        fun text():String {val size=input.readInt();return ByteArray(size).also{input.readFully(it)}.toString(Charsets.UTF_8)}
        assertEquals(10L,input.readLong());assertEquals(3L,input.readLong());assertEquals(device,text())
        assertEquals(20L,input.readLong());assertEquals(1,input.readInt());assertEquals("c".repeat(32),text())
        assertEquals(7L,input.readLong());assertEquals(1000L,input.readLong());assertEquals(121000L,input.readLong())
        assertEquals(1,input.readUnsignedByte());assertEquals(target,text());assertEquals(1,input.readUnsignedByte())
        assertEquals(0,input.readInt());assertEquals(-1,input.read())
    }
    @Test fun rejectsWrongAccountDeviceRecipientAndOverlongFreshness() {
        rejected(reply().put("senderAccountId",99));rejected(reply().put("senderDevice",target))
        rejected(reply().put("recipientAccountId",99));rejected(reply().put("freshUntilUtcMs",121001))
        rejected(reply().put("snapshotVersion",2));rejected(reply().put("senderAccountId","10"))
    }
    @Test fun rejectsDuplicateTargetsAndMissingEligibility() {
        val value=reply();val rows=value.getJSONArray("candidates");rows.put(rows.getJSONObject(0));rejected(value)
        val offline=reply();offline.getJSONArray("candidates").getJSONObject(0).put("online",false);rejected(offline)
    }
    @Test fun rejectsOverflowWithoutTruncatingTargets() {
        val value=reply();val rows=value.getJSONArray("candidates")
        repeat(32){rows.put(JSONObject().put("device","%064x".format(it+1)).put("online",true).put("binding",""))}
        rejected(value)
    }
}
