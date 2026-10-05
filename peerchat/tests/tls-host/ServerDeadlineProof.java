package com.peerchat.android.platform.tls;

import java.net.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

/** Real blocking socket/TLS test, not an outer Future timeout masquerading as
 * cancellation. No credentials or normal profile records are used. */
public final class ServerDeadlineProof {
    public static void main(String[] args)throws Exception {
        ServerChannel.OperationScope scope=new ServerChannel.OperationScope();
        for(int cycle=0;cycle<3;cycle++)try(ServerSocket server=new ServerSocket(0,1,InetAddress.getLoopbackAddress())) {
            scope.configure(true,Long.MAX_VALUE);
            AtomicBoolean released=new AtomicBoolean();AtomicReference<Throwable> failure=new AtomicReference<>();
            Thread worker=new Thread(() -> {
                try(var channel=new ServerChannel("127.0.0.1",server.getLocalPort(),"0".repeat(64),false,scope)) {
                    throw new AssertionError("Unresponsive TLS peer must not complete handshake");
                } catch(Throwable error){failure.set(error);}finally{released.set(true);}
            },"deadline proof blocking TLS");
            worker.start();server.setSoTimeout(10000);
            try(Socket accepted=server.accept()) {
                // The socket is ACTUALLY in TLS read; leave the peer silent.
                long started=System.nanoTime();
                if(scope.activeSocketCount()!=1)throw new AssertionError("Socket was not registered for cancellation");
                scope.configure(true,started+TimeUnit.MILLISECONDS.toNanos(250));
                worker.join(2000);
                if(worker.isAlive()||!released.get()||scope.activeSocketCount()!=0||failure.get()==null)
                    throw new AssertionError("Deadline left an inner operation/resource alive");
                if(System.nanoTime()-started>TimeUnit.SECONDS.toNanos(2))throw new AssertionError("Deadline exceeded tolerance");
                if(scope.providerInitializationCount()!=1)throw new AssertionError("Provider registry rebuilt across the same backend lifetime");
                try{scope.check();throw new AssertionError("Expired scope accepted work");}catch(SocketTimeoutException expected){}
            } finally {scope.configure(false,Long.MAX_VALUE);worker.join(2000);}
        }
        scope.configure(true,Long.MAX_VALUE);scope.check();scope.configure(false,Long.MAX_VALUE);
        try{scope.check();throw new AssertionError("Released lifetime accepted work");}catch(SocketTimeoutException expected){}
        System.out.println("PASS: 3 silent-peer TLS reads interrupted by socket-close deadline; inner worker exited, resources released, expired scope fails closed, later lifetime reopens; exactly one scoped provider initialization. No outer-timeout-only claim.");
    }
}
