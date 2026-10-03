package com.peerchat.android;
// Host-JVM bridge check only. This is not the Android application or engine.
public final class NativeBridge {
    public native long create();
    public native int destroy(long handle);
    native int[] info(long handle);
    private native byte[] exchange(long handle,byte[] input);
    public static void main(String[] args) {
        if(args.length>1)System.load(args[1]);
        System.load(args[0]);
        NativeBridge bridge=new NativeBridge();
        byte[] input="PeerChat / 日本語 / café / 😀\u0000end".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        for(int i=0;i<1000;i++){
            long handle=bridge.create();if(handle==0)throw new AssertionError("create");
            if(bridge.info(handle)[0]!=1)throw new AssertionError("ABI");
            if(!java.util.Arrays.equals(input,bridge.exchange(handle,input)))throw new AssertionError("UTF-8");
            if(bridge.exchange(handle,new byte[]{(byte)0xc0,(byte)0x80})!=null)throw new AssertionError("invalid UTF-8");
            if(bridge.destroy(handle)!=0||bridge.destroy(handle)!=2||bridge.info(handle)!=null)throw new AssertionError("retired handle");
        }
        System.out.println("PASS: host JVM JNI, 1000 cycles, Unicode/NUL exchange, invalid UTF-8 and retired handles (NOT Android execution)");
    }
}
