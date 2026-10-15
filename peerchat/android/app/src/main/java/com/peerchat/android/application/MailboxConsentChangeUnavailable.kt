package com.peerchat.android.application

/** Never save an OFF preference while the recovered server proof remains ON. */
class MailboxConsentChangeUnavailable(val recovered:Boolean):Exception(if(recovered)
    "Storage consent remains ON. Disabling storage for a recovered enrollment requires a separate consent update; this repair preserves queued ciphertext instead of revoking or rotating keys."
    else "Connect to the authenticated server before changing mailbox consent. The existing preference is unchanged.")
internal fun requireMailboxConsentChangeAllowed(scopedStatus:Int){
    if(scopedStatus!=0)throw MailboxConsentChangeUnavailable(scopedStatus>0)
}
