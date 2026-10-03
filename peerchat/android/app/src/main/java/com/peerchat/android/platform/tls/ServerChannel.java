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
    private final SSLSocket socket;
    public ServerChannel(String host, int port, String explicitPin, boolean longLived) throws Exception {
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
                // Explicit out-of-band developer pin, as on Windows. Never TOFU,
                // never accept a replacement automatically; no trust-all mode.
                try {
                    byte[] observed=MessageDigest.getInstance("SHA-256").digest(chain[0].getEncoded());
                    byte[] expected=new byte[32];
                    for(int i=0;i<32;i++) expected[i]=(byte)Integer.parseInt(pin.substring(i*2,i*2+2),16);
                    if(!MessageDigest.isEqual(expected,observed)) throw new CertificateException("Server certificate pin mismatch");
                } catch(CertificateException error){throw error;}
                  catch(Exception error){throw new CertificateException("Server certificate validation failed",error);}
            }
        };
        // Reuse the pinned TLS library, instance-locally. No global provider
        // registration and no Android identity/private-key substitution. Outer
        // server TLS uses account authentication, not mutual device certificates.
        BouncyCastleJsseProvider provider=new BouncyCastleJsseProvider(new BouncyCastleProvider());
        SSLContext context=SSLContext.getInstance("TLS",provider);context.init(null,new TrustManager[]{trust},null);
        Socket tcp=new Socket(); SSLSocket candidate=null;
        try {
            tcp.connect(new InetSocketAddress(host,port),10000);tcp.setSoTimeout(10000);
            candidate=(SSLSocket)context.getSocketFactory().createSocket(tcp,host,port,true);
            java.util.List<String> versions=new java.util.ArrayList<>();
            for(String version:candidate.getSupportedProtocols()) if(version.equals("TLSv1.2")||version.equals("TLSv1.3")) versions.add(version);
            candidate.setEnabledProtocols(versions.toArray(new String[0]));
            SSLParameters parameters=candidate.getSSLParameters();
            if(pin.isEmpty()) parameters.setEndpointIdentificationAlgorithm("HTTPS");
            candidate.setSSLParameters(parameters);candidate.startHandshake();
            if(longLived) candidate.setSoTimeout(0);
            socket=candidate;
        } catch(Exception error){if(candidate!=null)candidate.close();else tcp.close();throw error;}
    }
    public synchronized void write(byte[] data,int maximum)throws IOException {
        if(data==null||data.length==0||data.length>maximum)throw new IOException("Invalid server frame size");
        DataOutputStream output=new DataOutputStream(socket.getOutputStream());output.writeInt(data.length);output.write(data);output.flush();
    }
    public byte[] read(int maximum)throws IOException {
        if(maximum<1||maximum>100000)throw new IOException("Invalid frame bound");
        DataInputStream input=new DataInputStream(socket.getInputStream());int size=input.readInt();
        if(size<1||size>maximum)throw new IOException("Invalid server frame size");
        byte[] data=new byte[size];input.readFully(data);return data;
    }
    public void readTimeout(int milliseconds)throws SocketException {
        if(milliseconds<0||milliseconds>30000)throw new IllegalArgumentException("Invalid read timeout");
        socket.setSoTimeout(milliseconds);
    }
    public void close()throws IOException {socket.close();}
}
