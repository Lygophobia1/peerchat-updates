package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class GroupRelayScopeTest {
    @Test fun serverPurposeAndNativeScopeStayBound() {
        val group="d".repeat(32)
        val target="a".repeat(64)
        val scope=GroupRelayScope(7,3,group,5,8,target)
        val fields=scope.fields()
        assertEquals("group_key",fields.getString("purpose"))
        assertEquals(group,fields.getString("groupId"))
        assertEquals("5",fields.getString("rosterGeneration"))
        assertEquals(3,fields.length())
        val binding=NativeRecordWriter(27).apply {text("PeerChat/AndroidGroupRelay/v1");number(7);number(3);text(group);number(5);number(8);text(target)}.bytes()
        val request=NativeRecordWriter(1).apply {number(1);number(77);text(target);number(5);blob(binding)}.bytes()
        assertEquals(scope,relayExecutions(request).single().group)
        val wrongTarget=NativeRecordWriter(1).apply {number(1);number(77);text("b".repeat(64));number(5);blob(binding)}.bytes()
        assertThrows(IllegalArgumentException::class.java){relayExecutions(wrongTarget)}
    }
}
