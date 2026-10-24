package com.peerchat.android.platform

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.util.UUID

internal fun attachmentName(raw:String):String {
    var value=raw.substringAfterLast('/').substringAfterLast('\\').map{if(it.code<32||it.code==127||it in "\\/:<>\"|?*")'_' else it}.joinToString("").trimEnd(' ','.')
    if(value.isEmpty()||value=="."||value=="..")value="attachment"
    val stem=value.substringBefore('.').uppercase(java.util.Locale.ROOT)
    if(stem in listOf("CON","PRN","AUX","NUL")||Regex("(COM|LPT)[0-9]").matches(stem))value="_"+value
    while(value.toByteArray(Charsets.UTF_8).size>220)value=value.dropLast(Character.charCount(value.codePointBefore(value.length)))
    return value
}
internal const val MAX_DIRECT_FILE_BYTES=5L*1024*1024*1024
internal const val MAX_CACHED_FILE_BYTES=1536L*1024*1024
private const val IMPORT_QUOTA_BYTES=10L*1024*1024*1024
private const val IMPORT_FREE_RESERVE=64L*1024*1024
private val importReservations=Any()

/** Reserve known size (the full limit for unknown providers) before copying.
 * Sized sparse partials reserve future storage under the native/shared file lock.
 * Only a synced, atomically renamed source is exposed to the native journal. */
internal fun importAttachment(context:Context,uri:Uri,directory:File,maximum:Long=MAX_DIRECT_FILE_BYTES,checkActive:()->Unit={}):Pair<String,String> {
    require(uri.scheme=="content"&&directory.isDirectory&&maximum in 1..MAX_DIRECT_FILE_BYTES)
    var name="attachment";var expected:Long?=null
    context.contentResolver.query(uri,arrayOf(OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE),null,null,null)?.use{c->
        if(c.moveToFirst()){
            val n=c.getColumnIndex(OpenableColumns.DISPLAY_NAME);if(n>=0&&!c.isNull(n))name=c.getString(n)
            val size=c.getColumnIndex(OpenableColumns.SIZE);if(size>=0&&!c.isNull(size))c.getLong(size).takeIf{it>=0}?.let{expected=it}
        }
    }
    require(expected==null||expected!!<=maximum){if(maximum==MAX_CACHED_FILE_BYTES)"Direct-only: files over 1.5 GiB cannot use the cache. Choose Direct Transfer." else "The direct transfer limit is 5 GiB."}
    val safe=attachmentName(name);val id=UUID.randomUUID().toString().replace("-","")
    val file=File(directory,"$id-$safe");val part=File(directory,"$id.part")
    val reservation=expected?:maximum
    var owned=false
    try {
        synchronized(importReservations){
            java.io.RandomAccessFile(File(directory,".capacity.lock"),"rw").use{gate->gate.channel.lock().use{
                val files=checkNotNull(directory.listFiles()).filter{it.name!=".capacity.lock"}
                require(files.size<256){"Too many retained attachment sources"}
                var logical=0L;var allocated=0L
                for(entry in files){
                    val stat=android.system.Os.lstat(entry.path)
                    require(android.system.OsConstants.S_ISREG(stat.st_mode)&&stat.st_size>=0){"Unsafe attachment storage entry"}
                    logical=Math.addExact(logical,stat.st_size)
                    allocated=Math.addExact(allocated,minOf(stat.st_size,Math.multiplyExact(stat.st_blocks,512L)))
                }
                require(logical<=IMPORT_QUOTA_BYTES&&reservation<=IMPORT_QUOTA_BYTES-logical){"Attachment storage is full; retained transfers are preserved"}
                require(directory.usableSpace>=IMPORT_FREE_RESERVE+logical-allocated+reservation){"Not enough free space to prepare this attachment"}
                check(part.createNewFile());owned=true
                java.io.RandomAccessFile(part,"rw").use{output->output.setLength(reservation);output.fd.sync()}
            }}
        }
        context.contentResolver.openInputStream(uri).use{input->
            checkNotNull(input);java.io.RandomAccessFile(part,"rw").use{output->val buffer=ByteArray(32768);var total=0L
                while(true){checkActive();val count=input.read(buffer);if(count<0)break;if(count==0)continue
                    require(count.toLong()<=reservation-total){"Attachment exceeds its declared size or transfer limit"}
                    require(directory.usableSpace>=IMPORT_FREE_RESERVE+count){"Not enough free space to prepare this attachment"}
                    output.write(buffer,0,count);total+=count
                }
                require(expected==null||total==expected){"Attachment changed while preparing"}
                output.setLength(total);output.fd.sync()
            }
        }
        checkActive();check(part.renameTo(file)){"Could not retain prepared attachment"}
        val fd=android.system.Os.open(directory.path,android.system.OsConstants.O_RDONLY,0)
        try{android.system.Os.fsync(fd)}finally{android.system.Os.close(fd)}
        return file.name to safe
    }catch(error:Exception){if(owned){part.delete();file.delete()};throw error}
}
