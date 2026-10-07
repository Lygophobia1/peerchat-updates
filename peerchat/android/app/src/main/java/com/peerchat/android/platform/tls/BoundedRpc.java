package com.peerchat.android.platform.tls;

import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.LongSupplier;
import java.util.function.Predicate;

/** One profile's serialized request resource. No retry, sessions or delivery state. */
public final class BoundedRpc<T extends AutoCloseable> implements AutoCloseable {
    public interface Factory<T> {T open() throws Exception;}
    public interface Operation<T,R> {R run(T channel) throws Exception;}
    private final ReentrantLock gate=new ReentrantLock();
    private final Factory<T> factory;
    private final Predicate<T> available;
    private final LongSupplier clock;
    private T resource;
    private String binding="";
    private long opened,used;
    private int count;
    public BoundedRpc(Factory<T> factory,Predicate<T> available){this(factory,available,System::nanoTime);}
    public BoundedRpc(Factory<T> factory,Predicate<T> available,LongSupplier clock){
        this.factory=factory;this.available=available;this.clock=clock;
    }
    private void retire(){if(resource!=null)try{resource.close();}catch(Exception ignored){}resource=null;binding="";}
    public <R> R run(String identity,Operation<T,R> operation)throws Exception {
        if(!gate.tryLock(10,TimeUnit.SECONDS))throw new java.net.SocketTimeoutException("RPC serialization deadline");
        try{
            long now=clock.getAsLong();
            if(resource!=null&&(!binding.equals(identity)||now-used>=10_000_000_000L||
                now-opened>=100_000_000_000L||count>=60||!available.test(resource)))retire();
            try{
                if(resource==null){resource=factory.open();binding=identity;opened=clock.getAsLong();count=0;}
                R result=operation.run(resource);used=clock.getAsLong();count++;return result;
            }catch(Exception error){retire();throw error;} // No ambiguous replay.
        }finally{gate.unlock();}
    }
    public void close(){gate.lock();try{retire();}finally{gate.unlock();}}
}
