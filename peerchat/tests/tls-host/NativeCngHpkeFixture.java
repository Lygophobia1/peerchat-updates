final class NativeCngHpkeFixture {
    static { System.loadLibrary("recipient-key-proof"); }
    static native long create(String testAlias);
    static native byte[] publicKey(long handle);
    static native byte[] agree(long handle,byte[] validatedPublicPoint);
    static native void destroy(long handle);
}
