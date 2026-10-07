package com.peerchat.android.platform
import com.peerchat.android.platform.tls.BoundedRpc
import org.junit.Assert.*
import org.junit.Test
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
}
