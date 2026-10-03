package com.peerchat.android;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.Executors;
import java.util.ArrayList;
import java.util.concurrent.Future;

// Host failure-boundary control only, NOT a replacement SQLite/Keystore backend.
public final class ProfileBridge {
    static { System.loadLibrary("peerchat_messaging"); System.loadLibrary("peerchat_jni"); }
    private static final NativeBridge foundation=new NativeBridge();
    public static native int bind(long handle, Object platform);
    public static native byte[] publicKey(long handle);
    static native byte[] sign(long handle, byte[] input);
    public static final class UnavailableDatabase {
        static final AtomicInteger closes=new AtomicInteger();
        boolean closed;
        public void execute(byte[] sql) { throw new IllegalStateException("Synthetic platform database unavailable"); }
        public void close() { if(closed)throw new AssertionError("Double-close");closed=true;closes.incrementAndGet(); }
    }
    static void check(boolean value) { if(!value)throw new AssertionError("JNI profile failure boundary failed"); }
    static void cycle() {
        long handle=foundation.create();check(handle!=0);
        check(bind(handle,new UnavailableDatabase())==6);
        check(publicKey(handle)==null&&sign(handle,new byte[]{1})==null);
        int[] state=foundation.info(handle);check(state[2]==0&&state[3]==0&&state[4]==0);
        check(PeerTlsBridge.open(handle,false,new byte[64])==0);
        check(PeerTlsBridge.exchange(handle,1,5,new byte[0])==null);
        check(PeerTlsBridge.close(handle,1)==2);
        check(foundation.destroy(handle)==0&&foundation.destroy(handle)==2);
    }
    public static void main(String[] args) throws Exception {
        for(int i=0;i<1000;i++)cycle();
        var pool=Executors.newFixedThreadPool(4);
        try {
            var tasks=new ArrayList<Future<?>>();
            for(int i=0;i<100;i++)tasks.add(pool.submit(ProfileBridge::cycle));
            for(var result:tasks)result.get();
        } finally {pool.shutdown();}
        check(UnavailableDatabase.closes.get()==1100);
        check(bind(0,new UnavailableDatabase())==2&&publicKey(0)==null&&sign(0,new byte[]{1})==null);
        System.out.println("PASS: JNI provider failure containment, 1100 lifecycles including 100 concurrent, exact close, invalid/stale handles; no SQLite/Keystore runtime claim");
    }
}
