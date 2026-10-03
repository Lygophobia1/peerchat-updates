package com.peerchat.android.platform.tls;
import java.util.Arrays;
public final class RelayFramesProof {
    interface Action{void run()throws Exception;}
    static void reject(Action action)throws Exception{boolean failed=false;try{action.run();}catch(java.io.IOException expected){failed=true;}if(!failed)throw new AssertionError("Malformed relay envelope accepted");}
    public static void main(String[] args)throws Exception{
        RelayFrames frames=new RelayFrames("a".repeat(43));
        for(int cycle=0;cycle<1000;cycle++){
            byte[] input=new byte[1+cycle%65536];Arrays.fill(input,(byte)cycle);
            byte[] wrapped=frames.wrap(input),copy=frames.unwrap(wrapped);input[0]^=1;
            if(copy[0]!=(byte)cycle||copy.length!=wrapped.length-43)throw new AssertionError("Copied buffer ownership changed");
            wrapped[0]^=1;reject(()->frames.unwrap(wrapped));
        }
        byte[] maximum=new byte[65536];if(frames.unwrap(frames.wrap(maximum)).length!=65536)throw new AssertionError("Maximum changed");
        reject(()->new RelayFrames("b".repeat(42)));reject(()->new RelayFrames("é".repeat(43)));
        reject(()->frames.wrap(new byte[0]));reject(()->frames.wrap(new byte[65537]));reject(()->frames.wrap(null));
        reject(()->frames.unwrap(new byte[43]));reject(()->frames.unwrap(new byte[65580]));reject(()->frames.unwrap(null));
        System.out.println("PASS: 1000 existing relay envelope copied-buffer/session-binding cycles; exact 64 KiB bound, malformed/empty/oversize/non-ASCII rejection. HOST ONLY");
    }
}
