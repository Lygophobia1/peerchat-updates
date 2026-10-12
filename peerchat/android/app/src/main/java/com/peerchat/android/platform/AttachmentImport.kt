package com.peerchat.android.platform

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream
import java.util.UUID

internal fun attachmentName(raw:String):String {
    var value=raw.substringAfterLast('/').substringAfterLast('\\').map{if(it.code<32||it.code==127||it in "\\/:<>\"|?*")'_' else it}.joinToString("").trimEnd(' ','.')
    if(value.isEmpty()||value=="."||value=="..")value="attachment"
    val stem=value.substringBefore('.').uppercase(java.util.Locale.ROOT)
    if(stem in listOf("CON","PRN","AUX","NUL")||Regex("(COM|LPT)[0-9]").matches(stem))value="_"+value
    while(value.toByteArray(Charsets.UTF_8).size>220)value=value.dropLast(Character.charCount(value.codePointBefore(value.length)))
    return value
}
/** SAF import is copied once into durable app-private storage before native offer.
 * No URI permission, activity or provider handle is required after restart. */
internal fun importAttachment(context:Context,uri:Uri,directory:File):Pair<String,String> {
    require(uri.scheme=="content"&&directory.isDirectory)
    val files=checkNotNull(directory.listFiles());require(files.size<256)
    val retained=files.sumOf{if(it.name.endsWith(".part"))256L*1024*1024 else it.length()}
    require(retained<=1024L*1024*1024)
    val name=context.contentResolver.query(uri,arrayOf(OpenableColumns.DISPLAY_NAME),null,null,null)?.use{c->if(c.moveToFirst())c.getString(0) else null} ?: "attachment"
    val safe=attachmentName(name);val file=File(directory,UUID.randomUUID().toString().replace("-","")+"-"+safe)
    check(file.createNewFile())
    try {
        context.contentResolver.openInputStream(uri).use{input->
            checkNotNull(input);FileOutputStream(file).use{output->val buffer=ByteArray(32768);var total=0L
                while(true){val count=input.read(buffer);if(count<0)break;total+=count;require(total<=256L*1024*1024&&retained+total<=1024L*1024*1024);output.write(buffer,0,count)}
                output.fd.sync()
            }
        }
        // Persist the directory entry as well as the contents before native journal.
        val fd=android.system.Os.open(directory.path,android.system.OsConstants.O_RDONLY,0)
        try{android.system.Os.fsync(fd)}finally{android.system.Os.close(fd)}
        return file.name to safe
    }catch(error:Exception){file.delete();throw error}
}
