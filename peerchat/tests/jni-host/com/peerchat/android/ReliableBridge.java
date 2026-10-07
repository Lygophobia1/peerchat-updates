package com.peerchat.android;

/** JNI rejection/lifetime proof only. Does not fake a production profile. */
public final class ReliableBridge {
    static { System.loadLibrary("peerchat_messaging");System.loadLibrary("peerchat_jni"); }
    public static native int friends(long profile,long account,long generation,byte[] snapshot);
    public static native int presence(long profile,byte[] expectedPeer,boolean online,long generation,long network);
    public static native int demand(long profile,byte[] expectedPeer);
    public static native byte[] requests(long profile);
    public static native long open(long profile,long attempt,boolean incoming,byte[] expectedPeer);
    public static native byte[] exchange(long profile,long peer,int operation,byte[] input);
    public static native int close(long profile,long peer,boolean recoverable);
    public static native byte[] send(long profile,byte[] expectedPeer,byte[] text);
    public static native byte[] history(long profile,byte[] expectedPeer,long before);
    public static native byte[] state(long profile,byte[] expectedPeer);
    static void require(boolean value,String reason){if(!value)throw new AssertionError(reason);}
    public static void main(String[] args){
        NativeBridge foundation=new NativeBridge();
        byte[] peer=new byte[64];java.util.Arrays.fill(peer,(byte)'b');
        byte[] empty=new byte[0],snapshot=new byte[]{'P','T',1,1,0,0,0,0,0,0,0,0};
        for(int cycle=0;cycle<1000;cycle++){
            long handle=foundation.create();require(handle!=0,"create");
            require(friends(handle,0,1,snapshot)==1,"unbound profile accepted auth snapshot");
            require(friends(handle,1,1,null)==1,"null snapshot accepted");
            require(open(handle,1,false,peer)==0,"unbound profile opened production peer");
            require(open(handle,1,true,new byte[63])==0,"malformed identity accepted");
            require(presence(handle,peer,true,1,1)==1&&requests(handle)==null,"unbound profile requested relay");
            require(exchange(handle,1,0,empty)==null,"unknown transport accepted");
            require(exchange(handle,1,0,new byte[65537])==null,"oversized transport accepted");
            require(send(handle,peer,new byte[]{'x'})==null,"unbound profile queued text");
            require(history(handle,peer,Long.MAX_VALUE)==null,"unbound profile returned history");
            require(state(handle,peer)==null,"unbound profile returned session state");
            require(close(handle,1,true)==2,"unknown transport closed successfully");
            require(foundation.destroy(handle)==0,"destroy");
            require(friends(handle,0,1,snapshot)==2,"stale profile accepted snapshot");
            require(open(handle,1,false,peer)==0&&exchange(handle,1,2,empty)==null,"stale runtime handle used");
            require(close(handle,1,false)==2,"stale profile closed transport");
        }
        System.out.println("PASS: reliable JNI exports, 1000 invalid/unbound/stale lifetimes, bounded inputs and fail-closed missing profile. HOST ONLY; no real profile messaging claim.");
    }
}
