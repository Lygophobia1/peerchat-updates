package com.peerchat.android.platform.tls;

import java.security.Provider;
import java.security.Security;
import java.util.Arrays;
import javax.net.ssl.SSLContext;
import org.bouncycastle.jsse.provider.BouncyCastleJsseProvider;
import org.bouncycastle.tls.crypto.impl.jcajce.JcaTlsCryptoProvider;
import org.junit.Test;
import static org.junit.Assert.*;

public class ServerChannelCacheProviderTest {
    @Test public void cacheCryptoUsesPrivateBcForAllAlgorithms()throws Exception {
        Provider globalBc=Security.getProvider("BC"),globalJsse=Security.getProvider("BCJSSE");
        JcaTlsCryptoProvider algorithms=ServerChannel.cacheCryptoAlgorithms();
        assertNotNull(algorithms.getHelper());assertNull(algorithms.getAltHelper());
        assertEquals("ProviderJcaJceHelper",algorithms.getHelper().getClass().getSimpleName());
        assertEquals("BC",algorithms.getHelper().createCipher("AES/GCM/NoPadding").getProvider().getName());
        BouncyCastleJsseProvider cache=ServerChannel.cacheCryptoProvider();
        SSLContext context=SSLContext.getInstance("TLS",cache);context.init(null,null,new java.security.SecureRandom());
        assertTrue(Arrays.asList(context.getSupportedSSLParameters().getProtocols()).contains("TLSv1.2"));
        assertTrue(Arrays.asList(context.getSupportedSSLParameters().getProtocols()).contains("TLSv1.3"));
        assertSame(globalBc,Security.getProvider("BC"));assertSame(globalJsse,Security.getProvider("BCJSSE"));
    }
    @Test public void cacheAndOrdinaryProviderRegistriesRemainSeparateAndScoped()throws Exception {
        ServerChannel.OperationScope scope=new ServerChannel.OperationScope();
        java.lang.reflect.Method ordinary=ServerChannel.OperationScope.class.getDeclaredMethod("tlsProvider");
        java.lang.reflect.Method cache=ServerChannel.OperationScope.class.getDeclaredMethod("cacheTlsProvider");
        ordinary.setAccessible(true);cache.setAccessible(true);
        Object cacheProvider=cache.invoke(scope);
        assertSame(cacheProvider,cache.invoke(scope));
        assertEquals(0,scope.providerInitializationCount());
        Object ordinaryProvider=ordinary.invoke(scope);
        assertSame(ordinaryProvider,ordinary.invoke(scope));assertNotSame(ordinaryProvider,cacheProvider);
        assertEquals(1,scope.providerInitializationCount());
    }
}
