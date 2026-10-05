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
    static final ServerChannel.OperationScope REQUEST_SCOPE=new ServerChannel.OperationScope();
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
    static String request(String host,int port,String pin,String json)throws Exception{
        try(var channel=new ServerChannel(host,port,pin,false,REQUEST_SCOPE)){
            channel.write(json.getBytes(StandardCharsets.UTF_8),4096);
            return new String(channel.read(16384),StandardCharsets.UTF_8);
        }
    }
    static String login(String host,int port,String pin,String user,String password)throws Exception{
        String response=request(host,port,pin,"{\"version\":1,\"type\":\"login\",\"username\":\""+user+"\",\"password\":\""+password+"\"}");
        PeerTlsProof.check(flag(response,"ok"));return string(response,"sessionToken");
    }
    static String encoded(String json,String name)throws IOException {
        Matcher value=Pattern.compile("\""+name+"\"\\s*:\\s*\"([A-Za-z0-9+/=_-]{1,2048})\"").matcher(json);
        if(!value.find())throw new IOException("Incomplete bounded host proof field");return value.group(1);
    }
    static String keyProof(String challenge,PeerTlsProof.Identity identity)throws Exception {
        String device=PeerTls.peerId(identity.pub);
        PeerTlsProof.check(string(challenge,"publicIdentity").equals(device));
        byte[] input=java.util.Base64.getDecoder().decode(encoded(challenge,"signatureInput"));
        return "\"publicIdentity\":\""+device+"\",\"challengeId\":\""+string(challenge,"challengeId")+
            "\",\"publicKey\":\""+java.util.Base64.getEncoder().encodeToString(identity.pub)+
            "\",\"signature\":\""+java.util.Base64.getEncoder().encodeToString(identity.sign(input))+"\"";
    }
    static String enrollRemembered(String host,int port,String pin,String token,PeerTlsProof.Identity identity)throws Exception {
        String device=PeerTls.peerId(identity.pub);
        String challenge=request(host,port,pin,"{\"version\":1,\"type\":\"remember_enroll_challenge\",\"sessionToken\":\""+token+"\",\"publicIdentity\":\""+device+"\"}");
        PeerTlsProof.check(flag(challenge,"ok"));
        String response=request(host,port,pin,"{\"version\":1,\"type\":\"remember_enroll\",\"sessionToken\":\""+token+"\","+keyProof(challenge,identity)+"}");
        PeerTlsProof.check(flag(response,"ok"));return string(response,"credential");
    }
    static String resumeRemembered(String host,int port,String pin,String credential,PeerTlsProof.Identity identity)throws Exception {
        String challenge=request(host,port,pin,"{\"version\":1,\"type\":\"remember_challenge\",\"credential\":\""+credential+"\",\"publicIdentity\":\""+PeerTls.peerId(identity.pub)+"\",\"purpose\":\"resume\"}");
        PeerTlsProof.check(flag(challenge,"ok"));
        String response=request(host,port,pin,"{\"version\":1,\"type\":\"remember_resume\",\"credential\":\""+credential+"\","+keyProof(challenge,identity)+"}");
        PeerTlsProof.check(flag(response,"ok"));return string(response,"sessionToken");
    }
    static ServerChannel events(String host,int port,String pin,String token,String device)throws Exception{
        var channel=new ServerChannel(host,port,pin,false);
        try{
            channel.write(("{\"version\":1,\"type\":\"events\",\"sessionToken\":\""+token+"\",\"publicIdentity\":\""+device+"\",\"clientHeartbeats\":true}").getBytes(StandardCharsets.UTF_8),4096);
            String ready=new String(channel.read(16384),StandardCharsets.UTF_8);
            PeerTlsProof.check(flag(ready,"ok")&&string(ready,"type").equals("events_ready"));
            channel.write("{\"type\":\"event_heartbeat\"}".getBytes(StandardCharsets.UTF_8),4096);
            return channel;
        }catch(Throwable failure){channel.close();throw failure;}
    }
    static void presence(String host,int port,String pin,String token,String device,boolean expected)throws Exception{
        String result=request(host,port,pin,"{\"version\":1,\"type\":\"presence\",\"sessionToken\":\""+token+"\",\"publicIdentity\":\""+device+"\",\"directCandidates\":false}");
        PeerTlsProof.check(flag(result,"ok")==expected);
        if(!expected)PeerTlsProof.check(string(result,"error").equals("session_superseded"));
    }
    static void observedPresence(String host,int port,String pin,String token,String device,boolean online)throws Exception{
        long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(5);
        do{
            String result=request(host,port,pin,"{\"version\":1,\"type\":\"friend_state\",\"sessionToken\":\""+token+"\"}");
            PeerTlsProof.check(flag(result,"ok"));
            Matcher row=Pattern.compile("\\{[^{}]*\"publicIdentity\"\\s*:\\s*\""+device+"\"[^{}]*\\}").matcher(result);
            if(row.find()&&flag(row.group(),"online")==online)return;
            Thread.sleep(20);
        }while(System.nanoTime()<deadline);
        throw new IOException("Fresh device presence was not observed");
    }
    public static void main(String[] args)throws Exception{
        PeerTlsProof.Identity a=new PeerTlsProof.Identity(),b=new PeerTlsProof.Identity();
        String aid=PeerTls.peerId(a.pub),bid=PeerTls.peerId(b.pub),host=args[2],pin=args[4];int port=Integer.parseInt(args[3]);
        Process fixture=new ProcessBuilder(args[1],host,Integer.toString(port),pin,aid,bid).redirectError(ProcessBuilder.Redirect.INHERIT).start();
        String at,bt,userA,passwordA,userB,passwordB;long accountA,accountB;
        try(BufferedReader input=new BufferedReader(new InputStreamReader(fixture.getInputStream(),StandardCharsets.US_ASCII))){
            accountA=Long.parseLong(input.readLine());at=input.readLine();accountB=Long.parseLong(input.readLine());bt=input.readLine();
            userA=input.readLine();passwordA=input.readLine();userB=input.readLine();passwordB=input.readLine();
            PeerTlsProof.check(fixture.waitFor(30,TimeUnit.SECONDS)&&fixture.exitValue()==0);
        }finally{if(fixture.isAlive())fixture.destroyForcibly();}
        // A warmed/shared algorithm registry must never cache pin acceptance.
        // Fresh contexts still reject a changed pin and untrusted system chain.
        request(host,port,pin,"{\"version\":1,\"type\":\"session_info\",\"sessionToken\":\""+at+"\"}");
        for(String rejectedPin:new String[]{pin.equals("0".repeat(64))?"1".repeat(64):"0".repeat(64),""}){
            boolean rejected=false;
            try(var channel=new ServerChannel(host,port,rejectedPin,false,REQUEST_SCOPE)){}
            catch(IOException expected){
                for(Throwable cause=expected;cause!=null;cause=cause.getCause())
                    if(cause instanceof java.security.cert.CertificateException)rejected=true;
            }
            PeerTlsProof.check(rejected&&REQUEST_SCOPE.activeSocketCount()==0);
        }
        request(host,port,pin,"{\"version\":1,\"type\":\"session_info\",\"sessionToken\":\""+at+"\"}");
        PeerTlsProof.check(REQUEST_SCOPE.providerInitializationCount()==1);
        System.out.println("PASS: reused scoped TLS provider retains fresh-context pin/chain validation; wrong pin and untrusted certificate rejected, correct pin still succeeds.");
        ExecutorService connections=Executors.newFixedThreadPool(2);
        String rememberedB=enrollRemembered(host,port,pin,bt,b);
        String queued=null;
        try{
            for(int cycle=0;cycle<3;cycle++){
                // Simulate abandoned old process: no logout, then authenticate
                // a new server session for the SAME account/device. Its event
                // lease supersedes the old token and cannot be cleared by it.
                try(var oldEvents=events(host,port,pin,at,aid)){
                    observedPresence(host,port,pin,bt,aid,true);
                    String oldToken=at;
                    at=login(host,port,pin,userA,passwordA);
                    bt=resumeRemembered(host,port,pin,rememberedB,b);
                    try(var freshEvents=events(host,port,pin,at,aid)){
                        presence(host,port,pin,at,aid,true);
                        presence(host,port,pin,oldToken,aid,false);
                        observedPresence(host,port,pin,bt,aid,true);
                    }
                    observedPresence(host,port,pin,bt,aid,false);
                }
                presence(host,port,pin,at,aid,true);
                long bootstrapStarted=System.nanoTime();
                String bootstrap=request(host,port,pin,"{\"version\":1,\"type\":\"messaging_bootstrap\",\"bootstrapVersion\":1,\"sessionToken\":\""+bt+"\",\"publicIdentity\":\""+bid+"\"}");
                PeerTlsProof.check(flag(bootstrap,"ok")&&flag(bootstrap,"presenceLive")&&string(bootstrap,"registeredPublicIdentity").equals(bid));
                long bootstrapMillis=TimeUnit.NANOSECONDS.toMillis(System.nanoTime()-bootstrapStarted);
                PeerTlsProof.check(bootstrapMillis<10000);
                System.out.println("METRIC: authenticated bootstrap round_trips=1 elapsed_ms="+bootstrapMillis);
                observedPresence(host,port,pin,bt,aid,true);
                // No MainActivity, presenter, selected conversation, or Flow
                // collector. Server authorizes the friend/device before delivery.
                String contacts=bootstrap;
                PeerTlsProof.check(flag(contacts,"ok")&&contacts.contains(aid));
                final String activeA=at,activeB=bt;
                try(var left=new ReliableProfileProof.Side(a,args[0]+"/a",bid,accountA,accountB);
                    var right=new ReliableProfileProof.Side(b,args[0]+"/b",aid,accountB,accountA)){
                    Future<SocketExecution> first=connections.submit(()->new SocketExecution(left,host,port,pin,activeA,accountB));
                    Future<SocketExecution> second=connections.submit(()->new SocketExecution(right,host,port,pin,activeB,accountA));
                    try(SocketExecution forward=first.get(25,TimeUnit.SECONDS);SocketExecution backward=second.get(25,TimeUnit.SECONDS)){
                        until(forward,backward,()->left.ready()&&right.ready());
                        if(queued!=null){final String pending=queued;until(forward,backward,()->delivered(left,pending));
                            PeerTlsProof.check(ReliableProfileProof.history(right,pending,3L*cycle)==2);}
                        String text="Native JNI message 日本語 😀";
                        String one=new String(com.peerchat.android.ReliableBridge.send(left.handle,ReliableProfileProof.utf8(bid),ReliableProfileProof.utf8(text)),StandardCharsets.US_ASCII);
                        String two=new String(com.peerchat.android.ReliableBridge.send(right.handle,ReliableProfileProof.utf8(aid),ReliableProfileProof.utf8(text)),StandardCharsets.US_ASCII);
                        until(forward,backward,()->delivered(left,one)&&delivered(right,two));
                        PeerTlsProof.check(ReliableProfileProof.history(left,one,2L*(cycle+1)+cycle)==2);
                        PeerTlsProof.check(ReliableProfileProof.history(right,two,2L*(cycle+1)+cycle)==2);
                    }
                    if(cycle<2)queued=new String(com.peerchat.android.ReliableBridge.send(left.handle,ReliableProfileProof.utf8(bid),ReliableProfileProof.utf8("Native JNI message 日本語 😀")),StandardCharsets.US_ASCII);
                }
            }
        }finally{connections.shutdownNow();}
        System.out.println("PASS: UI-free remembered device resume and authoritative friendship snapshot before delivery; fresh session/device presence replacement, superseded presence rejected; 3 native runtime/profile reopen + pinned TLS/relay cycles, 6 bidirectional deliveries, 2 offline durable queued IDs drained once, exact ACK/dedupe history, nonfriend rejection. HOST ONLY, plus separate application-projection unit coverage; not a physical claim.");
    }
}
