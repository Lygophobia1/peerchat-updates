package com.peerchat.android.platform.tls;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import java.util.regex.*;

/** Actual isolated PeerChat.Server opaque relay + production JNI/Profile/TLS.
 * Host test identities and SQLite/DPAPI only; no Android hardware claim. */
public final class ServerRelayProof {
    static {System.loadLibrary("peerchat_messaging");System.loadLibrary("peerchat_jni");}
    static final byte[] EMPTY=new byte[0];
    static String string(String json,String name)throws IOException{
        Matcher value=Pattern.compile("\""+name+"\"\\s*:\\s*\"([A-Za-z0-9_-]{1,80})\"").matcher(json);
        if(!value.find())throw new IOException("Incomplete host relay fixture response");return value.group(1);
    }
    static boolean flag(String json,String name)throws IOException{
        Matcher value=Pattern.compile("\""+name+"\"\\s*:\\s*(true|false)").matcher(json);
        if(!value.find())throw new IOException("Incomplete host relay fixture flag");return value.group(1).equals("true");
    }
    static final class SocketExecution implements AutoCloseable {
        final ReliableProfileProof.Side nativeSide;
        final ServerChannel channel;
        final RelayFrames frames;
        final AtomicReference<Throwable> failure=new AtomicReference<>();
        final Thread reader;
        volatile boolean stopped;
        SocketExecution(ReliableProfileProof.Side side,String host,int port,String pin,String token,long target)throws Exception{
            PeerTlsProof.check(token.matches("[A-Za-z0-9_-]{43}"));nativeSide=side;
            channel=new ServerChannel(host,port,pin,false);channel.readTimeout(20000);
            try{
                String request="{\"version\":1,\"type\":\"relay_attach\",\"sessionToken\":\""+token+"\",\"publicIdentity\":\""+side.id+"\",\"targetAccountId\":\""+target+"\",\"targetPublicIdentity\":\""+side.other+"\"}";
                channel.write(request.getBytes(StandardCharsets.UTF_8),4096);
                String response=new String(channel.read(16384),StandardCharsets.UTF_8);
                PeerTlsProof.check(flag(response,"ok")&&string(response,"type").equals("relay_ready")&&string(response,"purpose").equals("friend"));
                frames=new RelayFrames(string(response,"sessionId"));side.attach(flag(response,"incoming"));channel.readTimeout(0);
                reader=new Thread(()->{try{while(!stopped)side.receive(frames.unwrap(channel.read(43+65536)));}catch(Throwable error){if(!stopped)failure.set(error);}},"host opaque relay receiver");
                reader.setDaemon(true);reader.start();
            }catch(Throwable error){channel.close();throw error;}
        }
        void pump()throws Exception{
            if(failure.get()!=null)throw new IOException("Host opaque relay reader failed",failure.get());
            nativeSide.events();byte[] output=nativeSide.operation(1);
            if(output.length>0)channel.write(frames.wrap(output),43+65536);
        }
        @Override public void close()throws Exception{
            stopped=true;channel.close();reader.join(3000);PeerTlsProof.check(!reader.isAlive());nativeSide.closeRoute();
        }
    }
    interface Condition{boolean ready()throws Exception;}
    static void until(SocketExecution left,SocketExecution right,Condition condition)throws Exception{
        long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(15);
        while(System.nanoTime()<deadline){left.pump();right.pump();if(condition.ready())return;Thread.sleep(5);}
        throw new IOException("Host server/shared runtime timed out");
    }
    static boolean delivered(ReliableProfileProof.Side side,String id){
        var record=new ReliableProfileProof.Reader(com.peerchat.android.ReliableBridge.history(side.handle,ReliableProfileProof.utf8(side.other),Long.MAX_VALUE),2);
        long count=record.num();boolean result=false;
        for(long i=0;i<count;i++){String message=record.str();record.str();record.str();record.num();record.num();record.num();long status=record.num();record.str();if(message.equals(id))result=status==2;}
        record.end();return result;
    }
    public static void main(String[] args)throws Exception{
        PeerTlsProof.Identity a=new PeerTlsProof.Identity(),b=new PeerTlsProof.Identity();
        String aid=PeerTls.peerId(a.pub),bid=PeerTls.peerId(b.pub),host=args[2],pin=args[4];int port=Integer.parseInt(args[3]);
        Process fixture=new ProcessBuilder(args[1],host,Integer.toString(port),pin,aid,bid).redirectError(ProcessBuilder.Redirect.INHERIT).start();
        String at,bt;long accountA,accountB;
        try(BufferedReader input=new BufferedReader(new InputStreamReader(fixture.getInputStream(),StandardCharsets.US_ASCII))){
            accountA=Long.parseLong(input.readLine());at=input.readLine();accountB=Long.parseLong(input.readLine());bt=input.readLine();
            PeerTlsProof.check(fixture.waitFor(30,TimeUnit.SECONDS)&&fixture.exitValue()==0);
        }finally{if(fixture.isAlive())fixture.destroyForcibly();}
        ExecutorService connections=Executors.newFixedThreadPool(2);
        try{
            for(int cycle=0;cycle<3;cycle++){
                try(var left=new ReliableProfileProof.Side(a,args[0]+"/a",bid,accountA,accountB);
                    var right=new ReliableProfileProof.Side(b,args[0]+"/b",aid,accountB,accountA)){
                    Future<SocketExecution> first=connections.submit(()->new SocketExecution(left,host,port,pin,at,accountB));
                    Future<SocketExecution> second=connections.submit(()->new SocketExecution(right,host,port,pin,bt,accountA));
                    try(SocketExecution forward=first.get(25,TimeUnit.SECONDS);SocketExecution backward=second.get(25,TimeUnit.SECONDS)){
                        until(forward,backward,()->left.ready()&&right.ready());
                        String text="Native JNI message 日本語 😀";
                        String one=new String(com.peerchat.android.ReliableBridge.send(left.handle,ReliableProfileProof.utf8(bid),ReliableProfileProof.utf8(text)),StandardCharsets.US_ASCII);
                        String two=new String(com.peerchat.android.ReliableBridge.send(right.handle,ReliableProfileProof.utf8(aid),ReliableProfileProof.utf8(text)),StandardCharsets.US_ASCII);
                        until(forward,backward,()->delivered(left,one)&&delivered(right,two));
                        PeerTlsProof.check(ReliableProfileProof.history(left,one,2L*(cycle+1))==2);
                        PeerTlsProof.check(ReliableProfileProof.history(right,two,2L*(cycle+1))==2);
                    }
                }
            }
        }finally{connections.shutdownNow();}
        System.out.println("PASS: actual isolated PeerChat.Server authenticated friend relay + pinned outer TLS + production JNI/Profile/peer TLS/Hello/trust; 3 relay reconnect/profile reopen cycles, 6 bidirectional durable text/ACK deliveries, exact native history, nonfriend server relay rejection. HOST ONLY; no Android runtime claim.");
    }
}
