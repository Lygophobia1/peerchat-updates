package com.peerchat.android;
public final class PeerTlsBridge {
    static {System.loadLibrary("peerchat_messaging");}
    public static native long open(long profile,boolean incoming,byte[] expected);
    public static native byte[] exchange(long profile,long session,int operation,byte[] input);
    public static native int close(long profile,long session);
}
