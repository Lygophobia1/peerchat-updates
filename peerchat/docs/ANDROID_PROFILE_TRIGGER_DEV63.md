# Android profile startup trigger correction — dev.63

After dev.62 replacement installation, the real Motorola produced:

    stage=profile_open_started
    stage=profile_open_returned
    stage=local_startup_failed category=IllegalStateException

SQLiteLog provided the underlying bounded schema error:

    incomplete input in CREATE TRIGGER IF NOT EXISTS message_archive_insert
    AFTER INSERT ON messages BEGIN INSERT INTO message_archive_index(message_id)
    VALUES(NEW.id)

This is a local profile/storage opening failure **before TLS/authentication**.
The native Storage schema correctly includes `; END;` for each archive trigger.
AndroidStoragePlatform.execute split the fixed schema batch at every semicolon,
incorrectly executing the trigger header/body before its END. The JNI binding
failed closed; the presenter defect fixed in dev.62 had left Sign In busy after
that failure. No server, pin, identity or network defect is implied.

The dev.63 correction is confined to fixed native SQL statement grouping in the
Android adapter. It preserves the current single-statement trigger body with its
END as one execSQL operation. Ordinary schema statements, PRAGMA execution,
transactions and bound user values are unchanged. NUL, quoted literals/comments,
unfinished triggers and unsupported trigger bodies remain rejected before any
statement executes. This is deliberately not a general SQL parser or new schema.
No Windows/native Storage behavior or persisted records are replaced.

Focused Robolectric SQLite tests execute the real archive INSERT/DELETE trigger
syntax, repeat schema installation idempotently, exercise both triggers, and
reject malformed/incomplete statements and forbidden user literals. Existing
Android startup/trust/history/scheduler/UI suites and debug/test/release/lint,
ARM64/JNI/dependency and corresponding Windows/package/version audits remain the
build gates. The legacy JNI host fixture include-path limitation documented in
ANDROID_NETWORK_RECOVERY_DEV62.md is not silently marked passing.

Install with adb install -r only. Preserve the same identity, recipient registry,
Remember Me, protected server configuration and encrypted local history. Verify
physical profile-open now reaches identity/history projection and actual
authentication; Online still requires live authenticated presence. Stop for the
user's sign-in/reset/restart/FCM retest; automated tests alone are not acceptance.
