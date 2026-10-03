package com.peerchat.android.platform.tls;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Existing opaque server-relay envelope only, not a peer protocol/owner. */
public final class RelayFrames {
    public static final int MAXIMUM_PAYLOAD=65536;
    private final byte[] session;
    public RelayFrames(String value)throws IOException {
        if(value==null||!value.matches("[A-Za-z0-9_-]{43}"))throw new IOException("Invalid relay session binding");
        session=value.getBytes(StandardCharsets.US_ASCII);
    }
    public byte[] wrap(byte[] payload)throws IOException {
        if(payload==null||payload.length==0||payload.length>MAXIMUM_PAYLOAD)throw new IOException("Invalid relay payload size");
        byte[] frame=Arrays.copyOf(session,session.length+payload.length);
        System.arraycopy(payload,0,frame,session.length,payload.length);return frame;
    }
    public byte[] unwrap(byte[] frame)throws IOException {
        if(frame==null||frame.length<=session.length||frame.length>session.length+MAXIMUM_PAYLOAD)throw new IOException("Invalid relay envelope size");
        for(int i=0;i<session.length;i++)if(frame[i]!=session[i])throw new IOException("Relay session binding changed");
        return Arrays.copyOfRange(frame,session.length,frame.length);
    }
}
