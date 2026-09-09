# PeerChat C++ GUI

Build the retained legacy GUI with `./scripts/build-gui.ps1` in PowerShell, then open
`build/legacy/PeerChat.exe`.
Requires MinGW-w64 g++ with C++17 support at build time. The executable uses Windows
Schannel, CNG, DPAPI, Winsock, and SQLite at runtime; no GUI framework is needed.

This GUI is now connected to the direct P2P text core. It starts a listener, lets you
connect to a hostname/IP and port, asks you to verify new persistent peer IDs, shows
connected contacts, sends Unicode text and URLs, displays delivery state, reconnects
saved contacts, and reloads encrypted local history after restart. The application
does not automatically trust a new identity.

## Learning the C++ code

- `StoredMessage` groups the globally unique ID, sender, timestamp, delivery state, and text.
- `Application` owns the window and delegates all peer work to `Node`.
- `std::vector<StoredMessage>` holds only the selected conversation in the UI.
- `std::wstring` owns Unicode text; `std::array` and standard containers manage collections.
- `Font` demonstrates RAII: its destructor releases a Windows font automatically.
  Copying is disabled so two objects cannot accidentally release the same handle.
- The static window callback routes Windows events to a `ChatApp` instance using
  `this`. This bridges a C-style API to C++ instance methods.

The drawing uses Windows GDI and native controls. Peer, TLS, protocol, and storage
code live in separate files so you can troubleshoot one layer at a time. Read
See `../ARCHITECTURE.md` for the full path from a button click to an encrypted record.

The microphone/voice experiments are intentionally not linked into this text-first
build. Opus voice begins after text has been tested on real PCs.
