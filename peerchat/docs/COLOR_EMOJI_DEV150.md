# Color Emoji, Multi-select and Favorites — isolated dev.150

Branch: `codex/native-unicode-emoji`, descended from dev.148 `f444265` and
the isolated dev.149 emoji checkpoint `5b79ce1`. No integration or deployment.

## Delivered

- Color picker and display-only Unicode-to-artwork projection for the existing
  DM/group message renderer, including received and historical messages. Hyperlinks,
  original text, copy source, encrypted messages and stored history remain unchanged.
- 3,145 Microsoft Fluent Flat assets, 259 regional flags and one Noto family fallback,
  offline in one embedded compressed archive. PNGs are decoded only as needed;
  128 decoded images are held in the hot cache. Weak references reuse images still
  held by visible controls without pinning the rest of the collection. The picker
  remains the small curated catalog rather than creating thousands of image controls.
- The picker stays open after every selection. Each selection inserts at the evolving
  native caret, initially replacing selected text. No search/category/scroll reset,
  send action, draft replacement or undo-stack clearing. Explicit ×, Escape and window
  deactivation/outside-click dismissal; profile/conversation generation guards remain.
- Separate Favorites category immediately after All. Each tile has an accessible
  add/remove star. Toggling never inserts or affects Recently Used/frequency. It preserves
  search/category/caret and scroll (subject to normal clamping if the last row disappears).
  Exact selected skin-tone variants are independent favorites.
- Favorites extend the same optional profile-bound DPAPI preference sidecar. Old
  recent rows remain readable; no message, journal or database schema changes. No
  preference failure can block messaging. Favorites persist across restart, but an
  older emoji-feature client rewriting that optional sidecar does not preserve new
  favorite rows; use this build for the feature test.
- Native editable TextBox retained: focus, selection, undo/redo, draft callbacks,
  Enter/Shift+Enter and size validation stay with the existing editor. A bounded
  live color preview strip (up to 24 emojis from the first 2,048 draft characters)
  gives color feedback while typing. It is hidden for ordinary/empty drafts.
- Long prose bubbles have a bounded color presentation (up to 4,096 characters and
  256 artwork inlines per projection), plus the unchanged exact selectable-text mode,
  Copy and full read-only text actions. No source text is truncated. Code stays literal.

## Explicit limitations

The editable text itself still uses WPF's native monochrome rendering; the strip
is a preview, not a colored editable overlay. A RichTextBox/WebView/native editor
replacement would change caret, selection, undo and draft architecture, so was not
introduced. The existing full/selectable text/code editor also retains native fonts.
Color is provided in picker, conversation display and bounded prose preview.
Unsupported/new sequences fall back to exact native Unicode; 3,405 assets are not
a promise of coverage for every Unicode version or arbitrary nonstandard ZWJ sequence.
Very emoji-dense projections retain native text beyond the inline-image bound.

## Licensing and reproducibility

See `desktop/Assets/Emoji/NOTICE.md` and the four complete LICENSE files, also copied
into packaged `Client/licenses`. Microsoft assets are MIT licensed:
https://github.com/microsoft/fluentui-emoji/blob/1ffb34c752ecf5d402f04cfb4b392c77f57c54bc/LICENSE

Regional flags retain the upstream public-domain/exemption copying notice:
https://github.com/googlefonts/noto-emoji/blob/e20cbc2bbec1926686be9f9bee7d1d2cfa1fea0e/third_party/region-flags/LICENSE

The family fallback is Apache-2.0, with Google copyright notice and full terms:
https://github.com/googlefonts/noto-emoji/blob/e8073ab740292f8d5f19b5de144087ac58044d06/svg/LICENSE

`scripts/build-emoji-artwork.cjs` requires these pinned source checkouts and build-only
sharp. The older color family SVG is hash-pinned separately (current Noto uses a
monochrome family pictogram). It verifies Git revisions and that source hash,
rasterizes trusted SVGs to 64-pixel transparent PNGs,
resolves only restricted local country-flag aliases, and emits a provenance manifest.
No SVG interpreter, Node/sharp, fonts, CDN or artwork download runs in production.

## Validation

- Full Windows Client/Updater/ScreenMedia build and final Desktop rebuild: PASS.
- Extended `tests/test-emoji.ps1`: PASS. Every catalog/tone resolves to frozen color
  artwork; actual non-gray pixels verified; cache cap; 100 consecutive complex emoji
  insertions; undo/redo; favorites star/unstar, independence, restart, profile binding,
  search/category/scroll preservation; keyboard/Escape and outside dismissal; exact
  accessible/rendered text identity; bounded long/draft previews; empty composer 42 DIP.
- 200 warm in-memory draft projections of 512 ordinary characters plus three emojis
  took 20 ms in one host run. This measures projection code only, NOT physical typing
  or end-to-end rendering latency; no network performance claim.
- Real packaged native mutual-TLS DM round trips both ways, route interruption,
  restart, exact protected Unicode history and deduplication: PASS.
- Portable strict UTF-8 and Android-equivalent JVM vectors: PASS. Android unchanged;
  no phone access or physical Android acceptance claimed.
- Existing presence/composer/mailbox lifecycle regressions: PASS.
- Existing media/transfer/DM/group/unified diagnostics and native package audit: PASS.
  The packaged 10-MiB integrity fixtures and live/read-only diagnostics regressions
  passed; previously validated encryption/history code is unchanged.
- Existing dev.149 CNG and portable group-envelope/Unicode/history tests remain
  applicable: no cryptographic, group protocol or history implementation changes here.

## Manual acceptance

Extract this standalone ZIP separately from dev.148; do not run two clients against
the same profile concurrently.

1. Inspect color picker/chat at normal and scaled Windows display sizes; search,
   categories, tone variants, flags and family; mixed scripts and multiline content.
2. Select draft text, then click several emojis without reopening. Check evolving
   caret, unchanged category/search/scroll, undo/redo, no accidental send and outside/×/Escape close.
3. Star/unstar without insertion; filter/search Favorites; verify Recent is unchanged
   by stars. Restart and switch profiles to check persistence/isolation.
4. Send/receive in DM/groups, reload history and restart. Check Copy returns exact
   Unicode, long-message color/text modes, full view and existing attachment controls.
5. Verify native editor + live color preview while typing, Enter send, Shift+Enter,
   draft switching and ordinary text. Optionally interoperate with Android's native keyboard.

Physical color-emoji acceptance PENDING. Stop for review; no merge/deploy or other milestone.
