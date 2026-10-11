# Long-message presentation — dev.76

Windows presentation correction following the separate dev.74 History Sync
checkpoint. Dev.73 and dev.74 artifacts are preserved. Dev.75 was an internal
build/test iteration, not a published or physically accepted candidate.

## UI and bounds

- Ordinary chat text over 1,024 characters uses the same read-only text surface
  as snippet/file previews. Actual content replaces the obsolete copy-to-read
  placeholder. Short messages retain safe text/Hyperlink rendering.
- Inline text has a vertical scrollbar and a 160-pixel maximum height. Expand
  raises that limit to 320 pixels; Show less restores it. Open full uses the
  existing safe read-only window, never a shell/script/editor execution path.
- Copy uses the exact complete backing text, not a substring or normalized
  display value. Prose uses Segoe UI; conservative code-like detection or an
  explicit file preview uses Consolas.
- Protected generated text retains its existing 512-KiB UTF-8 acceptance bound.
  Full reads reuse the existing bounded two-reader gate, and at most 24 completed
  attachment preview cards retain their text. Ordinary safe full-file reads keep
  the existing 1-MiB limit. The conversation never receives an unlimited-height
  TextBlock for long content.

## Presentation identity and persistence

New generated typed text uses `message.peerchat-text` in the existing authenticated
attachment-name field. This is a presentation marker only, not permission, sender
identity, executable-type approval, or a new protocol field. The normal UI shows
`Long message`, not a filename or `File attachment`. Real .txt/.c/.java files
remain attachments. Transfer acceptance and delivery behavior are unchanged.

Protected source bytes, DPAPI domain, logical IDs, hashes, archive attestation,
history storage, ACK/dedupe and DeliveryManager behavior are unchanged. Resume
tokens preserve their exact stored name. Both old `message.txt` and new generated
names reopen; older pending offers are not renamed underneath their metadata.
Reload/archive projection derives presentation from the same retained metadata.

An old received dev.73 `message.txt` offer cannot safely be distinguished from an
actual user-attached message.txt file. Those received historical file records keep
their file presentation; no filename-only legacy migration guesses are made.
Their full text remains readable through the safe viewer. Old local protected
snippets and ordinary long-text history use the Long message presentation.

Missing/not-yet-downloaded attachment bytes remain honestly unavailable, using
the existing Download/restore flow. This change does not fabricate local contents
or introduce auto-download/storage/security policy changes.

## Validation

Focused WPF checks cover exact prose/Unicode/CRLF, code typography, real vertical
scrolling, bounded expansion, complete copying, read-only Open full, stale preview
rejection, archive/reload presentation, and a page of 30 long messages. Native
tests cover protected-source restart and legacy-token compatibility, DPAPI
tampering, exact Unicode transfer, reconnect and message ACK coexistence. Existing
archive attachment catalog/restart/security checks are retained.

Full Windows build/import/package audit and matching Android JVM/debug/test/
release/lint/ARM64 audits are required, followed by development version/updater
audit and diff check. Android UI is unchanged; these artifacts carry the unified
version without claiming Android long-message UI parity.

Physical checks pending: send/read long prose and pasted code, scroll/expand/copy/
Open full, restart both clients and verify exact content, confirm genuine text/code
files still appear as files. Archive restore should retain the generated marker
and original exact bytes; no archive semantic change is introduced here.
