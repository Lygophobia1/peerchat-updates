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
    @Test fun smallClockSkewRetriesOriginalSnapshotOnlyAfterIssuance() {
        var utc=1000L;var calls=0;var fetched=0;var installed:ByteArray?=null
        val execution=EligibilitySnapshotExecution({utc}){_,wire->calls++;installed=wire;0}
        val response=reply().put("issuedUtcMs",1410).put("freshUntilUtcMs",121410)
        val expected=EligibilitySnapshotExecution.encode(response,10,3,device,20)
        fun step(now:Long)=execution.refreshWith(1,10,3,device,listOf(20),now){fetched++;response}
        assertFalse(step(100));assertEquals(0,calls);assertEquals(1,fetched)
        assertTrue(execution.diagnostic.contains("clock_ahead_ms=410"))
        utc=1409;assertFalse(step(200));assertEquals(0,calls)
        utc=1410;assertTrue(step(300));assertEquals(1,calls);assertEquals(1,fetched)
        assertArrayEquals(expected,installed)
        assertFalse(step(400));assertEquals(1,calls)
    }
    @Test fun deferredSnapshotStillRequiresNativeAuthorizationAndNeverExtendsExpiry() {
        var utc=1000L;var calls=0
        val execution=EligibilitySnapshotExecution({utc}){_,_->calls++;1}
        val response=reply().put("issuedUtcMs",1410).put("freshUntilUtcMs",2410)
        assertFalse(execution.refreshWith(1,10,3,device,listOf(20),100){response})
        utc=1410
        assertFalse(execution.refreshWith(1,10,3,device,listOf(20),200){fail("Unexpected fetch");response})
        assertEquals(1,calls);assertTrue(execution.diagnostic.contains("binding_rejected"))
        val expired=EligibilitySnapshotExecution({utc}){_,_->calls++;0}
        utc=1000;expired.refreshWith(1,10,3,device,listOf(20),100){response}
        utc=2410
        assertFalse(expired.refreshWith(1,10,3,device,listOf(20),200){fail("Unexpected fetch");response})
        assertEquals(1,calls);assertTrue(expired.diagnostic.contains("reason=expired"))
    }
    @Test fun generationChangesRecipientRemovalAndResetDiscardDeferredEvidence() {
        var utc=1000L;var calls=0
        val execution=EligibilitySnapshotExecution({utc}){_,_->calls++;0}
        val response=reply().put("issuedUtcMs",1410).put("freshUntilUtcMs",121410)
        execution.refreshWith(1,10,3,device,listOf(20),100){response}
        utc=1410
        assertFalse(execution.refreshWith(1,10,4,device,emptyList(),200){fail("Unexpected fetch");response})
        assertEquals(0,calls)
        utc=1000;execution.refreshWith(1,10,4,device,listOf(20),60100){response}
        utc=1410
        assertFalse(execution.refreshWith(1,10,4,device,emptyList(),60101){fail("Unexpected fetch");response})
        assertEquals(0,calls)
        utc=1000;execution.refreshWith(1,10,4,device,listOf(20),121100){response}
        execution.reset();utc=1410
        assertFalse(execution.refreshWith(1,10,4,device,emptyList(),121200){response})
        assertEquals(0,calls)
    }
    @Test fun largeClockMismatchIsNotDeferredOrAcceptedByTheAdapter() {
        var calls=0
        val execution=EligibilitySnapshotExecution({1000}){_,_->calls++;1}
        assertFalse(execution.refreshWith(1,10,3,device,listOf(20),100){
            reply().put("issuedUtcMs",61001).put("freshUntilUtcMs",181001)
        })
        assertEquals(1,calls);assertTrue(execution.diagnostic.contains("binding_rejected"))
    }
    @Test fun physicalClockSkewDefersWithoutChangingAuthenticatedBytes() {
        var utc=1000L;var calls=0;var installed:ByteArray?=null
        val execution=EligibilitySnapshotExecution({utc}){_,wire->calls++;installed=wire;0}
        val response=reply().put("issuedUtcMs",38500).put("freshUntilUtcMs",158500)
        val expected=EligibilitySnapshotExecution.encode(response,10,3,device,20)
        assertFalse(execution.refreshWith(1,10,3,device,listOf(20),100){response})
        assertEquals(0,calls)
        utc=38499;assertFalse(execution.refreshWith(1,10,3,device,listOf(20),37599){error("Refetched pending evidence")})
        utc=38500;assertTrue(execution.refreshWith(1,10,3,device,listOf(20),37600){error("Refetched pending evidence")})
        assertEquals(1,calls);assertArrayEquals(expected,installed)
    }

    @Test fun reusedSnapshotRefreshesBeforeItsOriginalFreshnessEnds() {
        var utc=1000L;var fetched=0
        val execution=EligibilitySnapshotExecution({utc}){_,_->0}
        val response=reply()
        fun step(now:Long)=execution.refreshWith(1,10,3,device,listOf(20),now){fetched++;response}
        assertTrue(step(100));assertEquals(1,fetched)
        utc=61000;assertTrue(step(60100));assertEquals(2,fetched)
        utc=106899;assertFalse(step(105099));assertEquals(2,fetched)
        utc=107000;assertTrue(step(106100));assertEquals(3,fetched)
    }

    @Test fun nearExpiryReuseNeverSchedulesPastFreshness() {
        assertEquals(1000L,EligibilitySnapshotExecution.refreshDelay(11000,1000))
        assertEquals(1000L,EligibilitySnapshotExecution.refreshDelay(17000,1000))
        assertEquals(2000L,EligibilitySnapshotExecution.refreshDelay(18000,1000))
        assertEquals(60000L,EligibilitySnapshotExecution.refreshDelay(121000,1000))
    }

    @Test fun quotaBackoffStillVisitsLaterRecipients() {
        val fetched=mutableListOf<Long>()
        val execution=EligibilitySnapshotExecution({1000L}){_,_->fail("No quota response may reach native");1}
        val recipients=(20L..24L).toList()
        fun step(now:Long)=execution.refreshWith(1,10,3,device,recipients,now){recipient->
            fetched+=recipient;JSONObject().put("state","snapshot_quota_exceeded")
        }
        assertFalse(step(100));assertEquals(listOf(20L,21L,22L,23L),fetched)
        assertTrue(execution.diagnostic.contains("snapshot_quota_exceeded"))
        assertFalse(step(1100));assertEquals(recipients,fetched)
        assertFalse(step(15100));assertEquals(recipients,fetched)
        assertFalse(step(60100));assertEquals(recipients+recipients.take(4),fetched)
    }
}
