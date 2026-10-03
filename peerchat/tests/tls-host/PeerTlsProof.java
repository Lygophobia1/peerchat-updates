package com.peerchat.android.platform.tls;

import java.io.*;
import java.math.BigInteger;
import java.security.*;
import java.security.interfaces.ECPublicKey;
import java.security.spec.ECGenParameterSpec;
import java.util.*;
import org.bouncycastle.asn1.*;

public final class PeerTlsProof {
    static final class Identity implements OpaqueTlsSigner.Identity {
        final KeyPair key;
        final byte[] pub;
        int signs;
        boolean broken;
        final List<byte[]> inputs=new ArrayList<>();
        Identity()throws Exception{KeyPairGenerator g=KeyPairGenerator.getInstance("EC");g.initialize(new ECGenParameterSpec("secp256r1"));key=g.generateKeyPair();pub=PeerTls.publicBytes((ECPublicKey)key.getPublic());}
        public byte[] publicKey(){return pub.clone();}
        public byte[] sign(byte[] original)throws Exception{
            if(broken)throw new IOException("Synthetic provider failure");signs++;inputs.add(original.clone());
            Signature signature=Signature.getInstance("SHA256withECDSA");signature.initSign(key.getPrivate());signature.update(original);
            ASN1Sequence values=ASN1Sequence.getInstance(signature.sign());byte[] raw=new byte[64];
            for(int i=0;i<2;i++){byte[] b=ASN1Integer.getInstance(values.getObjectAt(i)).getValue().toByteArray();int size=Math.min(32,b.length);System.arraycopy(b,b.length-size,raw,i*32+32-size,size);}return raw;
        }
    }
    static void check(boolean value){if(!value)throw new AssertionError("Peer TLS harness failed");}
    interface Action{void run()throws Exception;}
    static void rejected(Action call)throws Exception{boolean failed=false;try{call.run();}catch(Exception expected){failed=true;}check(failed);}
    static void feed(PeerTls from,PeerTls to)throws Exception{
        byte[] bytes=from.takeOutput();for(int start=0;start<bytes.length;start+=19)to.receive(Arrays.copyOfRange(bytes,start,Math.min(bytes.length,start+19)));
    }
    static void establish(PeerTls client,PeerTls server)throws Exception{
        for(int i=0;i<100 && !(client.ready() && server.ready());i++){feed(client,server);feed(server,client);}check(client.ready() && server.ready());
    }
    public static void main(String[] args)throws Exception{
        Identity a=new Identity(),b=new Identity();String aid=PeerTls.peerId(a.pub),bid=PeerTls.peerId(b.pub);
        for(boolean tls13:new boolean[]{false,true}){
            a.inputs.clear();b.inputs.clear();
            try(PeerTls left=new PeerTls(a,false,bid,tls13);PeerTls right=new PeerTls(b,true,aid,tls13)){
                establish(left,right);check(Arrays.equals(left.remotePublicKey(),b.pub) && Arrays.equals(right.remotePublicKey(),a.pub));
                byte[] text="TLS application bytes / 日本語\0😀".getBytes(java.nio.charset.StandardCharsets.UTF_8);
                left.send(text);feed(left,right);check(Arrays.equals(right.takePlaintext(),text));right.send(text);feed(right,left);check(Arrays.equals(left.takePlaintext(),text));
                // Certificate generation plus real TLS handshake both invoke the
                // original-message path; no private key is provided to the TLS lib.
                check(a.inputs.size()>=2 && b.inputs.size()>=2);
                if(tls13){byte[] prefix="TLS 1.3, client CertificateVerify".getBytes(java.nio.charset.StandardCharsets.US_ASCII);byte[] input=a.inputs.get(a.inputs.size()-1);check(input.length==64+prefix.length+1+32);for(int i=0;i<64;i++)check(input[i]==0x20);check(Arrays.equals(Arrays.copyOfRange(input,64,64+prefix.length),prefix));}
                left.close();feed(left,right);check(right.closed());
            }
        }
        long now=System.currentTimeMillis();byte[] cert=PeerTls.certificate(a,new Date(now-86400000L),new Date(now+86400000L));
        check(Arrays.equals(PeerTls.validateCertificate(cert,aid),a.pub));
        rejected(()->PeerTls.validateCertificate(cert,bid));rejected(()->PeerTls.validateCertificate(new byte[]{1,2,3},aid));
        byte[] altered=cert.clone();altered[altered.length-1]^=1;rejected(()->PeerTls.validateCertificate(altered,aid));
        byte[] expired=PeerTls.certificate(a,new Date(now-172800000L),new Date(now-86400000L));rejected(()->PeerTls.validateCertificate(expired,aid));
        byte[] future=PeerTls.certificate(a,new Date(now+86400000L),new Date(now+172800000L));rejected(()->PeerTls.validateCertificate(future,aid));
        rejected(()->new PeerTls(null,false,bid));
        try(PeerTls wrong=new PeerTls(a,false,aid);PeerTls right=new PeerTls(b,true,aid)){rejected(()->establish(wrong,right));check(!wrong.ready());}
        try(PeerTls left=new PeerTls(a,false,bid);PeerTls right=new PeerTls(b,true,aid)){a.broken=true;rejected(()->establish(left,right));check(!left.ready());}finally{a.broken=false;}
        for(int i=0;i<100;i++)try(PeerTls left=new PeerTls(a,false,bid);PeerTls right=new PeerTls(b,true,aid)){establish(left,right);}
        if(args.length==2)schannel(a,args[0],args[1]);
        System.out.println("PASS: real external-signing TLS 1.2 + 1.3 mutual handshakes; exact TLS1.3 signature-input structure; fragmented records; bidirectional plaintext; certificate/pin/time/tamper/provider rejection; 100 lifecycles. HOST ONLY");
    }
    static byte[] unhex(String text)throws Exception{if(text==null || text.length()%2!=0 || text.length()>524288 || !text.matches("[0-9a-f]*"))throw new IOException("Invalid fixture bytes");byte[] bytes=new byte[text.length()/2];for(int i=0;i<bytes.length;i++)bytes[i]=(byte)Integer.parseInt(text.substring(i*2,i*2+2),16);return bytes;}
    static String hex(byte[] bytes){StringBuilder text=new StringBuilder();for(byte b:bytes)text.append(String.format(Locale.ROOT,"%02x",b&255));return text.toString();}
    static void schannel(Identity identity,String executable,String profile)throws Exception{
        Process child=new ProcessBuilder(executable,profile,PeerTls.peerId(identity.pub)).redirectError(ProcessBuilder.Redirect.INHERIT).start();
        java.util.concurrent.ScheduledExecutorService watchdog=java.util.concurrent.Executors.newSingleThreadScheduledExecutor();
        watchdog.schedule(()->{if(child.isAlive())child.destroyForcibly();},30,java.util.concurrent.TimeUnit.SECONDS);
        try(BufferedReader input=new BufferedReader(new InputStreamReader(child.getInputStream(),java.nio.charset.StandardCharsets.US_ASCII));PrintWriter output=new PrintWriter(child.getOutputStream(),true)){
            byte[] pub=unhex(input.readLine());
            try(PeerTls client=new PeerTls(identity,false,PeerTls.peerId(pub))){
                boolean remoteReady=false;
                for(int i=0;i<20 && !(remoteReady && client.ready());i++){
                    output.println("R:"+hex(client.takeOutput()));String wire=input.readLine(),plain=input.readLine(),state=input.readLine();
                    check(wire!=null && wire.startsWith("O:") && plain!=null && plain.startsWith("P:") && state!=null);
                    client.receive(unhex(wire.substring(2)));remoteReady=state.equals("R:1");
                }
                check(client.ready() && remoteReady);
                byte[] text="Synthetic Schannel echo / 日本語\0😀".getBytes(java.nio.charset.StandardCharsets.UTF_8);
                client.send(text);output.println("R:"+hex(client.takeOutput()));String wire=input.readLine(),plain=input.readLine(),state=input.readLine();
                check(Arrays.equals(unhex(plain.substring(2)),text) && state.equals("R:1"));client.receive(unhex(wire.substring(2)));check(Arrays.equals(client.takePlaintext(),text));
                output.println("X");check(child.waitFor(5,java.util.concurrent.TimeUnit.SECONDS) && child.exitValue()==0);
            }
        }finally{watchdog.shutdownNow();if(child.isAlive())child.destroyForcibly();}
        System.out.println("PASS: external original-message TLS1.2 mutual identity + byte echo against unchanged Windows Schannel. HOST ONLY");
    }
}
