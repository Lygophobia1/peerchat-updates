package com.peerchat.android.platform.tls;

import java.io.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.Test;
import static org.junit.Assert.*;

public class ServerChannelWindowTest {
    static class Output extends OutputStream {
        final ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        int writes,flushes;boolean failWrite,failFlush;
        public void write(int value){throw new AssertionError("Expected one bulk TLS write");}
        public void write(byte[] value,int offset,int count)throws IOException {
            writes++;
            if(failWrite){bytes.write(value,offset,Math.min(count,7));throw new IOException("Injected partial write");}
            bytes.write(value,offset,count);
        }
        public void flush()throws IOException {flushes++;if(failFlush)throw new IOException("Injected flush failure");}
    }
    static List<byte[]> frames(int count,int size){
        List<byte[]> result=new ArrayList<>();for(int n=0;n<count;n++){byte[] value=new byte[size];for(int i=0;i<size;i++)value[i]=(byte)(i*31+n);result.add(value);}return result;
    }
    static byte[] legacy(List<byte[]> frames)throws IOException {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();DataOutputStream output=new DataOutputStream(bytes);
        for(byte[] frame:frames){output.writeInt(frame.length);output.write(frame);output.flush();}return bytes.toByteArray();
    }
    @Test public void everyPartialWindowPreservesExactFramingWithOneWriteAndFlush()throws Exception {
        for(int count=1;count<=8;count++){
            List<byte[]> input=frames(count,65536);input.set(count-1,new byte[]{0,1,(byte)255});
            Output output=new Output();AtomicInteger checks=new AtomicInteger();
            ServerChannel.writeWindow(output,input,100000,()->checks.incrementAndGet());
            assertArrayEquals(legacy(input),output.bytes.toByteArray());assertEquals(1,output.writes);assertEquals(1,output.flushes);assertEquals(2,checks.get());
        }
    }
    @Test public void maximumWindowIsExactlyBoundedAndByteEquivalent()throws Exception {
        List<byte[]> input=frames(8,100000);Output output=new Output();ServerChannel.writeWindow(output,input,100000,()->{});
        assertEquals(800032,output.bytes.size());assertArrayEquals(legacy(input),output.bytes.toByteArray());assertEquals(1,output.writes);
    }
    static void invalid(List<byte[]> input,int maximum){
        Output output=new Output();assertThrows(IOException.class,()->ServerChannel.writeWindow(output,input,maximum,()->{}));
        assertEquals(0,output.bytes.size());assertEquals(0,output.writes);assertEquals(0,output.flushes);
    }
    @Test public void allWindowBoundsRejectBeforeAnyOutput(){
        invalid(null,100000);invalid(Collections.emptyList(),100000);invalid(frames(9,1),100000);
        invalid(frames(1,1),0);invalid(frames(1,1),100001);invalid(frames(1,100001),100000);invalid(frames(1,11),10);
    }
    @Test public void malformedFrameAtEveryPositionCannotExposeEarlierFrames(){
        for(int position=0;position<8;position++)for(int fault=0;fault<3;fault++){
            List<byte[]> input=frames(8,20);input.set(position,fault==0?null:new byte[fault==1?0:100001]);invalid(input,100000);
        }
    }
    @Test public void canceledScopeDoesNotWriteAnyPrefix(){
        Output output=new Output();assertThrows(IOException.class,()->ServerChannel.writeWindow(output,frames(8,65536),100000,()->{throw new IOException("Scope ended");}));
        assertEquals(0,output.writes);assertEquals(0,output.flushes);
    }
    static class Resource implements AutoCloseable {
        final Output output=new Output();boolean closed;
        public void close(){closed=true;}
    }
    private void failedOwnerWindow(boolean flushFailure,boolean canceledAfterWrite)throws Exception {
        List<Resource> resources=new ArrayList<>();AtomicInteger exchanges=new AtomicInteger();AtomicInteger applied=new AtomicInteger();
        BoundedRpc<Resource> owner=new BoundedRpc<>(()->{Resource r=new Resource();resources.add(r);return r;},r->!r.closed);
        assertThrows(IOException.class,()->owner.run("same-session",8,r->{
            exchanges.incrementAndGet();r.output.failWrite=!flushFailure&&!canceledAfterWrite;r.output.failFlush=flushFailure;
            AtomicInteger checks=new AtomicInteger();ServerChannel.writeWindow(r.output,frames(8,65536),100000,()->{
                if(checks.incrementAndGet()==2&&canceledAfterWrite)throw new IOException("Scope ended during write");
            });applied.incrementAndGet();return null;
        }));
        assertEquals(1,exchanges.get());assertEquals(0,applied.get());assertEquals(1,resources.size());assertTrue(resources.get(0).closed);
        assertEquals(1,resources.get(0).output.writes);assertEquals(flushFailure||canceledAfterWrite?1:0,resources.get(0).output.flushes);
        // A later explicit operation gets a fresh owner; the ambiguous payload
        // is neither resent nor turned into an observable successful reply.
        owner.run("same-session",r->{assertNotSame(resources.get(0),r);return null;});
        assertEquals(2,resources.size());assertEquals(1,exchanges.get());assertEquals(0,resources.get(1).output.writes);owner.close();assertTrue(resources.get(1).closed);
    }
    @Test public void partialWriteClosesOwnerWithoutReplayOrApply()throws Exception {failedOwnerWindow(false,false);}
    @Test public void flushFailureClosesOwnerWithoutReplayOrApply()throws Exception {failedOwnerWindow(true,false);}
    @Test public void cancellationAfterWriteClosesOwnerWithoutApply()throws Exception {failedOwnerWindow(false,true);}
}
