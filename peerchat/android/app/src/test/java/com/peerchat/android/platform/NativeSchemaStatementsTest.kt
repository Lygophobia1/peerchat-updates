package com.peerchat.android.platform

import android.database.sqlite.SQLiteDatabase
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class NativeSchemaStatementsTest {
    private val schema="CREATE TABLE IF NOT EXISTS messages(id TEXT UNIQUE NOT NULL);"+
        "CREATE TABLE IF NOT EXISTS message_archive_index(sequence INTEGER PRIMARY KEY AUTOINCREMENT,message_id TEXT UNIQUE NOT NULL);"+
        "CREATE TRIGGER IF NOT EXISTS message_archive_insert AFTER INSERT ON messages BEGIN INSERT INTO message_archive_index(message_id) VALUES(NEW.id); END;"+
        "CREATE TRIGGER IF NOT EXISTS message_archive_delete AFTER DELETE ON messages BEGIN DELETE FROM message_archive_index WHERE message_id=OLD.id; END;"
    @Test fun archiveTriggersExecuteIntactAndReopenIdempotently() {
        SQLiteDatabase.create(null).use{db->
            repeat(3){nativeSqlStatements(schema).forEach{db.execSQL(it)}}
            assertEquals(4,nativeSqlStatements(schema).size)
            db.execSQL("INSERT INTO messages(id) VALUES(?)",arrayOf("test-only-id"))
            db.rawQuery("SELECT message_id FROM message_archive_index",null).use{assertTrue(it.moveToFirst());assertEquals("test-only-id",it.getString(0));assertFalse(it.moveToNext())}
            db.execSQL("DELETE FROM messages WHERE id=?",arrayOf("test-only-id"))
            db.rawQuery("SELECT COUNT(*) FROM message_archive_index",null).use{assertTrue(it.moveToFirst());assertEquals(0,it.getInt(0))}
        }
    }
    @Test fun incompleteUnsupportedAndUserLiteralSqlFailsBeforeExecution() {
        for(sql in listOf(schema.substringBeforeLast("END;"),"CREATE TRIGGER t AFTER INSERT ON messages INSERT INTO messages VALUES(1); END;", "SELECT 'secret';", "SELECT 1; --comment", "SELECT 1;\u0000")) {
            try{nativeSqlStatements(sql);fail("Malformed fixed schema accepted")}catch(expected:IllegalArgumentException){}
        }
        assertEquals(listOf("BEGIN IMMEDIATE","COMMIT"),nativeSqlStatements("BEGIN IMMEDIATE; COMMIT;"))
        assertEquals(2,nativeSqlStatements("PRAGMA synchronous=FULL; PRAGMA secure_delete=ON;").size)
    }
}
