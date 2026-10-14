# Development 137 — diagnostics navigation styling

UI-only refinement of dev.136. The context dropdown is now a header-only primary TabControl for File Transfers, Direct Message Calls, Group Calls and History Sync. Window-local templates provide rounded controls, blue selection emphasis, hover/focus contrast, wrapping and larger click targets (primary 44, secondary 38 device-independent pixels). Secondary tabs keep their existing sections and content, with rounded pill styling instead of the default thin WPF tab borders.

The existing sample-state/status dropdown is unchanged, including its prior visibility rules. Sample providers, live group telemetry, bounded graph/event collection, pause, copy/export, contextual transfer identity, calls and History Sync are unchanged. No backend, Android, server, protocol or storage changes. Unrelated object files remain untracked and untouched.

Windows Desktop and Updater compiled as 4.0.2-dev.137. The unchanged native runtime binaries are reused exactly from the validated dev.136 candidate. Validation passed: native media/audio lifecycle regressions, all diagnostics sections/tabs/sample states, keyboard primary selection, secondary click-target sizing, minimum-window resizing and 125/150/200-percent layout-transform simulations; attachment, History Settings and group-surface regressions. Package containment validates crypto/media ABI and native dependency loading.

Versioned Screenshots/DirectMessageCalls.png and Screenshots/GroupCalls.png are rendered WPF UI fixtures, not physical-call performance evidence. DM values are samples. The group screenshot uses injected test snapshots to exercise the preserved live-group renderer, not a production mock provider.

Review both navigation levels, selection/hover/keyboard focus, the retained status dropdown, wrapping at smaller sizes, and unchanged scrolling/graphs/copy/export. Stop for visual acceptance before deeper telemetry integration.
