package com.peerchat.android;
/** Test declarations for the production local adapter; no alternate owner. */
public final class TransferBridge {
    public static native byte[] request(long profile,int operation,byte[] input);
}
