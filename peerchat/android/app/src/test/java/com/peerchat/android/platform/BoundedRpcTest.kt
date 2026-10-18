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
}
