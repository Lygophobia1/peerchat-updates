package com.peerchat.android;
/** Host declarations for the unchanged production mailbox JNI boundary. */
public final class MailboxBridge {
 public static native int configureEnrollment(long handle,long account,long generation,byte[] server,byte[] pin);
 public static native byte[] registration(long handle,boolean allowed);
 public static native int registered(long handle,byte[] binding);
 public static native byte[] receive(long handle,byte[] envelope);
}
