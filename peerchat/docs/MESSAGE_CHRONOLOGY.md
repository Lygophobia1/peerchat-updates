# Message chronology

DM and authenticated group history display ascending original message timestamp,
then sender device ID, then message ID. Logical clocks, authenticated timestamps,
message IDs, delivery state and local database cursors retain their original values.
Messages with incorrect sender clocks therefore still have incorrect wall times;
chronological presentation cannot infer or repair those clocks.

Storage selects the newest chronological page before returning it. Sorting only
the last arrival page would omit recent messages when old archive/mailbox messages
arrive late. Existing rowid/sequence tokens remain the native ABI: a token names
the earliest displayed message's ordering key, rather than a numeric arrival bound.
DM anchors can name another device peer in the same local profile so an account
conversation can merge bounded pages across its known device histories.

Authenticated ordering keys are cached only in process memory. A cold conversation
read decrypts its protected records to derive keys; no plaintext timestamps or
message bodies are added to database indexes. The cache retains at most eight
conversations and 32768 keys in total, evicting older entries. A watermark/count
query detects inserts and erasure, including writes through another connection.
Warm reads decrypt only the selected page's payloads. A conversation larger than
the key budget uses a full protected-record scan with bounded page memory on each
request. First reads, cache eviction and very large histories can therefore cost
more time than subsequent reads of cached conversations.

Windows collections, conversation previews and older-page anchors use the same
timestamp/sender/ID order. Android native snapshot merging, group history,
presenter previews, older-page anchors and the combined text/file timeline use
the same order. File cards retain their durable creation timestamps and stable
IDs; progress updates do not move them. Public attachment references without an
authenticated original timestamp continue to report that the time is unavailable.

Archive synchronization watermarks and transport/outbox/mailbox traversal cursors
remain monotonic local progress positions. They are intentionally independent of
chronological display and never become recipient acknowledgements.

Focused checks live in `tests/message_chronology_storage_test.cpp`,
`tests/message_chronology_projection_test.cs`, and the Android
`NativeProjectionTest`, `ConversationTimelineTest`, `GroupHistoryProjectionTest`
fixtures. The storage fixture covers cold/warm history, late arrivals, equal-time
ties, external inserts, duplicate live/history records, restart, cross-device
anchors and protected transfer preference CAS. It does not replace authenticated
transport, archive admission, Android device, or rendered deployment tests.
