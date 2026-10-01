# Screen video reference continuity

`H264RtpReceiver` is a per-media-generation parser used by `ScreenShareMedia`.
It owns no transport, worker, logical share, or recovery generation. The existing
RTP callback and media poll serialize it under the existing media mutex.

Packets wait at most 20 ms or 32 sequence positions. Modular sequence distances
are used both to order packets and to select the next packet after loss, including
65535 -> 0 wrap. A late packet cannot revive an abandoned timestamp or move a
newer access unit back to an older timestamp. RTP payload type and SSRC are scoped
to the selected track/media generation.

Missing packets, timestamp changes before marker, invalid FU-A transitions,
unfinished FU-A at marker, and malformed aggregation invalidate the entire AU.
The uncertain timestamp at the far side of a sequence gap is also discarded;
RTP has no general AU-start flag that could prove its prefix was received.
No known-incomplete AU is submitted to Media Foundation.

After discontinuity, delta pictures are withheld until a complete IDR and valid
SPS/PPS are available. Only complete AUs may update the parameter-set cache. The
IDR resets H.264 reference pictures; recovery does not flush the MFT from the RTP
thread or recreate the decoder, ICE, TURN, or logical share. A refused AU at the
bounded decoder queue also requires an IDR. The existing media poll requests an
RTCP PLI at most every 500 ms while awaiting recovery. The sender's PLI callback
only flags work for that same poll.

An encoded AU dropped before packetization produces no RTP sequence gap. The
adapter attaches an encoded-output ordinal before its existing bounded queue
policy. The media sender observes ordinal gaps and withholds dependent AUs until
the next bootstrap. Encoder selection, controls, bitrate and capture are unchanged.
This metadata addition requires matching rebuilt Core/ScreenMedia binaries.

Diagnostics distinguish reordered/recovered packets, true missing sequence
positions, late packets, incomplete FU-A/AUs, prevented decoder submissions,
queue refusal, recovery requests, and admitted clean points. Recovery completion
requires an actual MF output timestamp at or beyond the admitted recovery IDR;
admission alone is not reported as decoded recovery. Presented-frame counters
remain the authoritative indication of display progress.

Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\test-h264-rtp.ps1`.
It exercises deterministic reorder/loss/FU-A/wrap/timestamp/queue/reference cases,
then the pinned libdatachannel packetizer, receiving session and PLI handler.
No screen capture, visual observation, network peer or third-party rebuild is
needed. Physical validation should move windows rapidly and verify corruption
clears after recovery while decoding/presentation continue. Sustained packet loss
can still cause visible pauses while waiting for a clean IDR; this gate trades
damaged reference pictures for bounded recovery, not loss concealment.
