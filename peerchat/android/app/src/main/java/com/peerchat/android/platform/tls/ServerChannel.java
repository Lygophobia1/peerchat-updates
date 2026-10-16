package com.peerchat.android.platform.tls;

import java.io.*;
import java.net.*;
import java.security.*;
import java.security.cert.*;
import javax.net.ssl.*;
import org.bouncycastle.jsse.provider.BouncyCastleJsseProvider;
import org.bouncycastle.jce.provider.BouncyCastleProvider;

/** Outer discovery/opaque-relay socket adapter only. Never authenticates a peer
 * device, promotes friend trust, or owns reliable delivery/reconnect decisions. */
public final class ServerChannel implements AutoCloseable {
    /** Public certificate identity only. A mismatch always aborts the handshake. */
    public static final class ServerIdentityChanged extends CertificateException {
        public final String expected,observed;
        ServerIdentityChanged(String expected,String observed){
            super("Server certificate pin mismatch");this.expected=expected;this.observed=observed;
        }
    }
    public static ServerIdentityChanged identityChange(Throwable error){
        for(int depth=0;error!=null&&depth<16;depth++,error=error.getCause())
            if(error instanceof ServerIdentityChanged)return (ServerIdentityChanged)error;
        return null;
    }
    /** Inspection REJECTS in the trust callback, before any application data.
     * Expired/malformed certificates fail before a pin can be offered. */
    public static String inspectCertificate(String host,int port)throws Exception {
        try(ServerChannel ignored=new ServerChannel(host,port,
                "0000000000000000000000000000000000000000000000000000000000000000",false)){
            throw new CertificateException("Certificate inspection did not reject handshake");
        }catch(Exception error){
            ServerIdentityChanged change=identityChange(error);
            if(change==null)throw error;
            return change.observed;
        }
    }
    /** Platform I/O lifetime, not a session/retry owner. Closing raw sockets
     * interrupts connect, TLS, read AND write without waiting for close_notify. */
    public static final class OperationScope {
        private static final java.util.concurrent.ScheduledExecutorService CLOCK =
            java.util.concurrent.Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t=new Thread(r,"PeerChat I/O deadline");t.setDaemon(true);return t;
            });
        private final java.util.Set<Socket> sockets=new java.util.HashSet<>();
        private java.util.concurrent.ScheduledFuture<?> alarm;
        private boolean active=true;
        private long deadline=Long.MAX_VALUE;
        private BouncyCastleJsseProvider tlsProvider;
        private int providerInitializations;
        private long providerInitializationNanos;
        // Provider construction loads the pinned algorithm registry. Reuse that
        // immutable registry within the backend I/O scope, not SSLContext,
        // sessions, credentials, sockets or a platform-global registration.
        private synchronized BouncyCastleJsseProvider tlsProvider() throws IOException {
            check();
            if(tlsProvider==null){
                long start=System.nanoTime();
                tlsProvider=new BouncyCastleJsseProvider(new BouncyCastleProvider());
                providerInitializationNanos=System.nanoTime()-start;providerInitializations++;
            }
            check();return tlsProvider;
        }
        public synchronized int providerInitializationCount(){return providerInitializations;}
        public synchronized long providerInitializationMillis(){return providerInitializationNanos/1_000_000;}
        public synchronized void configure(boolean enabled,long deadlineNanos) {
            if(alarm!=null)alarm.cancel(false);
            active=enabled;deadline=deadlineNanos;
            if(!enabled){closeSockets();return;}
            if(deadlineNanos!=Long.MAX_VALUE){
                long delay=Math.max(0,deadlineNanos-System.nanoTime());
                alarm=CLOCK.schedule(() -> {synchronized(this){
                    if(active&&System.nanoTime()>=deadline){active=false;closeSockets();}
                }},delay,java.util.concurrent.TimeUnit.NANOSECONDS);
            }
        }
        private void closeSockets(){for(Socket s:sockets)try{s.close();}catch(IOException ignored){};sockets.clear();}
        public synchronized void check() throws SocketTimeoutException {
            if(!active||System.nanoTime()>=deadline)throw new SocketTimeoutException("Platform operation lifetime ended");
        }
        private synchronized void attach(Socket socket)throws IOException {check();sockets.add(socket);}
        private synchronized void detach(Socket socket){sockets.remove(socket);}
        public synchronized int activeSocketCount(){return sockets.size();}
    }
    private final SSLSocket socket;
    private final Socket tcp;
    private final OperationScope operations;
    private java.util.concurrent.ScheduledFuture<?> operationAlarm;
    public ServerChannel(String host, int port, String explicitPin, boolean longLived) throws Exception {
        this(host,port,explicitPin,longLived,new OperationScope());
    }
    public ServerChannel(String host, int port, String explicitPin, boolean longLived,OperationScope operations) throws Exception {
        this(host,port,explicitPin,longLived,operations,null);
    }
    public ServerChannel(String host, int port, String explicitPin, boolean longLived,OperationScope operations,
                         java.util.function.BiConsumer<String,Long> timing) throws Exception {
        long started=System.nanoTime();
        this.operations=java.util.Objects.requireNonNull(operations);operations.check();
        if(host == null || host.isEmpty() || port < 1 || port > 65535) throw new IOException("Invalid server endpoint");
        String pin = explicitPin == null ? "" : explicitPin.replace(":", "").replace(" ", "").toLowerCase(java.util.Locale.ROOT);
        if(!pin.isEmpty() && !pin.matches("[0-9a-f]{64}")) throw new IOException("Invalid server certificate pin");
        TrustManagerFactory factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        factory.init((KeyStore)null);
        X509TrustManager system = null;
        for(TrustManager manager : factory.getTrustManagers()) if(manager instanceof X509TrustManager) system=(X509TrustManager)manager;
        if(system == null) throw new CertificateException("System certificate validator unavailable");
        final X509TrustManager platform = system;
        X509TrustManager trust = new X509TrustManager() {
            public X509Certificate[] getAcceptedIssuers(){return platform.getAcceptedIssuers();}
            public void checkClientTrusted(X509Certificate[] chain,String type)throws CertificateException {platform.checkClientTrusted(chain,type);}
            public void checkServerTrusted(X509Certificate[] chain,String type)throws CertificateException {
                if(chain == null || chain.length == 0) throw new CertificateException("Missing server certificate");
                chain[0].checkValidity();
                if(pin.isEmpty()) { platform.checkServerTrusted(chain,type); return; }
                // A pin may be manually supplied or explicitly confirmed by
                // first-use UI. Never accept a replacement automatically.
                try {
                    byte[] observed=MessageDigest.getInstance("SHA-256").digest(chain[0].getEncoded());
                    byte[] expected=new byte[32];
                    for(int i=0;i<32;i++) expected[i]=(byte)Integer.parseInt(pin.substring(i*2,i*2+2),16);
                    if(!MessageDigest.isEqual(expected,observed)){
                        StringBuilder fingerprint=new StringBuilder();
                        for(byte b:observed)fingerprint.append(String.format(java.util.Locale.ROOT,"%02x",b&255));
                        throw new ServerIdentityChanged(pin,fingerprint.toString());
                    }
                } catch(CertificateException error){throw error;}
                  catch(Exception error){throw new CertificateException("Server certificate validation failed",error);}
            }
        };
        // Reuse the pinned algorithm provider within this I/O scope. Each
        // connection still gets a fresh SSLContext and the unchanged validator.
        // No global provider
        // registration and no Android identity/private-key substitution. Outer
        // server TLS uses account authentication, not mutual device certificates.
        int previousInitializations=operations.providerInitializationCount();
        long providerAcquireStarted=System.nanoTime();
        BouncyCastleJsseProvider provider=operations.tlsProvider();
        if(timing!=null)try{
            timing.accept("crypto_provider_initializations",(long)operations.providerInitializationCount());
            timing.accept(previousInitializations==0?"crypto_provider_initialized":"crypto_provider_reused",(System.nanoTime()-providerAcquireStarted)/1_000_000);
            if(previousInitializations==0)timing.accept("crypto_provider_initialization_duration",operations.providerInitializationMillis());
        }catch(RuntimeException ignored){/* Instrumentation cannot affect TLS/trust. */}
        SSLContext context=SSLContext.getInstance("TLS",provider);context.init(null,new TrustManager[]{trust},null);
        observe(timing,"tls_context_ready",started);
        tcp=new Socket(); SSLSocket candidate=null;
        try {
            operations.attach(tcp);arm(10000);
            tcp.setTcpNoDelay(true);
            tcp.connect(new InetSocketAddress(host,port),10000);tcp.setSoTimeout(10000);
            observe(timing,"server_tcp_connected",started);
            candidate=(SSLSocket)context.getSocketFactory().createSocket(tcp,host,port,true);
            java.util.List<String> versions=new java.util.ArrayList<>();
            for(String version:candidate.getSupportedProtocols()) if(version.equals("TLSv1.2")||version.equals("TLSv1.3")) versions.add(version);
            candidate.setEnabledProtocols(versions.toArray(new String[0]));
            SSLParameters parameters=candidate.getSSLParameters();
            if(pin.isEmpty()) parameters.setEndpointIdentificationAlgorithm("HTTPS");
            candidate.setSSLParameters(parameters);candidate.startHandshake();
            observe(timing,"server_tls_ready",started);
            if(longLived){candidate.setSoTimeout(0);disarm();}
            socket=candidate;
        } catch(Exception error){disarm();operations.detach(tcp);tcp.close();throw error;}
    }
    private static void observe(java.util.function.BiConsumer<String,Long> timing,String stage,long started) {
        // Fixed stages and elapsed time only. Diagnostics cannot change TLS behavior.
        if(timing!=null)try{timing.accept(stage,(System.nanoTime()-started)/1_000_000);}catch(RuntimeException ignored){}
    }
    private void arm(int milliseconds){disarm();if(milliseconds>0)
        operationAlarm=OperationScope.CLOCK.schedule(() -> {try{tcp.close();}catch(IOException ignored){}},milliseconds,java.util.concurrent.TimeUnit.MILLISECONDS);}
    private void disarm(){if(operationAlarm!=null){operationAlarm.cancel(false);operationAlarm=null;}}
    public synchronized void write(byte[] data,int maximum)throws IOException {
        operations.check();
        if(data==null||data.length==0||data.length>maximum)throw new IOException("Invalid server frame size");
        DataOutputStream output=new DataOutputStream(socket.getOutputStream());output.writeInt(data.length);output.write(data);output.flush();
    }
    /** A long-lived reader has no idle read timeout. Bound its independent
     * writer without changing that read policy or allocating a thread per write. */
    public void writeBounded(byte[] data,int maximum,int milliseconds)throws IOException {
        boundedWrite(tcp,milliseconds,() -> write(data,maximum));
    }
    interface WriteOperation { void run() throws IOException; }
    static void boundedWrite(Socket socket,int milliseconds,WriteOperation operation)throws IOException {
        if(milliseconds<1||milliseconds>30000)throw new IllegalArgumentException("Invalid write timeout");
        java.util.concurrent.ScheduledFuture<?> deadline=OperationScope.CLOCK.schedule(
            () -> {try{socket.close();}catch(IOException ignored){}},milliseconds,java.util.concurrent.TimeUnit.MILLISECONDS);
        try{operation.run();}finally{deadline.cancel(false);}
    }
    public byte[] read(int maximum)throws IOException {
        operations.check();
        if(maximum<1||maximum>100000)throw new IOException("Invalid frame bound");
        DataInputStream input=new DataInputStream(socket.getInputStream());int size=input.readInt();
        if(size<1||size>maximum)throw new IOException("Invalid server frame size");
        byte[] data=new byte[size];input.readFully(data);return data;
    }
    public void readTimeout(int milliseconds)throws SocketException {
        if(milliseconds<0||milliseconds>30000)throw new IllegalArgumentException("Invalid read timeout");
        socket.setSoTimeout(milliseconds);
        arm(milliseconds);
    }
    public boolean isOpen(){return !tcp.isClosed();}
    public void close()throws IOException {disarm();operations.detach(tcp);tcp.close();}
}
