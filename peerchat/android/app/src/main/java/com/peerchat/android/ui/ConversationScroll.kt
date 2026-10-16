package com.peerchat.android.ui

/** Includes the older/date rows; null before first layout is not user intent. */
internal fun conversationNearLatest(lastVisible:Int?,total:Int):Boolean =
    total==0||lastVisible==null||lastVisible>=total-3
