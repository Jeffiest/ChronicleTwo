# Native media contracts, 2026-10-11

The SOUND reader loads the bounded HD3 index and reads one indexed RIFF entry at a time.
It walks padded chunks, validates PCM format/extent/alignment and resolves case-insensitive
duplicate names to the first record. The PAL terminator names an empty string inside the index.
Synthetic missing/truncated/chunk-length and duplicate cases pass. The production reader also
decodes all 3,101 local PAL indexed names as mono 48 kHz PCM16 (481,470,146 samples, including
the duplicate name's first-match result twice).

`CSound` stream opens now load real PCM into two dedicated mixer buses, independent of the
sixteen MIDI ports. Playback state follows the render cursor and EOF; volume, pause/resume,
stop/rewind, close and level are routed to those buses. Two active GTests verify mixed output,
MIDI-bus independence, master/channel volume and transport behavior. These mixer tests and the
reader fixture are separate; there is no complete `CSound`/event-script integration fixture yet.
Audible playback and the missing Monica voice line require user-controlled acceptance.

Retail `CommandStreamOpenFromFPL` passes a VPK container name and a `.wav` member name;
the native bridge looks up that member in SOUND.HD3. Plain opens use their basename. Unsupported
names/formats fail closed and do not manufacture a completion timer. The full mapping of plain
`.WV` requests, stream flags and event waits still needs acceptance.

`video/pss.cpp` adds bounded parsing of complete program-stream/PES packets, selecting video
and private-stream payloads and extracting PTS/DTS plus sequence metadata. Partial trailing
packets remain unconsumed for the caller to carry forward. The synthetic fixture covers MPEG-2
pack/PES, sequence dimensions/progressive metadata, partial packets and invalid optional-header
bounds. Metadata scanning currently requires the sequence header within one PES payload;
split elementary-stream headers and actual PAL movie parsing are not accepted yet.

This demuxer is not connected to `movie.cpp`. The player remains timing-only. MPEG decoder
development headers and link libraries are unavailable in this environment; an installed FFmpeg
command-line program alone does not provide them. The proposed dependency is an LGPL shared
development bundle with pinned headers/import libraries/runtime packaging, followed by decode,
audio synchronization, frame upload and skip/close acceptance. No movie-playback completion is claimed.

The seven component executables and both aggregate targets are listed in
`EXECUTION_BATCH_2026-10-11.md`, with Windows validation logs and explicit remaining boundaries.
