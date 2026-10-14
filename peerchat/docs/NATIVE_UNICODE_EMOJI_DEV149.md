# Native Unicode Emoji — isolated dev.149

Base: validated dev.148 `f444265607196fb045d311baa44da1e230817636`.
Branch: `codex/native-unicode-emoji`. This branch is not integrated or deployed.

## Implementation

- `desktop/EmojiCatalog.cs`: small original English catalog of Unicode strings, names, keywords and eight categories. Common skin-tone variants use complete Unicode sequences. No external catalog, emoji artwork, fonts, packages or proprietary assets were imported; no new third-party licensing dependency. This is a curated subset, not an exhaustive/localized CLDR catalog. The operating system supplies native glyphs.
- `desktop/EmojiPicker.cs`: searchable resizable modal picker, category/recent/frequent filters, tone selection, named accessible controls, keyboard focus, Escape cancellation and native buttons. Recent/frequent selections are local to the profile, bounded, atomically replaced and DPAPI-protected in optional `emoji-usage.v1`. Corruption or missing preferences cannot prevent messaging. No database schema changes or live statistics writes.
- `EmojiComposer`: inserts through TextBox.SelectedText inside one undoable change, preserves the captured caret/selection, and restores composer focus. No resetting of text/undo stacks. Profile/conversation changes during the dialog invalidate insertion. Emoji-specific text-element boundaries extend legacy StringInfo for ZWJ, variation selectors, paired regional flags, skin modifiers and tag sequences. Backspace/Delete remove whole sequences; standard Ctrl/Alt editing remains native.
- `desktop/Main.xaml`: slimmer 42-DIP empty composer; growth capped at the existing 150-DIP bound. Composer-only template avoids double padding, centers caret/hint and keeps attachment/emoji/send aligned. Existing Enter, Shift+Enter, draft and send handlers are retained. The emoji button has the same eligibility as the composer, for both DM and groups.
- Existing message and bounded text viewers use native font fallback. Messages remain ordinary Unicode content; nothing is normalized, stripped, replaced or rewritten in storage.
- `build.ps1` includes the two small new UI sources; isolated version is 4.0.2-dev.149.

## Transport and Android audit

Existing Windows composition/group/DM/history paths preserve strings. Shared `portable/utf8.hpp` strictly transports Unicode scalars and rejects malformed UTF-8/surrogates. Android composition uses StandardCharsets/Charsets.UTF_8 byte arrays and JNI byte-array APIs, not Modified UTF-8 for messages. Both short-message and long-text transfer paths retain existing byte limits and integrity/security validation. Android keyboard, JNI implementation, protocol, identities, history schemas, server and cryptography are unchanged.

## Validation (isolated fixtures only)

- Full Client/Updater/ScreenMedia build; final Desktop rebuild and package dependency audit: PASS. Existing compiler warnings remain (VersionInfo duplication, unused fields, unawaited operations); no new compilation errors.
- `tests/test-emoji.ps1`: loaded WPF controls, catalog search/tone/filtering, keyboard search-to-result navigation, Escape/cancellation, insertion/selection/caret, whole-cluster Backspace/Delete, undo/redo, recents/frequency, DPAPI profile isolation, bounded/corrupt preferences, compact empty/multiline layout, native render screenshots and long text viewer: PASS.
- Packaged CoreAdapter + real native mutual peer TLS: Unicode DM sends in both directions, route interruption/reconnect, exact protected history after worker restart, stable identities and distinct durable message IDs: PASS. Relay bytes are forwarded locally in memory; not a Pi/network/Android throughput or physical acceptance test.
- Group envelope tests with Unicode/ZWJ/modifier/flag/selector/multiline fixture, normal Windows CNG and portable crypto: PASS. Existing signature, tampering, replay, revocation/rotation and protected-history regressions retained.
- Portable strict UTF-8 vectors + Android-equivalent JVM UTF-8 vectors: PASS. JVM coverage is not physical Android/JNI/UI acceptance.
- Existing network-presence/composer/mailbox lifecycle suite: PASS.
- Existing media-performance/Connections/transfer/DM/group/unified diagnostics suite: PASS, including packaged 10-MiB transfer integrity tests. No optimization claim made.
- Existing archive application boundary: PASS (25 committed imports, three unresolved rows preserved, protected attachment retrieval and worker restart/account/session regressions).
- Native package dependency/export/load audit: PASS, restricted package/system DLL search, ScreenMedia and HPKE included.

## Known rendering limits and scope

On the test host, this .NET Framework WPF/native-font path renders monochrome emoji. Some newer/combined ZWJ emoji and flags render as generic/component glyphs even though exact sequences survive insertion, encryption and restart. Color rendering and identical appearance across Windows/Android are NOT promised. Labels/tooltips remain available. Native Android rendering continues to depend on its existing OS keyboard/font support.

The catalog does not replace arbitrary Unicode keyboard/paste input. These editing helpers are not a universal grapheme/text-processing engine. No reactions, GIFs, artwork, custom uploads, clipboard-image features or Android keyboard duplication were added.

## Manual acceptance checklist

Extract the standalone emoji ZIP to a separate folder. Do not replace the dev.148 physical-test folder. Do not run two clients against the same profile simultaneously.

1. In DM and a group, check the compact empty composer, hint/caret alignment, multiline growth, Enter send and Shift+Enter newline.
2. Place the caret in the middle of text; select text; pick an emoji in each case. Check focus, insertion, selection replacement, undo/redo and ordinary typing.
3. Search names/keywords; switch categories, Recent/Frequent and tones. Check Tab/arrows/Enter and Escape/cancel, scrolling, resizing and Windows display scaling.
4. Send/receive common faces, skin-tone hands, family/profession ZWJ sequences, flags, keycaps, hearts, mixed scripts and multiline text in DM/groups. Verify content after reconnect/restart and in long-text views.
5. Optionally exchange the same sequences with the existing Android build using its stock keyboard. Record glyph-quality differences separately from content corruption.
6. Restart and switch profiles: recents restore only in the proper profile; drafts/history remain intact. Confirm attachment/send controls still work.

Physical emoji/composer acceptance: PENDING. Stop here; no merge/deploy or further roadmap work without authorization.
