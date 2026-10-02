# Golden event streams

FACT Each `<capture>.jsonl` here is the previous client's parser output for
`docs/evidence/captures/<capture>.c1`, mapped into schema 2 in `docs/spec/events.md`.
source: reference

RULE The goldens are the reference's output, and they rank below Bartle's documents and below the
captures. A diff between the MuckaII decoder and a golden is a lead to investigate, not a target to
match. Where the spec follows Bartle or a capture against the reference, MuckaII is right and the
golden differs; each such place is listed under "Known differences".
enforced_by: none: judgement
source: director

FACT The producer fed the 18 priming bytes `events.md` specifies as one read (all six captures begin
mid-session), then the `rx` records one at a time, and signalled end-of-read after each record. Within
a record it handed the parser one byte per call, so it could watch the parser's state between bytes;
this is the same as one call per record, because the parser's feed is a loop over single bytes and
end-of-read fires only at record boundaries. No wire-tier events are written. `scripts/check-goldens.py` validates every file against the
schema block in `events.md`.
source: reference

FACT `sound_code` and `spell_phrase` come from the reference's per-header sound queue and its 11-phrase
capture, which fire for every such header whoever the spell is on.
source: reference

EVIDENCE Counted in these files: the `sound_code` and `spell_phrase` counts agree with
`c1-codes.toml` `observed`; five `11 00`..`11 03` headers give five `spell_phrase` records. Per
capture:
source: reference (counted in the `.jsonl` files here)

| Capture | Records | `sound_code` | `spell_phrase` |
|---------|---------|--------------|----------------|
| blind-vs-water-snake | 138 | 7 | 3 |
| dark-two-rats | 219 | 13 | 0 |
| invisible-man-preemptive | 132 | 2 | 0 |
| invisible-player-joins | 257 | 24 | 2 |
| phone-login-at-20-columns | 112 | 0 | 0 |
| wyvern-poison-death | 59 | 5 | 0 |

FACT Against the schema 1 goldens the only changes are the added `sound_code` and `spell_phrase`
records, and the first `prompt` of `phone-login-at-20-columns.jsonl`, which the gate now closed by
priming discards (it follows a leading FES reply).
source: reference

## Known differences

FACT Places where the spec departs from the reference, so a correct MuckaII decoder's stream
differs from these goldens.
source: director

| Capture | Event | Difference |
|---------|-------|------------|
| dark-two-rats | `room_long` | MuckaII reports the last row of each long description (`events.md` Readings); the golden has no record for it, because the server closes `02 02` before that row's line end. Both rooms in the capture |

## Gaps

FACT Cannot produce: the schema has the event, the reference gives nothing to map from.
source: reference

| Capture | Event | Issue |
|---------|-------|-------|
| - | `account_change` | The `95 02` block is read, but nothing is reported outward. No capture has one. |

FACT Mapped, not exercised: the exporter maps these from the reference, but no capture holds the code
that produces them.
source: reference

| Event | Mapped from | Issue |
|-------|-------------|-------|
| `reset_signal` | The reference's reset signals | It reports `06 04` and `06 06` only when sent with exactly two numbers. No capture has a `06` code. |
| `teller` | The reference's tell sender | No capture has a `09` code. |

FACT No slot: the reference reports these, and the schema has nowhere to put them, because they
belong to a layer above the decoder or are client policy. They are left out of the goldens. The
counts are in the EVIDENCE table after this one.
source: reference

| Capture | Reference output | Issue |
|---------|------------------|-------|
| all six | Probe hint ("state may be stale") from codes and from plain un-coded lines | Client policy, not stream content |
| blind-vs-water-snake, invisible-player-joins | `11` phrase resolved to a status-effect change on the player (own phrases only) | Status-bar layer (`ui.md` "Effect icons"); the schema carries every bracket as `spell_phrase` |
| dark-two-rats | Room entry from the prose "It's too dark to see now" | Prose, layer above. The reference's code-path room entries equal the `room_name` records |
| blind-vs-water-snake, dark-two-rats | `04 0x 01..05` creature-presence sentence, with nested `03` object names masked | No slot |
| dark-two-rats, invisible-man-preemptive, invisible-player-joins | Prose "(Persona saved on ...)" score announcement | Prose reading, excluded by events.md |
| phone-login-at-20-columns | Prose stats reading on a displayed line | Excluded by events.md. Only values from `89` brackets are written |
| - | Text-triggered sounds and tell alerts | Layer above (`events.md` "Code events"). None fire in the six captures. |
| - | Sounds dropped for an `OK, ` self-act echo | Layer above; the exporter writes the `sound_code` anyway. No capture has one. |
| - | Underline on a tell's sender and verb | Display decoration the reference writes into the line's styles; an exporter would carry it into `text` styles. No capture has a tell. |
| - | Partial line forced out by a `98` code, ungated and outside end-of-read | No slot, so it would be dropped. No capture has a `98`. |
| - | `05` presence player name, prose `dir: Destination.` exits line, task-completed prose, frame boundary at session end | No slot. None of these occur in the six captures. |

EVIDENCE Counted in the reference's output for the captures named in the table above, one count per
capture, as recorded:
source: reference (counted in the reference's output for the `.c1` captures)

| Reference output | Counts |
|------------------|--------|
| Probe hint ("state may be stale") | 25, 19, 27, 25, 21, 9 |
| `11` phrase resolved to a status-effect change | 2, 1 |
| Room entry from the prose "It's too dark to see now" | 1 |
| The reference's code-path room entries (equal to the `room_name` records), in the three room captures | 4, 2, 4 |
| `04 0x 01..05` creature-presence sentence | 3, 1 |
| Prose "(Persona saved on ...)" score announcement | 2, 2, 1 |
| Prose stats reading on a displayed line | 5 |
