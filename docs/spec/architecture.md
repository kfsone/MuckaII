# Architecture: layers, threads, channels

Build details: `docs/build.md`.

## Layers

DECISION One static library per layer, all in one process, no IPC. A library depends only on the
rows above it in this table. No upward and no sideways dependency.
source: agent

| Library | Owns | Depends on |
|---------|------|------------|
| `transport` | TCP connect/read/write, telnet negotiation (`connection.md`), the single socket writer | OS sockets |
| `mud` | bytes in, `events.md` events out; the event types | nothing |
| `persistence` | wire log records and the background writer; the settings file format | sqlite3 |
| `session` | login, probes, stats, sleep, dreamword, flow state, the ONE ordered outbound queue, probe-block insertion | `transport`, `mud`, `persistence` |
| `client` | aliases, F-key macros, history type, key->command mapping, command-boundary parsing, the 7-bit filter | `session` |
| `audio` | output device, mixer over preloaded PCM, the code/text -> sound mapping (`sounds.toml`) | `mud`, miniaudio |
| `core` | the core thread: reactor loop, the UI -> core FIFO and `submitLine`, wiring of the layers above | all of the above |
| frontends `gui`, `tui`, `bot` | presentation and input; the line buffer | `core`, `client` |

## Composition

DECISION One core serves every consumer: a GUI, a TUI, a WebUI, a bot, or a tool that
post-processes MUD streams for analysis. Each consumer assembles the stack it needs when it is
built, through a builder. Subsystems beyond transport, mud and the session's login are optional
and attached that way: probes and probe injection, sleep tracking, the wire log, audio. The core
runs without them. This is composition when the stack is constructed, not a runtime plugin
mechanism.
source: director

FACT The first consumer is a proof of concept: connect, log in, and reach the tea room. Designs are
revisited against it, with Mucka as the behavioural prototype (`docs/spec/`).
source: director

FACT Library `x` is CMake target `muckaiiX`: `mud` is `muckaiiMud` (`docs/build.md` "Targets").
source: agent

RULE A library's types and an object's owning thread are separate questions. A type may live in
`client` while its instance is owned by a frontend (the line buffer, the history recall list).
enforced_by: none: judgement
source: agent

## Threads

| Thread | Runs | Owns | Never |
|--------|------|------|-------|
| UI | Slint event loop (`gui`), FTXUI loop (`tui`), bot loop (`bot`) | line buffer, history recall list, widgets | blocks, touches the socket, waits on core |
| core | one reactor | socket, `transport`, `mud`, `session`, `client` instances, settings store | renders, touches disk |
| audio | miniaudio device callback | mixer voices | locks, allocates, decodes |
| writer | one background SQLite writer | the database connection, settings file writes | touches the socket |
| resolver | Windows: none of MuckaII's; `GetAddrInfoExW`'s completion routine on a system thread wakes core. POSIX: one short-lived detached thread per connect running `getaddrinfo` (`connection.md` 1) | the resolved addresses until core takes them | touches the server socket |

RULE Only the core thread touches the socket. `transport`, `mud`, `session` and `client` contain no
locks: each instance is touched by exactly one thread. Synchronisation lives only in the channels
below: the UI -> core FIFO in `core`, the SPSC ring in `audio`, the writer queue in `persistence`.
enforced_by: none: judgement
source: agent

DECISION The reactor is a plain `poll()` (`WSAPoll()` on Windows) over two sockets: the server socket
and a loopback UDP wake socket. Any thread wakes core by sending one byte to the wake socket. The
poll timeout is `session`'s next deadline on its injectable clock. Rejected: asio and libuv (a
dependency for one socket); a condition variable (cannot wait on a socket).
source: agent

RULE Each reactor turn, in this order: read the socket and decode; drain the wake socket and the
UI -> core FIFO into `session`'s queue; fire due `session` timers; write the queue. A timer send
(keepalive, wake probe) therefore never overtakes a line already in the FIFO (`input.md`). One turn
reads at most 16 times (`kMaxReadsPerTurn`), so a flooding server never starves the FIFO, and a
failed `poll()` ends the connection as a socket error rather than spinning.
enforced_by: none: judgement
source: agent

RULE Core assigns each wire record its run sequence and timestamp at handover, on the core thread.
That is `persistence.md`'s single serialisation point. The writer thread only stores.
enforced_by: none: judgement
source: agent

## Cross-thread channels

| From -> to | Carries | Mechanism | Wake |
|------------|---------|-----------|------|
| UI -> core | `Submit{text, origin}`, settings edits, connect/disconnect | mutex-guarded vector, swapped out whole by core | one byte to the wake socket |
| core -> UI | one event batch per socket read | `slint::invoke_from_event_loop` moving the batch (`gui`); in-place call (`tui`, `bot`) | Slint's own |
| core -> audio | `Play{sound, startFrame}` | lock-free SPSC ring, fixed capacity | none: the callback drains it each period |
| core -> writer | wire records, settings snapshots | mutex + condition variable queue, bounded: a record that does not fit is dropped and counted (`persistence.md`) | condition variable |

RULE The UI side of each channel holds its lock only for a push. The audio callback takes no lock.
enforced_by: none: judgement
source: agent

## Decoder API (`mud`)

DECISION `mud::Decoder` is synchronous: `feed(std::span<const std::byte>)` and `endOfRead()`.
Each call appends `mud::Event` values (a `std::variant`, one alternative per `events.md` event)
to a caller-owned `std::vector<mud::Event>&` batch, on the caller's thread. Rejected: a virtual
sink interface, and a `std::function` called per event.
source: agent

RULE `mud` has no clock, no I/O, no timers and no threads. Timers live in `session`, driven by an
injectable clock.
enforced_by: none: judgement
source: agent

DECISION In-process events carry `seq`, a per-connection counter. They carry no `rx`. The golden
serialiser (a test-support target of `mud`) counts reads and writes `rx` as `events.md` defines it.
source: agent

## Delivery

RULE Frontends subscribe to `session`, never to `mud`. `session` re-emits every `mud` event and adds
its own:

| Session event | Carries |
|---------------|---------|
| connection state | connecting, connected, closed, with reason |
| flow state | profile, pregame, game, tombstone (`flow.md`) |
| sleep state | awake, asleep, drugged (`session.md`) |
| stats | the current merged reading |
| dreamword | current word or none |
| outbound sent | the bytes written, `origin`, and the offsets of any probe block in them |

enforced_by: none: judgement
source: agent

RULE Events are delivered as one batch per socket read, on the core thread. The `gui` subscriber
marshals each batch to the UI thread in one hop. `tui` and `bot` consume in place. `audio`'s mapping
subscribes on the core thread and feeds the audio ring directly, never via the UI thread.
enforced_by: none: judgement
source: agent

DECISION Latency budget: 30-50 ms from bytes read to the frame presented that shows them, and from
FES read to the status bar repainted. Start time is the steady clock read by core at the socket
read; end time is the same clock read in Slint's after-rendering notifier. Measured by the spike's
latency test against a loopback fake server, not asserted.
source: director (the 30-50 ms target, `INTERNAL.md`); agent (the measurement points)
open: Slint's after-rendering notifier fires after the frame is rendered, before it is presented.
default: measure to the notifier and add one refresh interval; spike step 3 checks this against a camera or present timestamp on one target.

## Input: two orderings

RULE Box edits (typing, history recall, Ctrl+R prefill, Escape) are frontend-local. The UI loop
applies input events to the frontend-owned line buffer in arrival order (Invariant #1). No box edit
crosses a thread.
enforced_by: none: judgement
source: agent

DECISION Two orderings, not one queue. Box edits are serialised by the UI loop over the line buffer;
sends are serialised by the UI -> core FIFO. Both are filled in UI arrival order, so a hotkey after
Enter still lands behind that line. Rejected: one queue holding both box edits and sends (a box edit
would cross a thread and wait on core).
source: agent

RULE Enter snapshots the line buffer's content at that moment, clears it, appends to the recall
list, and calls `core::submitLine(text, origin)`. Never re-read a widget. `submitLine` only
pushes `Submit{text, origin}` onto the UI -> core FIFO and never blocks. Hotkeys, F-keys and macros
call the same entry point, so all sends from one frontend share one FIFO in UI arrival order.
enforced_by: none: judgement
source: agent

DECISION Core alone decides whether input is sensitive (`persistence.md`, Wire log). It publishes
its verdict (an atomic, updated before each event batch is delivered); `submitLine` returns the
verdict at that moment and carries it with the line. The frontend keeps a line out of the recall
list on that returned verdict and hides the command box on `Core::isInputSensitive()`, never on its
own copy of the flow state. The wire log masks a line when its carried verdict or the flow when it
is sent says so, so a flow change between Enter and the send never unmasks it. Rejected: the
frontend deciding from the flow events it has received (one batch behind core).
source: agent, on a review finding
DECISION The history recall list is owned by the frontend and appended at Enter on the UI thread.
Rejected: history owned by core, where Up straight after Enter could miss the line just entered
(`input.md` typing-path failure 4).
source: agent

RULE On core, `client` drains the FIFO in order, expands each line (aliases, macros, `$`
commands), applies the 7-bit filter as its last step, and hands
`Outbound{bytes, origin, boundaries, sleepAt}` to `session`'s queue. `boundaries` are byte
offsets of command boundaries in the post-filter bytes (transliteration changes lengths);
`sleepAt` marks the boundary before a sleep command. `session` never re-parses a line.
enforced_by: none: judgement
source: agent

Who finds boundaries and sleep commands is open (OPEN H3, deferred until the proof of concept).
Until it is ruled, `boundaries` and `sleepAt` are not built, and no probe is injected.

RULE `session` alone inserts marked probe blocks, only at offsets `client` reported
(`session.md`). `transport`'s single writer writes the queue in order and never reorders.
enforced_by: none: judgement
source: agent

| `origin` | Produced by | 7-bit filter | Probe insertion | Wire log |
|----------|-------------|--------------|-----------------|----------|
| `typed` | Enter, Send button | yes | at reported boundaries | verbatim in game mode; outside it masked, same length, followed by an annotation (`persistence.md`, Wire log) |
| `macro` | F-keys, on-screen F-keys, `^n` slots | yes | at reported boundaries | as `typed` |
| `hotkey` | Ctrl+D, Ctrl+F and kin | yes | at reported boundaries | as `typed` |
| `raw` | raw console | no; no expansion | none | marked raw-console send; outside game mode masked as `typed` |

RULE Every user-originated send (`typed`, `macro`, `hotkey`, `raw`, and any origin added later
that the user produces) is masked in the wire log outside game mode. `session::isUserOrigin`
classifies each origin with no default case, and `Session::queue`, which every send passes, applies
the mask, so no path can skip it.
enforced_by: none: judgement; clang's -Wswitch (Linux gate) fails an unclassified origin
source: agent, on a review finding
| `login` | `session` login drive | n/a | none | verbatim, except the account ID line: masked, same length, annotated (`persistence.md`) |
| `password` | `session` login drive | n/a | none | masked, same length, annotated (`persistence.md`) |
| `keepalive` | `session` idle timer | n/a | none | verbatim |
| `probe` | `session` | n/a | is the block | marked with position |
| `telnet` | `session`'s answers to telnet negotiation, and NAWS (`connection.md` 2) | n/a | none | verbatim |
| `width` | `session`'s `$-[/T{cols}$-]` on a resize in game mode (`connection.md` 4.1) | n/a | none | verbatim |

DECISION Masking is carried with each send, beside its `origin`: the login drive's account ID line
is a `login` send masked as the account ID, so the `login` row is not verbatim for that one line,
as `persistence.md` "Wire log" already requires. Telnet replies and NAWS have their own `origin`,
`telnet`, because they share the one ordered queue (`connection.md` 1, Outbound order) but no row
above produced them. Rejected: a separate `account` origin, which would split one login drive into
two producers.
source: agent; director (masking the account ID, `persistence.md`)

RULE Raw console sends bypass `client` expansion and the 7-bit filter but still go through
`session`'s queue, so they keep their place in the one ordered stream.
enforced_by: none: judgement
source: agent

## Bot

FACT `bot` is a frontend for tests and the director's AI experiments (`INTERNAL.md`, Stack). It
sends through `core::submitLine` like any frontend.
source: director

RULE Release builds never point a bot at the public game.
enforced_by: none: judgement
source: agent

## Audio

DECISION `audio` opens one miniaudio device at startup and mixes its own voices: preloaded PCM,
each voice with a start frame on the device's frame clock, voices overlapping. The playback rules
are `sounds.toml` `engine-always-open`, `engine-scheduled-start`, `no-silence-padding` and
`metronome-book-ends`. Rejected: miniaudio's `ma_engine` and its resource manager (node graph and
loader not needed).
source: agent
