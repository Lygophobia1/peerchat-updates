package com.peerchat.android.application

/** UI lease churn must not discard an acknowledged cache append. Revocation of
 * transfer execution still fences every borrowed response, even after resume.
 * Account/session/profile/generation checks remain with the backend owner. */
internal class TransferWorkLifetime {
    @Volatile var revision=0L
        private set
    @Synchronized fun update(allowed:Boolean) { if(!allowed)revision++ }
}
