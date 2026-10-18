package com.peerchat.voiceproof;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;
import java.io.File;

/** Exercises the existing genuine C++ receive proof in Android's native runtime. */
public final class ProofActivity extends Activity {
    private static native int runProof(String logPath);
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView status = new TextView(this);
        status.setText("Running genuine NetEq receive proof; no microphone or network.");
        setContentView(status);
        if (state != null) return; // Rotation must not start concurrent test engines.
        new Thread(() -> {
            try {
                System.loadLibrary("peerchat_receive_proof");
                int result = runProof(new File(getFilesDir(), "receive-proof.log").getAbsolutePath());
                String outcome = "Receive proof exit=" + result;
                Log.i("PeerChatReceiveProof", outcome);
                runOnUiThread(() -> status.setText(outcome));
            } catch (Throwable error) {
                Log.e("PeerChatReceiveProof", "Native receive proof failed", error);
                runOnUiThread(() -> status.setText("Native receive proof failed; inspect test logs."));
            }
        }, "receive-proof").start();
    }
}
