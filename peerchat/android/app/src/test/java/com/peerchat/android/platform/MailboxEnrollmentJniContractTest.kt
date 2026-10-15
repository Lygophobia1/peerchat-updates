package com.peerchat.android.platform

import com.peerchat.android.MailboxBridge
import org.junit.Assert.assertEquals
import org.junit.Test

class MailboxEnrollmentJniContractTest {
    @Test fun scopedEnrollmentMethodsMatchNativeDescriptors(){
        val type=MailboxBridge::class.java;val long=java.lang.Long.TYPE;val bytes=ByteArray::class.java
        assertEquals(java.lang.Integer.TYPE,type.getMethod("configureEnrollment",long,long,long,bytes,bytes).returnType)
        assertEquals(bytes,type.getMethod("reenrollmentOffer",long).returnType)
        assertEquals(java.lang.Integer.TYPE,type.getMethod("reenrollmentDecision",long,bytes).returnType)
        assertEquals(bytes,type.getMethod("reenrollmentError",long).returnType)
        assertEquals(java.lang.Integer.TYPE,type.getMethod("hasScopedEnrollment",long).returnType)
    }
}
