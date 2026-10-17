package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class DmLocalAttachmentPageTest {
    private fun page(cursor:String="",more:Long=0,count:Int=0):ByteArray {
        val rows=NativeRecordWriter(30).also{w->w.number(count.toLong());repeat(count){n->
            w.text(n.toString(16).padStart(32,'0'));w.text("b".repeat(64));w.text("日本-📷.bin");w.number(20);w.number(4);w.number(0);w.number(20);w.number(1);w.text("");w.text("/private/verified-$n")
        }}.bytes()
        return NativeRecordWriter(30).also{it.text("PeerChat/DMLocalAttachmentPage/v1");it.text(cursor);it.number(more);it.blob(rows)}.bytes()
    }
    @Test fun preservesBoundedVerifiedCopiesAndOpaqueNextCursor(){
        val cursor="a".repeat(64)+":16";val parsed=dmLocalAttachmentPage(page(cursor,1,16))
        assertEquals(cursor,parsed.cursor);assertTrue(parsed.more);assertEquals(16,parsed.rows.size)
        assertEquals("日本-📷.bin",parsed.rows.first().name);assertEquals("/private/verified-0",parsed.rows.first().localPath)
    }
    @Test fun terminalEmptyPageIsValid(){val parsed=dmLocalAttachmentPage(page());assertFalse(parsed.more);assertTrue(parsed.rows.isEmpty())}
    @Test fun malformedOrUnboundedPagesAreRejected(){
        for(bytes in listOf(page("a".repeat(64)+":1",0),page("../wrong",1),page(count=17),page()+byteArrayOf(1))){
            try{dmLocalAttachmentPage(bytes);fail("Invalid native page accepted")}catch(_:IllegalArgumentException){}
        }
    }
}
