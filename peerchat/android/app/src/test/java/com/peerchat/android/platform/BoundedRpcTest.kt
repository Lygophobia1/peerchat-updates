package com.peerchat.android.platform
import com.peerchat.android.platform.tls.BoundedRpc
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
class BoundedRpcTest {
    class Resource:AutoCloseable {var closed=false;override fun close(){closed=true}}
    @Test fun reuseBoundsFailureAndIsolation(){
        var now=0L;var opens=0;val all=mutableListOf<Resource>()
        val owner=BoundedRpc({Resource().also{opens++;all+=it}},{!it.closed},{now})
        repeat(10){owner.run("profile-one"){assertFalse(it.closed)}}
        assertEquals(1,opens)
        now+=10_000_000_001;owner.run("profile-one"){assertFalse(it.closed)}
        assertEquals(2,opens);assertTrue(all[0].closed)
        owner.run("profile-two"){};assertEquals(3,opens);assertTrue(all[1].closed)
        try{owner.run<Unit>("profile-two"){throw java.io.IOException("partial")};fail()}catch(_:java.io.IOException){}
        assertTrue(all[2].closed);owner.run("profile-two"){};assertEquals(4,opens)
        all.last().close();owner.run("profile-two"){};assertEquals(5,opens)
        owner.close();assertTrue(all.all{it.closed})
    }
    @Test fun serializedAndCountBound(){
        var opens=0;var inside=0
        val owner=BoundedRpc({opens++;Resource()},{!it.closed})
        val threads=List(4){Thread{repeat(40){owner.run("same"){assertEquals(0,inside);inside++;inside--}}}}
        threads.forEach{it.start()};threads.forEach{it.join()}
        assertEquals(3,opens);owner.close()
    }
    @Test fun windowCountsEveryFrameAndRetiresBeforeServerLimit(){
        val resources=mutableListOf<Resource>()
        val owner=BoundedRpc({Resource().also{resources.add(it)}},{!it.closed})
        repeat(56){owner.run("same") {}}
        owner.run("same",8){assertSame(resources.last(),it)}
        assertEquals(2,resources.size);assertTrue(resources[0].closed)
        repeat(6){owner.run("same",8){}}
        assertEquals(2,resources.size)
        owner.run("same",8){};assertEquals(3,resources.size)
        for(count in listOf(0,9)){try{owner.run("same",count){};fail()}catch(_:IllegalArgumentException){}}
        try{owner.run<Unit>("same",8){throw IOException("partial window")};fail()}catch(_:IOException){}
        assertTrue(resources.last().closed);owner.close()
    }
    @Test fun deliveryLaneRunsWhileOrdinaryLaneIsBlockedAndBothRetire(){
        val ordinaryResources=mutableListOf<Resource>()
        val deliveryResources=mutableListOf<Resource>()
        val owners=RpcChannelPair({Resource().also{ordinaryResources+=it}},
            {Resource().also{deliveryResources+=it}},{!it.closed})
        for(type in listOf("mailbox_register","mailbox_revoke","mailbox_keys","mailbox_store",
            "mailbox_fetch","mailbox_ack","mailbox_receipts"))assertTrue(type,deliveryRpcOperation(type))
        for(type in listOf("mailbox_eligibility_snapshot","mailbox_eligibility_validate","cache_dm_read",
            "archive_authorization_state"))assertFalse(type,deliveryRpcOperation(type))
        val entered=CountDownLatch(1)
        val release=CountDownLatch(1)
        val ordinaryFinished=CountDownLatch(1)
        val threadFailure=AtomicReference<Throwable?>()
        val blocked=Thread {
            try {owners.run("mailbox_eligibility_snapshot","session-one") {entered.countDown();assertTrue(release.await(5,TimeUnit.SECONDS))}}
            catch(error:Throwable){threadFailure.set(error)}
            finally {ordinaryFinished.countDown()}
        }
        blocked.start()
        try {
            assertTrue(entered.await(2,TimeUnit.SECONDS))
            val deliveryFinished=CountDownLatch(1)
            val delivery=Thread {try {owners.run("mailbox_ack","session-one") {deliveryFinished.countDown()}}catch(error:Throwable){threadFailure.set(error)}}
            delivery.start()
            assertTrue("Delivery RPC waited behind ordinary RPC",deliveryFinished.await(2,TimeUnit.SECONDS))
            delivery.join(2000)
        } finally {release.countDown();blocked.join(2000)}
        assertEquals(0L,ordinaryFinished.count)
        assertNull(threadFailure.get())
        val ordinary=ordinaryResources.single()
        val firstDelivery=deliveryResources.single()
        assertNotSame(ordinary,firstDelivery)
        owners.closeSelected("mailbox_ack")
        assertTrue(firstDelivery.closed)
        assertFalse(ordinary.closed)
        owners.run("mailbox_store","session-one") {}
        val secondDelivery=deliveryResources.last()
        owners.run("mailbox_fetch","session-two") {}
        assertTrue(secondDelivery.closed)
        assertFalse(ordinary.closed)
        owners.run("mailbox_eligibility_validate","session-two") {}
        assertTrue(ordinary.closed)
        try {owners.run<Unit>("mailbox_receipts","session-two") {throw IOException("partial response")};fail()} catch(_:IOException) {}
        assertTrue(deliveryResources.last().closed)
        owners.run("mailbox_keys","session-two") {}
        owners.close()
        assertTrue(ordinaryResources.all{it.closed}&&deliveryResources.all{it.closed})
    }
    @Test fun cachePayloadCannotBlockMailboxOrMetadataAndRetiresIndependently() {
        val ordinary=mutableListOf<Resource>();val delivery=mutableListOf<Resource>();val cache=mutableListOf<Resource>()
        val owners=RpcChannelPair({Resource().also{ordinary+=it}},{Resource().also{delivery+=it}},
            {!it.closed},{Resource().also{cache+=it}})
        val entered=CountDownLatch(1);val release=CountDownLatch(1);val failure=AtomicReference<Throwable?>()
        val blocked=Thread {try{owners.runWindow("cache_dm_append","one",8){entered.countDown();assertTrue(release.await(5,TimeUnit.SECONDS))}}
            catch(t:Throwable){failure.set(t)}}
        blocked.start()
        try {
            assertTrue(entered.await(2,TimeUnit.SECONDS))
            val done=CountDownLatch(2)
            val workers=listOf("mailbox_ack","device_identity_snapshot").map{type->Thread{
                try{owners.run(type,"one"){done.countDown()}}catch(t:Throwable){failure.set(t)}
            }.also{it.start()}}
            assertTrue(done.await(2,TimeUnit.SECONDS));workers.forEach{it.join(2000)}
        } finally {release.countDown();blocked.join(2000)}
        assertNull(failure.get());assertEquals(1,ordinary.size);assertEquals(1,delivery.size);assertEquals(1,cache.size)
        owners.run("cache_dm_complete","one"){assertSame(cache.first(),it)}
        owners.run("cache_acquire","two"){}
        assertTrue(cache.first().closed);assertFalse(ordinary.first().closed);assertFalse(delivery.first().closed)
        try{owners.run<Unit>("cache_dm_append","two"){throw IOException("ambiguous")};fail()}catch(_:IOException){}
        assertTrue(cache.last().closed)
        owners.run("cache_dm_status","two"){}
        owners.close();assertTrue((ordinary+delivery+cache).all{it.closed})
    }
    @Test fun cacheOwnerUsesServerFrame64ThenRetiresOnMoreFalse() {
        val ordinary=mutableListOf<Resource>();val delivery=mutableListOf<Resource>();val cache=mutableListOf<Resource>()
        val owners=RpcChannelPair({Resource().also{ordinary+=it}},{Resource().also{delivery+=it}},
            {!it.closed},{Resource().also{cache+=it}})
        try {
            repeat(55){owners.run("cache_dm_receipts","session-one") {assertSame(cache.first(),it)}}
            owners.runWindow("cache_dm_append","session-one",8){assertSame(cache.first(),it)}
            assertEquals(1,cache.size)
            // The server returns more=false on frame 64; close inside the
            // operation exactly as ServerAccounts does before another request.
            owners.run("cache_dm_complete","session-one"){
                assertSame(cache.first(),it)
                owners.closeSelected("cache_dm_complete")
            }
            assertTrue(cache.first().closed)
            owners.run("cache_dm_status","session-one") {assertSame(cache.last(),it)}
            assertEquals(2,cache.size)
            owners.run("cache_dm_status","session-two") {assertSame(cache.last(),it)}
            assertEquals(3,cache.size);assertTrue(cache[1].closed)
            try {owners.runWindow<Unit>("cache_dm_append","session-two",8){throw IOException("partial window")};fail()}
            catch(_:IOException) {}
            assertTrue(cache.last().closed)
            owners.run("cache_dm_status","session-two") {assertSame(cache.last(),it)}
            assertEquals(4,cache.size)
            // Both other owners retain their original 60-frame budget.
            repeat(60){owners.run("device_identity_snapshot","session-one"){};owners.run("mailbox_ack","session-one") {}}
            assertEquals(1,ordinary.size);assertEquals(1,delivery.size)
            owners.run("device_identity_snapshot","session-one"){};owners.run("mailbox_ack","session-one") {}
            assertEquals(2,ordinary.size);assertEquals(2,delivery.size)
            assertTrue(ordinary.first().closed&&delivery.first().closed)
        } finally {owners.close()}
    }
    @Test fun cacheBudgetNeverExceedsServer64FrameBound() {
        val resources=mutableListOf<Resource>()
        val owner=BoundedRpc({Resource().also{resources+=it}},{!it.closed},64)
        repeat(57){owner.run("one") {assertSame(resources.first(),it)}}
        owner.run("one",8){assertSame(resources.last(),it)}
        assertEquals(2,resources.size);assertTrue(resources.first().closed)
        repeat(7){owner.run("one",8){}}
        assertEquals(2,resources.size) // New connection has used exactly 64 frames.
        owner.run("one"){};assertEquals(3,resources.size);assertTrue(resources[1].closed)
        owner.close()
        try {BoundedRpc({Resource()},{!it.closed},65);fail("bound above server 64 accepted")}
        catch(_:IllegalArgumentException) {}
    }
}
