package com.peerchat.android;
/** Test declarations for the existing production archive owner. */
public final class ArchiveBridge {
    public static native byte[] request(long profile,int operation,byte[] input);
}
