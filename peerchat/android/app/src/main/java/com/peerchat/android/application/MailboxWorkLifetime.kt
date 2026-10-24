package com.peerchat.android.application

/** Mailbox replies belong to continuous active use, not to an individual UI,
 * preparation, or service lease. A stop revokes borrowed work even if use
 * resumes before its response arrives. Session/profile fences stay external. */
internal class MailboxWorkLifetime {
    @Volatile var revision=0L
        private set
    @Synchronized fun update(inUse:Boolean){if(!inUse)revision++}
}

/** Do not consume the mailbox deadline until its lane has actually started.
 * Once borrowed, always enter the script so its finally owns cleanup. */
internal fun runMailboxBorrow(scope:ScopedBackendWork,borrow:()->(()->Unit)?) {
    val work=scope.local(borrow)
    work?.invoke()
}