# Android phone UI milestone

## Scope and isolation

Branch: `codex/android-ui-only`, based on Android port checkpoint `22b25ff`.
Worktree: `C:\Users\Livel\.codex\worktrees\android-ui-only\Create 2D Game\peerchat`.
The native prerequisite-extraction worktree and Windows product worktree are not
modified or merged. No identity, provider, storage, TLS, protocol, session,
delivery or media implementation is changed.

This is **implemented UI with transient local fixtures**, not Android messaging
interoperability. The entry activity never invokes NativeBridge or SecurityProof.
There is no network permission, no microphone permission, no Keystore call, no
account success simulation, and no app data/identity creation on launch.
The package ID remains `com.peerchat.android`; no second visible app ID was added.
No physical phone was required, installed to, cleared or uninstalled in this pass.

## Screens and navigation

```text
Welcome
  Sign in -> Login -> Server & trust draft (Back returns to Login)
  Create account -> explanation of the unconnected backend
  Explore UI demo -> Home
  Advanced -> Server & trust draft

Home: Chats | Calls | You
  Chats: DM | Groups + search -> Conversation -> Details -> Diagnostics
  Calls: room previews, recent/missed DM calls, incoming DM call preview
  You: demo profile, identity/device placeholders, appearance, advanced, diagnostics
```

Back returns from details to the same conversation, from conversation to the list,
from advanced/diagnostics to their invoking screen, and from other bottom tabs to
Chats. Login is outside the demo app shell. No desktop right pane or Discord-style
channel hierarchy is introduced.

Implemented presentation:

- Welcome, sign-in, show/hide password, loading and invalid-input states; explicit
  backend-unavailable, authentication-rejected and server-unreachable presentations.
  Error previews say **Preview only**. Normal sign-in can only return unavailable.
  Password is neither saved in instance state nor persisted or logged; it is cleared
  on submission and activity recreation. There is no token in UI state.
- Host/port draft, restore defaults, certificate/pin placeholders. No certificate
  bypass or trust-everything switch. Default host is blank rather than inventing a
  production endpoint; port defaults to the existing 7443 convention.
- DM/group lists with initials, presence, previews, unread badges, time, delivery
  sample indicators, muted state, search, ready/loading/empty/error and offline/
  reconnecting states. Lists and message history use LazyColumn with stable UI IDs.
- Phone chat with incoming/outgoing bubbles, date separators, group sender names
  and avatars, sent/delivered/read/failed sample states, local retry preview,
  Unicode/long text, selectable text and HTTPS link styling/actions.
- Multiline composer, local preview submission, sending affordance, disabled
  offline/reconnecting state and attachment placeholder. New local previews are
  labeled **Local preview · not sent**, never delivered. Older-history loading
  only prepends an explicit synthetic fixture. No outbox/ACK/dedupe is implemented.
- Separate scrollable details screen: identity/trust placeholders, group members,
  per-member audio visual controls, notifications, shared-file placeholder,
  connection-state previews, diagnostics and explicit non-operative remove/leave
  confirmation. No raw networking diagnostics clutter the conversation.
- Multiple voluntary voice-room fixtures: join/leave, member counts, one-person
  room, mute/deafen preview and compact joined-state composer bar. These mutate
  fixture presentation only; no media session or device is created.
- Share-room fixtures: sharer/viewer information, Watch/Stop Watching UI state,
  and compact watching bar. No video is displayed or captured. Voice membership
  and watching state remain independent.
- Useful Calls and You shells, session-local System/Dark/Light choice, build
  information and account/device placeholders. Recent/missed call samples are
  explicitly DM calls, not ringing group rooms.

## State and backend boundary

`ui/UiState.kt` defines immutable presentation records, routes and typed actions.
`PeerChatPresenter` is the screen-facing boundary: a StateFlow snapshot plus
high-level actions and sign-in. `PeerChatUiModel` is its demo ViewModel; a pure
`reduceUi` function implements fixture changes. It is not a Kotlin messaging engine.
`DemoConversations` implements `ConversationSource` for local initial fixtures.
`AccountAccess` returns typed LoginResult values; `UnconnectedAccountAccess`
**never** produces Authenticated. A future account adapter may provide a real,
backend-authenticated AccountUi; that record does not carry credentials or tokens.
Exceptions are mapped to bounded local errors without printing exception text.

Composables render snapshots and emit actions. Local focus, draft text, menus,
password visibility and a brief preview-sending animation are UI concerns only.
There are no network calls or native lifecycle decisions in composables.
ViewModel retains navigation across activity recreation; composer draft uses
rememberSaveable, while password deliberately does not. All fixture data and
settings reset on a full process restart. This is not production persistence.

The username is presentation. Stable conversation/member/message IDs select UI
records; display names never become routing, identity or database keys. Actual
sender/trust/membership verification will remain below this projection boundary.
UI draft bounds are presentation/resource guards, not new PeerChat protocol or
cryptographic validation rules. The real native boundary must enforce its own
existing limits and return authoritative results.

### Integration map (not implemented here)

| UI surface | Future adapter/source of truth |
|---|---|
| AccountAccess / login states | Authenticated discovery/account application service; native identity/profile binding underneath |
| PeerChatPresenter conversation snapshots | Existing native Storage/history and DeliveryManager; send actions delegate to existing native outbox/delivery |
| Presence and connection labels | Authoritative account/presence/application snapshots and SessionTransportManager; never inferred from IP/routes |
| Group sender/member names | Authenticated current group roster projection, after native message authentication |
| Identity/trust/device information | Opaque native profile/provider + verified device state; no private key passed to UI |
| Room actions and participant snapshots | Future group media application service, not TURN/RTP/NetEq/PeerConnection types |
| Diagnostics | Immutable native/application snapshots; formatting/presentation only |

Do not implement extra repositories/owners merely to duplicate native truth.
A real presenter may subscribe to existing application/native events and replace
the fixture implementation without changing the Compose screen contract. Replace
demo-only strings and capability placeholders as those features become available;
do not remove their honest labeling while still running mocks.

## Build and test commands

From the UI worktree's `peerchat` directory:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-22'
$env:ANDROID_HOME = 'C:\Users\Livel\AppData\Local\Android\Sdk'
.\android\gradlew.bat -p android :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug --console=plain
git diff --check
```

Default `peerchatUiOnly=true` excludes the PeerChat native engine from the APK and
does not build it. The inherited ARM64 foundation can be explicitly included with
`-PpeerchatUiOnly=false` or `android/build.ps1 -FoundationProof`; neither is needed
for UI validation. `android/build.ps1 -ConnectedTests` defaults to **only**
`com.peerchat.android.ui.UiSmokeTest`, not the inherited production-provider tests.
Do not run an unfiltered connected test suite against the default UI-only APK.

Artifacts:

- `android/app/build/outputs/apk/debug/app-debug.apk` — Android UI preview.1
- `android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`
- `android/app/build/reports/tests/testDebugUnitTest/index.html`
- `android/app/build/reports/lint-results-debug.html`
- `android/app/build/reports/ui-previews/*.png`

Validated APK SHA-256:

- App: `CDC0DDDFC674289AB06C235B750151D6A9641E5245F28FE309FBE4E4FDB717D6`
- Test: `525B45F235763FDEA82BC83B99DDD2225D6B185C1F78E75A4111FA3059922D4D`

Representative generated host renders (available after running the tests):

![Dark chats](../android/app/build/reports/ui-previews/dark-chats.png)
![Direct conversation](../android/app/build/reports/ui-previews/dark-dm.png)
![Group voice/watch preview](../android/app/build/reports/ui-previews/dark-voice-and-watch.png)
![Light chats](../android/app/build/reports/ui-previews/light-chats.png)

If a suitable emulator/device is later available, use replacement installs only
with the same package. The UI suite itself does not initialize providers:

```powershell
$adb = "$env:ANDROID_HOME\platform-tools\adb.exe"
# Select the intended device explicitly; do not install to every attached device.
$serial = '<selected-device-or-emulator>'
& $adb -s $serial install -r android/app/build/outputs/apk/debug/app-debug.apk
& $adb -s $serial install -r android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
& $adb -s $serial shell am instrument -w -r -e class com.peerchat.android.ui.UiSmokeTest com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
```

No uninstall, data wipe, alias generation or tamper-storage tests are part of UI
validation. Existing normal device identity remains untouched.

## Host validation — 2026-10-02

- Debug APK and instrumentation APK: built successfully with the existing pinned
  AGP/Kotlin/Compose tooling; no toolchain or protocol upgrade.
- 25 tests pass: 11 reducer/state tests, 3 ViewModel tests, 6 shared Compose flow
  tests, 3 rendering/layout tests, 2 actual Activity recreation tests.
- Host Compose runs under Robolectric 4.15.1/API 34 with native Android graphics.
  The same six flow assertions compile into UiSmokeTest for later device use.
  This is host Android simulation, **not** physical ART/IME/native validation.
- Covers login validation/loading/unavailability and bounded exception handling,
  honest failure previews, navigation/back, DM/group search/load states, offline
  composer, Unicode/local submission, history loading isolation, duplicate names
  with distinct IDs, 500 conversations, 300 messages with date separators,
  independent voice/watch states, one-person room, member-audio bounds, appearance,
  draft survival across recreation and password non-retention.
- 32 actual rendered PNGs: 15 important states in both themes, compact 320×640
  long-name/multiline-composer layout and 800×360 landscape. Screenshot tests
  reject blank output. Reviewed representative welcome/login, chat/list, group
  details, voice/watch and theme images. These are generated host renders, not
  screenshots fabricated from mock HTML or AI images.
- Preview annotations also cover welcome/login, DM/groups, direct/group chat,
  details, room states, Calls/You, offline and compact/landscape groundwork.
- Lint: 0 errors. Nine warnings remain: newer pinned dependency suggestions,
  inherited ARM64-only ChromeOS coverage, and inherited backup-rule declaration.
  No provider/backup behavior was changed to hide those warnings.
- APK debug signature verifies. APK has no PeerChat native libraries and no
  Internet, microphone, camera or media permissions; only AndroidX's app-local
  dynamic-receiver protection permission is merged.
- Existing API-37 emulator could not boot because the host hypervisor driver is
  absent. No driver/system change or emulator setup was attempted; host Compose
  validation supplied the UI evidence. No physical phone was required.
- Windows/native regressions are not rerun: no Windows or native/provider source
  changed. No real Android identity, messaging, TLS or media result is claimed.

## Remaining work and merge considerations

Phone-specific checks still needed: actual soft keyboard/insets, selection menu,
external link handling, accessibility/TalkBack, touch interaction, cutouts/gesture
navigation, font-scale extremes and OEM theme behavior. Host rendering is not a
claim of physical quality acceptance. No full tablet/master-detail redesign is
included; layouts remain a scrollable single-pane phone shell.

UI-only limitations: local samples reset on process restart; account creation,
files, media, remove/leave and trust actions are placeholders. These require real
application/native adapters, not more fake protocol logic. Broader screenshot
golden baselines and accessibility automation remain optional follow-up UI work.

Native extraction blocks real profile/identity/storage-backed UI state; subsequent
TLS/discovery/account/server integration blocks login, contacts/presence, real
delivery/history and group roster interoperability. Media is later and out of scope.

Merge is **not performed**. Expected integration edits are MainActivity and the
Android Gradle/build-script default UI/native selection. Preserve the native
branch's CMake/JNI/provider changes; wire the real presenter only after native
profile and transport milestones are validated. Do not replace native ownership
with reduceUi, persist fixtures into a real profile, or copy provider secrets to UI.
The shared/native extraction's uncommitted work remains in its original worktree.

Recommended next step: finish the opaque native identity/profile/storage milestone
on its branch, then integrate an application presenter deliberately. This UI branch
can be visually reviewed and its host tests rerun independently in the meantime.
