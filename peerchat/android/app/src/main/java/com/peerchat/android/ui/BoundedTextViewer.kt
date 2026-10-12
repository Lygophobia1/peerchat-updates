package com.peerchat.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp

internal fun codeLike(text:String):Boolean {
    val sample=text.take(8192)
    return sample.contains("~~~")||sample.contains("```")||
        sample.lineSequence().count{it.trimStart().let{line->line.startsWith("#include")||line.startsWith("import ")||line.startsWith("fun ")||line.startsWith("class ")||line.startsWith("def ")||line.startsWith("public ")}}>1
}
internal fun textViewRows(text:String):List<String> = text.split('\n').flatMap{line->
    val rows=mutableListOf<String>();var start=0
    while(start<line.length){var end=(start+2048).coerceAtMost(line.length);if(end<line.length&&line[end-1].isHighSurrogate()&&line[end].isLowSurrogate())end--;rows.add(line.substring(start,end));start=end}
    rows.ifEmpty{listOf("")}
}
@Composable private fun TextViewport(text:String,monospace:Boolean,modifier:Modifier) {
    val rows=remember(text){textViewRows(text)};val list=rememberLazyListState()
    val accent=MaterialTheme.colorScheme.primary
    Box(modifier){
        LazyColumn(Modifier.fillMaxSize().padding(end=10.dp).testTag("text-scroll"),state=list){
            items(rows.size){index->Text(rows[index],fontFamily=if(monospace)FontFamily.Monospace else FontFamily.Default,
                style=MaterialTheme.typography.bodyMedium)}
        }
        Canvas(Modifier.align(Alignment.CenterEnd).width(4.dp).fillMaxHeight()){
            val visible=list.layoutInfo.visibleItemsInfo.size
            if(rows.size>visible&&visible>0){
                val thumb=(size.height*visible/rows.size).coerceAtLeast(24.dp.toPx()).coerceAtMost(size.height)
                val top=(size.height-thumb)*list.firstVisibleItemIndex/(rows.size-visible).coerceAtLeast(1)
                drawRect(accent.copy(alpha=.65f),Offset(0f,top),Size(size.width,thumb))
            }
        }
    }
}
@Composable internal fun BoundedTextViewer(text:String,monospace:Boolean,title:String) {
    var expanded by remember{mutableStateOf(false)};var full by remember{mutableStateOf(false)}
    val clipboard=LocalClipboardManager.current
    TextViewport(text,monospace,Modifier.fillMaxWidth().height(if(expanded)420.dp else 220.dp))
    Row{
        TextButton({expanded=!expanded}){Text(if(expanded)"Show less" else "Expand")}
        TextButton({clipboard.setText(AnnotatedString(text))}){Text("Copy")}
        TextButton({full=true}){Text("Open full")}
    }
    if(full)androidx.compose.ui.window.Dialog(onDismissRequest={full=false},properties=androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth=false)){
        Surface(Modifier.fillMaxSize(),color=MaterialTheme.colorScheme.background){Column(Modifier.safeDrawingPadding().padding(12.dp)){
            Row(verticalAlignment=Alignment.CenterVertically){Text(title,Modifier.weight(1f));TextButton({full=false}){Text("Close")};TextButton({clipboard.setText(AnnotatedString(text))}){Text("Copy")}}
            TextViewport(text,monospace,Modifier.fillMaxWidth().weight(1f))
        }}
    }
}
