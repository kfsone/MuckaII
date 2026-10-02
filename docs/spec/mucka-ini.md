# mucka.ini

FACT `mucka.ini` is the settings file of the previous client, Mucka. MuckaII reads it once, as user
data, on first run (see `persistence.md`, "Import"). This file is the complete on-disk format as
Mucka read and wrote it. MuckaII never writes this format.
source: reference

DECISION The import of `mucka.ini` is `scope: launch`.
source: director

FACT Individual keys carry their own scope in the tables below, from the launch list in
`INTERNAL.md`.
source: director

open: proposed: a key for a `later` feature is imported and kept, and nothing acts on it until that
feature is built. Rejected alternative: dropping it at import.
default: the proposal: import and keep.

RULE Every key and section not listed here is unknown and is ignored. An unknown key, a malformed
value or an unknown section never fails a read; a malformed value takes the default. Mucka's own
reader behaved this way.
enforced_by: none: judgement
source: reference

FACT Sample files with their expected parse are in `docs/evidence/ini-samples/` (see "Samples" at
the end).
source: director

## Where Mucka kept it

FACT Mucka shipped on Windows and Android only. It never shipped on Linux or macOS.
source: reference

| Platform | Lookup order (first existing file wins) | Where a new file was created |
|----------|------------------------------------------|------------------------------|
| Windows | 1. `mucka.ini` in the directory holding Mucka's executable; 2. `%USERPROFILE%\mucka.ini` | `%USERPROFILE%\mucka.ini` |
| Android | the app-private files directory of package `org.kfs.mucka`, file `mucka.ini` | same |

source: reference

FACT On Android the file is private to Mucka's package. No other app, MuckaII included, can open it
by path. The only route to it is a file the user exports and then hands over through a file picker.
source: reference

FACT Hand-written `[watch]` rules therefore exist in practice only on Windows.
source: reference

FACT Mucka wrote the file atomically: it wrote `mucka.ini.tmp` beside it, then renamed that over
`mucka.ini`. A leftover `mucka.ini.tmp` is a write that never finished. It is not a settings file.
source: reference

EVIDENCE On the director's machine, with Mucka 0.22.1 installed, the file is `%USERPROFILE%\mucka.ini`.
There is no `mucka.ini` beside the installed executable in `C:\Program Files\Mucka\`.
source: director (the director's machine, observed read-only)

## Encoding and lines

| Property | Read | Write |
|----------|------|-------|
| Encoding | UTF-8. A leading byte-order mark (UTF-8, UTF-16 or UTF-32) is honoured and dropped | UTF-8, no byte-order mark |
| Line endings | CRLF, LF and lone CR each end a line | the platform newline: CRLF on Windows, LF on Android |
| Final newline | optional | always written |
| Blank lines | ignored | preserved where they were |
| Whitespace | trimmed from both ends of every line, every section name, every key and every value | none added |

source: reference

EVIDENCE The director's file is plain ASCII with CRLF line endings and no byte-order mark.
source: director (the director's machine, observed read-only)

FACT Profile names, F-key macros and `[watch]` text are typed by the user, so the file may hold
non-ASCII UTF-8. The importer reads UTF-8. Anything that later goes to the server still passes the
7-bit send boundary (root `CLAUDE.md`).
source: reference

## Line grammar (all sections except the `[watch]` family)

| Line, after trimming | Meaning |
|----------------------|---------|
| empty | blank, ignored |
| starts with `;` or `#` | whole-line comment, ignored |
| starts with `[` and ends with `]` | section header. The name is the text between the brackets, trimmed |
| starts with `[` but does not end with `]` | not a header and not a key: ignored. Its section does NOT start (see edge cases) |
| contains `=` at position 1 or later | `key=value`. The key is everything before the first `=`. The value is everything after it, with any inline comment removed |
| contains no `=`, or starts with `=` | ignored |

source: reference

| Rule | Detail |
|------|--------|
| Case | Section names and keys match case-insensitively (`[Settings]` is `[settings]`, `FontSize` is `fontsize`) |
| Inline comment | In a value, a `;` or `#` that has whitespace directly before it starts a comment that runs to end of line. `saycolor=#f9f1a5` keeps its `#`. `fontsize=15 ; big` reads `15` |
| Quoting | None. Quote characters are literal parts of the value |
| Escapes | None. A backslash is a literal character |
| Continuation lines | None |
| Empty value | `key=` is present with an empty value. For a number or a boolean that counts as absent |
| Lines before the first header | belong to no section and are never read |

source: reference

### Duplicates

| Case | Effect |
|------|--------|
| A key repeated in one section, read as a single value (every scalar key below) | the FIRST occurrence wins |
| A key repeated in one section, read by walking the section (`F<n>` keys, the `sound*` families, `[profiles]` entries) | F-keys and sound volumes and fallbacks: the LAST occurrence wins. Sound off-switches: any `off` occurrence switches it off. `[profiles]`: every line counts, in file order, and a repeated name keeps its first position |
| A section header repeated | only the block under the FIRST header is read. Blocks under later headers with the same name are invisible |
| `[watch]` repeated | every `[watch]` block is read (the `[watch]` family has its own parser) |

source: reference

### Edge cases

| Input | Effect in Mucka |
|-------|-----------------|
| `[settings] ; globals` (comment after a header) | not a header. Lines below it stay in whichever section was open before it. If none was, they belong to no section |
| `[profile:MUD2 # test]` | a valid header. The profile name is `MUD2 # test`. The bracket must still end the line |
| A profile name holding whitespace followed by `;` or `#`, listed in `[profiles]` | the `[profiles]` entry is cut at the marker, so it no longer matches its own `[profile:...]` header. The profile still loads, appended after the listed ones (see `[profiles]`) |
| `[settings:Name]` or `[fkeys:Name]` whose Name matches no profile | an orphan left by a deleted profile. Read by nothing |
| A value with leading or trailing spaces | trimmed. A value cannot begin or end with a space |

source: reference

### Value types

| Type | Read | Written by Mucka |
|------|------|------------------|
| bool | `yes`, `true`, `on`, `1` mean true. `no`, `false`, `off`, `0` mean false. Any case. Anything else, or empty, means absent | `yes` / `no` |
| int | a 32-bit decimal integer, culture-invariant. An optional leading sign is allowed. No thousands separators, no hex, no decimal point. Anything else means absent | invariant decimal |
| real | culture-invariant decimal with an optional fraction and exponent (`884`, `884.5`) | invariant, rounded to an integer |
| string | the trimmed value, verbatim | verbatim |
| hex colour | 6 hex digits, with an optional leading `#`. Anything else means absent | 6 lowercase hex digits, no `#` |
| face | a comma-separated list of `italic`, `bold`, `dim`, or the single word `none`. Any case, spaces around items allowed. One unknown word makes the whole value absent | items in `italic,bold,dim` order, or `none` |

source: reference

RULE An absent value takes the built-in default from the tables below. It never takes the value of
a neighbouring section, except through the precedence rules in "How the sections combine".
enforced_by: none: judgement
source: reference

## Sections

| Section | Holds | Written by |
|---------|-------|------------|
| `[settings]` | global settings, and the always-global display keys | Mucka |
| `[settings:Name]` | one profile's own copy of the settings-scope keys | Mucka, when "Save to profile only" was ticked on the settings page |
| `[fkeys]` | global F-key macros `F1`-`F36` | Mucka |
| `[fkeys:Name]` | one profile's own F-key macros | Mucka, when "Save to profile only" was ticked on the F-key page |
| `[profiles]` | profile names in most-recently-used order | Mucka |
| `[profile:Name]` | one profile's connection identity | Mucka |
| `[watch]` | watchword trigger slots | the user, by hand. Mucka never wrote it, but preserved it |
| `[watch slotname]` | answer table for one slot | the user, by hand |

source: reference

FACT Mucka rewrote only the key lines it owned. Comments, blank lines, key order, unknown keys and
unknown sections, including the whole `[watch]` family, survived each save byte for byte (apart
from line endings).
source: reference

### `[profiles]`

| Key | Value | Meaning |
|-----|-------|---------|
| `1`, `2`, ... `N` | profile name | most recently used first |

source: reference

| Rule | Detail |
|------|--------|
| Order | The MRU order is the order of the LINES in the file. The key numbers are ignored on read: `2=B` above `1=A` gives B, then A |
| Empty value | skipped |
| Repeated name (any case) | keeps its first position |
| Listed name with no `[profile:Name]` section | dropped |
| `[profile:Name]` section not listed | appended after the listed profiles, in file order |
| No profiles at all | Mucka showed two built-ins: `MUD2 UK` (`mud2.co.uk`, port 23), then `MUD2.COM` (`www.mud2.com`, port 27723) |
| Writing | Mucka rewrote the keys as `1`..`N`, removed numeric keys outside that range, and removed `[profile:...]` sections for deleted profiles. Their `[settings:Name]` and `[fkeys:Name]` sections were left in place |

source: reference

open: proposed: the importer does not invent the two built-in profiles from an ini that defines
none. The built-ins are MuckaII's own first-run default, a separate matter.
default: the proposal: the importer adds no profiles from an ini that defines none.

open: whether MuckaII's first-run built-in profiles are the same two (`MUD2 UK`, `MUD2.COM`).
default: yes, the same two, as `connection.md` 1 lists them.

### `[profile:Name]`

FACT `Name` is the profile name: the header text after `profile:`, trimmed. Profile names match
case-insensitively. A name may contain spaces, `:`, `#`, `;` and other punctuation.
source: reference

| Key | Type | Default | Meaning | Scope |
|-----|------|---------|---------|-------|
| `host` | string | `mud2.co.uk` | server host name. Empty means the default | launch |
| `port` | int | `23` | TCP port | launch |
| `account` | string | empty | MUD2 account ID. Auto-login answers the account and password prompts only when `telnetlogin` is on and both the account ID and a password are known | launch |
| `rememberpassword` | bool | `no` | the password is kept in the OS secure store. The password itself is NEVER in the ini | launch |
| `telnetlogin` | bool | `yes` | answer the host's telnet `login:` prompt automatically | launch |
| `loginname` | string | `mud` | the name sent to `login:`. Empty means `mud` | launch |
| `columns` | int | `0` | terminal width advertised to the server. `0` means fit the window. Clamped to 0-160 when used. See the precedence note below | launch |
| `antiidle` | int | `0` | seconds of no input before a blank keep-alive line is sent. `0` means off. Clamped to 0-3600 | later |
| `keepscreenon` | bool | `no` on desktop, `yes` on Android | keep the display awake while connected | later |
| `defaulthotkeys` | bool | `yes` | at session start, fill empty F-key slots F1-F5 with the built-in macros below. The built-ins are never written to the ini | launch |
| `guidedlogin` | bool | `no` | drive the MUD shell's option menu and persona choice automatically after login (MuckaII: managed persona selection) | launch |
| `guidedloginpersona` | string | empty | persona to pick under guided login. Empty means ask | launch |

source: reference

| Built-in macro (`defaulthotkeys=yes`) | Slot |
|---------------------------------------|------|
| `qscan` | F1 |
| `wield best weap` | F2 |
| `open bag,g 1 wafer from bag,eat 1 wafer` | F3 |
| `flee o` | F4 |
| `qw,qs` | F5 |

source: reference

FACT Passwords are never in `mucka.ini`. Mucka kept each password in the OS secure store under a
key derived from the profile name. MuckaII cannot read another app's secure-store entries, so
passwords are not importable.
source: reference

### `[settings]` and `[settings:Name]`: settings-scope keys

FACT These keys follow the per-profile override (see "How the sections combine"). Mucka wrote them
together, as one block, to whichever of the two sections the profile used.
source: reference

| Key | Type | Default | Meaning | Scope |
|-----|------|---------|---------|-------|
| `fontsize` | int | `0` | terminal font size in pixels. `0` means use `defaultfontsize`. Mucka's editor offered 9-24 | launch |
| `columns` | int | `0` | as `[profile:Name] columns`. Written with the same value | launch |
| `volume` | int | `75` | master sound volume, percent. Clamped to 0-100 | launch |
| `statupdate` | int | `10` | seconds between FES stat-update heartbeats. `0` means off. Clamped to 0-30 | launch |
| `mutebeep` | bool | `no` | permanently silence the terminal bell | launch |
| `logttr` | bool | `no` | debug builds only: note each reset-timer reading in the session transcript | later |
| `sounds` | bool | `yes` | master switch for all sound effects, bell included | launch |
| `soundgroup.<group>` | `off` | (absent: on) | that sound group is off. Only a false value counts; `on` is the same as absent | launch |
| `sound.<code>` | `off` | (absent: on) | that one sound is off. Only a false value counts | launch |
| `sounddefault.<group>` | sound code | (absent: none) | the sound to play for a code in that group that has no sound of its own | launch |
| `soundgroupvol.<group>` | int 0-100 | (absent: master volume) | the group's volume. Out-of-range values are clamped | launch |
| `soundvol.<code>` | int 0-100 | (absent: group volume) | the sound's volume. Out-of-range values are clamped | launch |

source: reference

| Sound-key detail | Rule |
|------------------|------|
| `<group>` | a sound-code prefix such as `07` or `13`, or the word `bell` for the terminal bell (`soundgroupvol.bell` only. The bell's on/off is `mutebeep`). Codes and groups are listed in `sounds.toml` |
| `<code>` | a sound code such as `0703` or `070001` |
| Matching | longest prefix first: `soundgroupvol.` before `soundgroup.`, `soundvol.` before `sound.`. The key `sounds` belongs to none of the families |
| Absence | a section with no sound keys at all means every sound is on at the master volume, with no fallbacks |
| Writing | Mucka removed every `sound.`/`soundgroup.`/`sounddefault.`/`soundvol.`/`soundgroupvol.` key from the section and rewrote only the overrides |

source: reference

open: `sounds.toml` `[ini_keys]` also lists `soundgroup.bell`, groups `tell` and `alert`, and
non-numeric codes such as `tell-wiz` and `alert-flee-failed`, which the table above does not allow.
Which key set did the reference read and write?
default: the importer accepts the union of both key sets, so nothing either list names is lost.

FACT A volume plays at the sound's own override, else its group's, else the master `volume`.
source: reference

### `[settings]` only: always-global keys

FACT These keys are read from `[settings]` only. A copy inside `[settings:Name]` or
`[profile:Name]` is read by nothing.
source: reference

| Key | Type | Default | Meaning | Scope |
|-----|------|---------|---------|-------|
| `defaultfontsize` | int | `0` | the fallback font size when `fontsize` is 0. `0` means 15 px. Mucka's editor offered 9-24 | launch |
| `defaultcolumns` | int | `0` | stored and edited in Mucka's Display tab, clamped to 0-160. Nothing else read it (`ui.md` section 7 lists it among the settings dialog rows) | later |
| `dreamwordsizeoffset` | int | `0` | dreamword text size relative to the terminal font. Clamped to -2..4 | launch |
| `showonline` | bool | `yes` | Online section of the side panel expanded. Also subscribes to FEW updates | later |
| `showinventory` | bool | `yes` | Inventory section expanded. Inventory or items-here expanded also subscribes to FEI updates | later |
| `showitemshere` | bool | `yes` | Items-here section expanded | later |
| `showmapcompass` | bool | `yes` | Map/compass section expanded | later |
| `maxonlinedisplay` | int | `0` | maximum names in the Online list. `0` means no limit | later |
| `onlinenamesonly` | bool | `no` | Online list shows names only | later |
| `onlineforgetwindow` | int | `5` | minutes a player stays in "Recent" after leaving Online. `0` means forget at once. Editor range 0-10 | later |
| `floatonline` | bool | `no` | the Online list starts floating rather than pinned | later |
| `floatcompass` | bool | `no` | the compass starts floating rather than pinned | later |
| `chattheme` | word | `lamplight` | the chat colour theme: `lamplight`, `dusk`, `rose`, `tidewater`, `bland`, in any case. Unknown means `lamplight` | launch |
| `emotecolor` | hex colour | theme | colour of the speaker of every chat line, and of act/emote text | launch |
| `saycolor` | hex colour | theme | colour of said words | launch |
| `shoutcolor` | hex colour | theme | colour of shouted words | launch |
| `tellcolor` | hex colour | theme | colour of told words, both directions | launch |
| `emoteown`, `emoteother` | face | `none`, `none` | face of the speaker part, on the player's own / another's message | launch |
| `sayown`, `sayother` | face | `italic`, `none` | face of said words | launch |
| `shoutown`, `shoutother` | face | `italic`, `none` | face of shouted words | launch |
| `tellown`, `tellother` | face | `italic`, `none` | face of told words | launch |
| `showcombatrail` | bool | `no` | Combat Rail shown. Written the moment it is toggled | later |
| `showcombatstats` | bool | `yes` | Combat Rail draws its stat rows | later |
| `scorepanelwidth` | real | `760` | score-graph panel width. Floor 660 | later |
| `scorepanelheight` | real | `400` | score-graph panel height. Floor 320 | later |

source: reference

FACT A stored colour is used as it stands, even when it differs from the theme. The theme is the
base the colour editor shows and resets to. An absent or malformed colour key takes the selected
theme's colour.
source: reference

| Theme (`chattheme`) | emote | say | shout | tell |
|---------------------|-------|-----|-------|------|
| `lamplight` (default) | `bfa36a` | `ede6d0` | `ffa033` | `7fb8ff` |
| `dusk` | `d69a5c` | `f4ddb5` | `ff8a3d` | `a99cff` |
| `rose` | `d4a04c` | `f7e7b0` | `ff8a3d` | `ff9ec7` |
| `tidewater` | `d4a04c` | `f7e7b0` | `ff8a3d` | `7fb8ff` |
| `bland` | `cab872` | `faeca8` | `ffc474` | `d8e09c` |

source: reference

### `[fkeys]` and `[fkeys:Name]`

| Key | Value | Chord |
|-----|-------|-------|
| `F1`-`F12` | macro text | F1-F12 with no modifier |
| `F13`-`F24` | macro text | Shift+F1 to Shift+F12 |
| `F25`-`F36` | macro text | Ctrl+F1 to Ctrl+F12 |

source: reference

| Rule | Detail |
|------|--------|
| Key form | `F` or `f` followed by a decimal 1-36. Any other key in the section is ignored |
| Order | free. The director's file has `F18` before `F11` |
| Empty slot | the key is absent. Mucka removed the line when a slot was cleared and never wrote `F7=` |
| Section present but empty | means "saved, every slot empty". This differs from the section being absent (see "How the sections combine") |
| Value | sent to the server as ONE line, verbatim, after alias and watchword expansion. Commas are not split by the client: `e,,n,e` reaches the server as typed. Behaviour of `,` and empty items is the server's |
| Value starting with `$` | not sent. The rest names a watchword slot, and that slot's queued answer is spoken |
| Inline comment | a value holding whitespace and then `;` or `#` is cut there on read (see "Line grammar") |

source: reference

FACT The F-key macro syntax and its expansion belong to `input.md` and `keys.toml`. This file
records only how the macros are stored.
source: reference

### `[watch]` and `[watch slotname]`

FACT This family is read by a separate parser with its own rules, NOT the line grammar above. Mucka
never wrote any of it.
source: reference

DECISION The importer copies the `[watch]` family verbatim: every raw line from each `[watch]` or
`[watch <name>]` header up to the next header of any kind, comments and blank lines included, in
file order. It does not re-parse or normalise them. Rejected: parsing into key/value pairs, which
loses comments and would split keys such as `"apple=3, p..."` at the wrong `=`.
source: director

| Element | Rule |
|---------|------|
| Comment | a trimmed line starting with `;` or `#`. There are no inline comments: `;` later in a line is part of the text |
| `[watch]` | starts the slot list. Every `[watch]` block in the file is read |
| `[watch name]` | `watch`, one space, then the slot name (trimmed, any case). Starts that slot's answer table. Ignored if the slot was not defined in an earlier `[watch]` block |
| Any other header | ends the current `[watch]` block |
| Separator | ` = ` (space, equals, space). A line without it is ignored. Text before it, right-trimmed, is the key. Text after it, left-trimmed, is the value. So keys and answers may contain `=` |
| Slot line (in `[watch]`) | `slotname = prefix...suffix`. The first `...` splits the trigger into prefix and suffix. Without `...` the whole value is the prefix |
| Answer line (in `[watch name]`) | `key = answer`. One pair of surrounding double quotes on the key is removed. A key ending in `...` (after unquoting) is a prefix match; otherwise it is exact |
| Echo mode | a slot with no `[watch name]` table queues the captured text itself as the answer |
| Whitespace | every whitespace run in triggers, keys and scanned text is collapsed to one space before matching |
| Limits | 20 slots; 32 answers per slot; answers cut to 48 characters; captures cut to 80 characters |
| Use | `$slotname` in outgoing text is replaced by the queued answer, which is then cleared |

source: reference

FACT A `[watch name]` section also passes the generic line grammar as a section named `watch name`.
Nothing in the generic sections reads it.
source: reference

FACT Watchword behaviour is `scope: later`. Importing the `[watch]` text verbatim is `scope: launch`,
so the rules are waiting when the feature is built.
source: director

## How the sections combine

| Part | Section used for profile P |
|------|----------------------------|
| settings-scope keys | `[settings:P]` if that header exists, otherwise `[settings]`, otherwise built-in defaults |
| F-keys | `[fkeys:P]` if that header exists, otherwise `[fkeys]`, otherwise all 36 slots empty |
| always-global keys | `[settings]` only |
| identity keys | `[profile:P]` only |

source: reference

RULE An override REPLACES; it does not merge. When `[settings:P]` exists, every settings-scope key
comes from it alone, and a key missing there takes its built-in default, NOT the `[settings]`
value. When `[fkeys:P]` exists, an F-key missing there is empty even if `[fkeys]` has it.
enforced_by: none: judgement
source: reference

FACT The existence of `[settings:P]` or `[fkeys:P]` was itself the user's choice ("Save to profile
only"). The importer keeps that choice per profile, per part.
source: reference

FACT MuckaII layers per key instead of replacing. For F-keys the importer carries the replace
semantics over as 36 explicit per-slot values in P's profile layer, with `cleared` for each slot
`[fkeys:P]` lacks. Settings-scope keys are written where Mucka's value differs from what layering
would give (`persistence.md`, "Settings storage").
source: director

| Value | Precedence, strongest first |
|-------|-----------------------------|
| font size | `fontsize` (from the settings section used) if above 0; else `[settings] defaultfontsize` if above 0; else 15 px |
| terminal columns | `columns` from the settings section used, if present; else `[profile:P] columns`. Mucka wrote the same value to both on save |

source: reference

EVIDENCE The director's file has `[profile:<alt>] columns=100` and `[settings:<alt>] columns=100`
(profile name withheld),
and `[profile:MUD2 UK] columns=90` with `[settings] columns=90`. They agree in every case.
source: director (the director's machine, observed read-only)

open: should MuckaII keep a separate column count per profile and a global one, or one value per
profile? The ini always carries two copies.
default: one layered key, like every other setting (`persistence.md`, Settings storage). The importer
takes the value by the precedence table above.

## Obsolete keys

FACT Mucka read nothing from these keys. They carry no setting to import.
source: reference

open: proposed: the importer drops them without telling the user.
default: the proposal: drop them silently.

| Key | Where found | Status |
|-----|-------------|--------|
| `logwiresession` | `[settings]` | Read by nothing. The wire log is always on, with no setting |
| `sendbutton` | `[settings]` | Read by nothing |
| `onlinefloating` | `[settings]` | Read by nothing. Replaced by `floatonline` |
| `fewrefresh` | `[profile:Name]` | Read by nothing |
| `menamecolor`, `mespeechcolor`, `speakercolor`, `speakerown`, `speakerother` | any section | Read by nothing. Mucka deleted them from every section on each save |
| `showcombatrail`, `showcombatstats` | `[profile:Name]`, `[settings:Name]` | Read by nothing (global `[settings]` only). Mucka deleted these copies |
| any always-global key inside `[settings:Name]` or `[profile:Name]` | per-profile sections | Read by nothing |

source: reference

EVIDENCE The director's file holds `sendbutton`, `onlinefloating` and `logwiresession` in
`[settings]`, and `fewrefresh=15` in two `[profile:...]` sections.
source: director (the director's machine, observed read-only)

## Samples

| File | Covers |
|------|--------|
| `docs/evidence/ini-samples/typical.ini` | Derived from the director's file with all personal data replaced. Three profiles, MRU order differing from file order, a `[settings:Name]` override, global F-keys out of order, a full `[watch]` family, every obsolete key the director's file had |
| `docs/evidence/ini-samples/overrides.ini` | Override replaces rather than merges, an empty `[fkeys:Name]`, orphan override sections, sound families, `[profiles]` keys out of order, an unlisted profile |
| `docs/evidence/ini-samples/edge-cases.ini` | Inline comments, a header with a comment, a duplicate section, duplicate keys, case, malformed numbers and booleans, a profile name with `#`, watch slot defined late |

### Expected parse: `typical.ini`

| Item | Expected |
|------|----------|
| Profile order | `Alt Box`, `Moor`, `Home UK` |
| `Alt Box` settings source | `[settings:Alt Box]`: `volume` 48, `fontsize` 14, `columns` 100, `statupdate` 5. `sounds` on. Always-global keys from `[settings]` |
| `Moor` settings source | `[settings]`: `volume` 40 |
| F-keys, all profiles | from `[fkeys]`: F1 `qscan`, F4 `qs,fes,qw,zz`, F18 present, F25 present. F12 empty |
| Watch | slots `crypta`, `cryptb`, `note`. `note` is echo mode. 2 answers under `crypta`, 2 under `cryptb` |
| Dropped | `sendbutton`, `onlinefloating`, `logwiresession`, `fewrefresh` |

### Expected parse: `overrides.ini`

| Item | Expected |
|------|----------|
| Profile order | `Beta`, `Alpha`, `Gamma` (`2=Beta` is the first line; `Gamma` has no `[profiles]` entry and is appended) |
| `Alpha` | `volume` 75 (built-in default: `[settings:Alpha]` exists without `volume`, so `[settings] volume=30` is NOT used). `fontsize` 18. Sounds: all on at the master volume (its override has no sound keys, and `[settings]`'s are not used). `chattheme` `dusk` (always-global). F-keys: all empty (`[fkeys:Alpha]` is present and empty, and `defaulthotkeys=no` adds no built-ins) |
| `Beta` | `volume` 30, from `[settings]`. F1 `look`, F2 `inventory`, from `[fkeys]` |
| `Gamma` | host `mud.example.org`, port 2323, `telnetlogin` no, `loginname` `mud` (empty value) |
| Sounds in `[settings]` | master on; group `13` off; sound `0703` off; group `07` fallback `070000`; group `07` volume 50; sound `0711` volume 100 (`150` clamped); `bell` group volume 20 |
| Orphans | `[settings:Deleted]` and `[fkeys:Deleted]` read by nothing |

### Expected parse: `edge-cases.ini`

| Item | Expected |
|------|----------|
| `[settings] ; global block` | not a header. Its lines fall into `[fkeys]` above it and are ignored there (not `F<n>` keys) |
| `[settings]` (second, real header) | `fontsize` 16 (`16 ; big` minus comment). `volume` 60 (first of two). `mutebeep` true (`On`). `statupdate` absent, so 10 (`ten` is not a number). `saycolor` `f9f1a5` (`#` kept, no space before it) |
| Duplicate `[settings]` block | invisible: `columns=200` there is never read |
| Profile order | `Plain`, `Test # one`. In `[profiles]`, `1=Test # one` reads as `Test`, which has no section and is dropped. `2=Plain` is listed. `[profile:Test # one]` is then appended as an unlisted section |
| `[profile:Plain]` | `port` absent, so 23 (`23x`). `rememberpassword` absent, so false (`maybe`) |
| `[fkeys]` | F3 `kill rat with sword` (the later of two). `F37` and `G1` ignored |
| Watch | slot `late` is defined in a second `[watch]` block after `[watch late]`, so the `[watch late]` table is ignored and `late` is echo mode |
