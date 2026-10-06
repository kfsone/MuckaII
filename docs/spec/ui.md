# UI: terminal look, chat colours, status bar, dreamword

Elsewhere: sounds `sounds.toml`; per-code colours `c1-codes.toml`; chords `keys.toml`; FES
heartbeat and width negotiation `connection.md`; scrollback, selection, F-key bar `input.md`.
Glyphs are written as code points (root `CLAUDE.md`, 7-bit text files).

RULE Every tap or click target in this file (dreamword pill, rec chip, score, menu rows) hands
keyboard focus back to the command input when it is done (Invariant #0).
source: director
enforced_by: none: judgement

## 0. Game screen layout

FACT The in-game screen has five primary components:

| Component | Scope | Contents |
|-----------|-------|----------|
| Stats bar | now | Runs along the top. Holds the status fields (section 3) and the settings menu, shown as a hamburger button. |
| Side panel | later | Optional, on the left: `later/side-panel.md` |
| Terminal scroll | now | The main component. Takes all the space the others leave. |
| Combat panel | later | Optional: `later/combat-panel.md` |
| Input box | now | The command input, along the bottom. |

source: director

## 1. Terminal look (scope: now)

### 1.1 Font and cell grid

| Property | Value | source |
|----------|-------|--------|
| Family | Cascadia Mono (regular), Cascadia Mono Italic for italic runs; release v2407.24, embedded in the app | reference |
| Default size | 15 px | reference |
| Size range | 9-24 px (setting `fontsize`) | reference |
| Line height | 1.30 x the font's natural extent (ascent + descent), glyph box centred vertically in the line | reference |
| Cell width | one advance; every glyph, including italic and bold, stays on the column grid | reference |
| Wrap | hard wrap at the effective column count (see 3.6) | reference |

RULE Italic glyphs lean past their cell. A neighbouring span's background never clips an italic
glyph's overhang.
source: reference
enforced_by: none: judgement

### 1.2 Palette

FACT The reference uses the Campbell palette (the Windows Terminal default dark scheme). Clio
draws with curses colours and has no hex values to copy.
source: reference
open: Director to confirm Campbell for MuckaII.
default: Campbell, as the table below gives it.

| Slot | Name | Hex |
|------|------|-----|
| 0 | Black | #0C0C0C |
| 1 | Red | #C50F1F |
| 2 | Green | #13A10E |
| 3 | Yellow (brown) | #C19C00 |
| 4 | Blue | #0037DA |
| 5 | Magenta | #881798 |
| 6 | Cyan | #3A96DD |
| 7 | Light grey | #CCCCCC |
| 8 | Dark grey (bright black) | #767676 |
| 9 | Bright red | #E74856 |
| 10 | Bright green | #16C60C |
| 11 | Bright yellow | #F9F1A5 |
| 12 | Bright blue | #3B78FF |
| 13 | Bright magenta | #B4009E |
| 14 | Bright cyan | #61D6D6 |
| 15 | White | #F2F2F2 |

source: reference

| Role | Value | source |
|------|-------|--------|
| Default foreground | slot 7, #CCCCCC | reference |
| Pane background | slot 0, #0C0C0C | reference |
| Span background "default" | not painted; the pane background shows | reference |
| Selection | #3A6EA5 at alpha 0x99, drawn over the glyphs | reference |

FACT Bartle's 99 absolute colours give 16 foregrounds and 8 backgrounds and a "99 99 default
colour". Slot numbers above match his numbering (99 01 red ... 99 15 white).
source: bartle:FE4 code 99

### 1.3 Attributes

| Attribute | Input | Drawn as | source |
|-----------|-------|----------|--------|
| Bold | SGR 1 / 22 | Bright promotion: slots 0-7 become 8-15. Plus synthetic weight: glyph stroked and filled at stroke width size/24, advance unchanged | reference |
| Italic | SGR 3 / 23 | Cascadia Mono Italic, same advance | reference |
| Underline | SGR 4 / 24 | Filled rule in the foreground colour, top at baseline + 6% of line height, thickness 5% of line height (min 1 px), full span width | reference |
| Reverse | `$-R` / `$-r`, SGR 7 / 27 | Parsed and tracked, NOT drawn: no fg/bg swap | reference |
| Blink | SGR 5 / 25 | Parsed, not drawn | reference |
| Dim | SGR 2 | Not recognised. Dim exists only as a chat face (see 2.3) | reference |
| Bright fg/bg | SGR 90-97 / 100-107 | slots 8-15 | reference |
| Extended colour | SGR 38 | Ignored | reference |
| Reset | SGR 0 or empty | Default style | reference |

FACT Bartle lists `$-R` / `$-r` as reverse video on / off and lets a client ignore any escape it
does not find useful, though a client should handle `$-C` and `$-K`.
source: bartle:FE4 section 2
open: The reference never draws reverse video. Should MuckaII swap fg/bg for `$-R` / SGR 7? Bartle
permits ignoring it; nobody has a capture of the server sending it.
default: parse and track reverse, and do not draw it, as the reference does and Bartle permits.

## 2. Chat colouring (scope: now)

DECISION Chat colouring, its themes, colours and faces, and their settings are `scope: now`.
source: director

### 2.1 What counts as a chat part

| Part | Codes | Gets the colour of row |
|------|-------|------------------------|
| Speaker label and act/emote text | 09, 09 00, 09 04 .. 09 10 | Emotes |
| Shouted words (shout, yell, yodel, scream, holler, cheer) | 09 01 | Shout |
| Said words (say, whisper) | 09 02 | Say |
| Told words, both directions | 09 03 | Tell |

source: bartle:FE4 code 09; reference

RULE A span inside a C09 message but styled by another code (a rainbow adjective in a name, say)
is not a chat part and keeps its own colour.
source: reference
enforced_by: none: judgement

### 2.2 Colour rules

| Rule | source |
|------|--------|
| A say: label and emote framing in the Emotes colour, quoted words in the Say colour | reference |
| A shout or a tell: the whole message, label included, in the Shout / Tell colour. The label keeps the Emotes row's face | reference |
| On every chat line, bold and italic sent by the server are removed. A non-chat span that lost its bold keeps its bright colour (bright slot written instead) | reference |
| A message the server wrapped over several rows: every row takes the colour and own/other verdict of the row that started it | reference |
| Colours and faces are looked up at draw time: changing a setting restyles every line already in the pane and scrollback | reference |

### 2.3 Own vs others

RULE A chat message is your own when its first row begins with: `<YourName> ` (MUD2 echoes your
say/shout/emote under your name), or `(<YourName ...>) ` (invisible form), or `You ` (`You shout
"..."`, `You tell your listeners "..."`), or `OK, ` + whitespace (`OK, Ann the heroine
waves.`). Name matching respects word boundaries (`Ann` does not match `Anne`). Anything else,
including `Bob the yeoman tells you "..."`, is another creature's.
source: reference
enforced_by: none: judgement

FACT Each row has a face for own messages and for others': any mix of italic, bold, dim.
source: reference

| Face | Drawn as | source |
|------|----------|--------|
| Italic | italic font | reference |
| Bold | synthetic weight (as 1.3); no bright promotion, the colour is exact | reference |
| Dim | each channel = round(0.60 x colour + 0.40 x #0C0C0C) | reference |

| Row | Own default | Others default | source |
|-----|-------------|----------------|--------|
| Emotes | none | none | reference |
| Say | italic | none | reference |
| Shout | italic | none | reference |
| Tell | italic | none | reference |

### 2.4 Themes

| Theme (`chattheme`) | Emotes | Say | Shout | Tell |
|---------------------|--------|-----|-------|------|
| Lamplight (default) | #BFA36A | #EDE6D0 | #FFA033 | #7FB8FF |
| Dusk | #D69A5C | #F4DDB5 | #FF8A3D | #A99CFF |
| Rose | #D4A04C | #F7E7B0 | #FF8A3D | #FF9EC7 |
| Tidewater | #D4A04C | #F7E7B0 | #FF8A3D | #7FB8FF |
| Bland | #CAB872 | #FAECA8 | #FFC474 | #D8E09C |

source: reference

| Editor rule | source |
|-------------|--------|
| Picking a theme loads its four colours and leaves the faces alone | reference |
| A colour edited after a pick is kept as a custom value; the theme is what Reset returns to | reference |
| Reset restores the selected theme's colours and the default faces | reference |
| A missing or malformed stored colour falls back to the selected theme's colour, per colour | reference |
| The editor shows live example lines for each row, own and other | reference |

FACT Chat settings are global (`[settings]` only); keys and value formats: `mucka-ini.md`,
`[settings]` only.
source: reference

### 2.5 Tell lines

RULE An inbound tell line (09 03, not starting `You tell your listeners`) has its sender's name
underlined; tapping it inserts `Name ` into the command box when the box is empty. `Someone` is
never a tappable sender.
source: reference
enforced_by: none: judgement

Whether this is one rule with `input.md`'s clicked-name prefill is open there (Box prefills, OPEN D6).

## 3. Status bar (scope: now)

### 3.1 Layout and chrome

| Element | Value | source |
|---------|-------|--------|
| Position | Top of the game view, full width, one 1 px white rule below it | reference |
| Background | #101618 | reference |
| Field labels (`Sta: ` etc.) | #58A6FF | reference |
| Current values | bold, in the field's colour | reference |
| `/max` | #808080, one point smaller than the value | reference |
| Separators | `|` in #444444 | reference |
| Menu button | U+2630 in #AAAAAA on a #2D333B rounded box, far left, at every width | reference |
| Wide order (left to right) | menu, Sta, Mag, Str, Dex, `|`, Score, `|`, effect cluster, spacer, rec chip, `|`, weather, `|`, reset countdown | reference |

RULE Every slot in the effect cluster has a fixed width. An icon appearing or going never moves
any other element; they behave like indicator lights.
source: reference
enforced_by: none: judgement

### 3.2 Fields and data sources

| Field | Shown as | Data source | Update rule | source |
|-------|----------|-------------|-------------|--------|
| Stamina | `Sta: 87/90` | FES fields 1, 2 (apparent, max) | Every FES reply; also inline code-89 readings (89 00 00 stamina, 89 00 01 max) | bartle:FE4 FES, code 89 |
| Magic | `Mag: 12/40` | FES fields 7, 8 (apparent, max) | Every FES reply. Hidden while current magic is 0 | bartle:FE4 FES; reference |
| Strength | `Str: 90/100` | FES fields 3, 4 (effective, max) | Every FES reply | bartle:FE4 FES |
| Dexterity | `Dex: 80/100` | FES fields 5, 6 (effective, max) | Every FES reply | bartle:FE4 FES |
| Score | `Score: 187325 (+120)` | FES field 9 (points); also code 89 01 and score/persona-saved lines | Every update. `-` while the score is 0 or unknown | bartle:FE4 FES, code 89 01; reference |
| Ailments | icons | FES fields 10-13 (blinded, deafened, crippled, dumbed: Y/N) | FES is authoritative for on/off | bartle:FE4 FES |
| Buffs/debuffs, glow | icons | Code 11 brackets around your own phrase (see 3.4) | On the code's line | reference |
| Weather | glyph [+ word] | FES field 15 (one letter) | Every FES reply that carries it; otherwise the last value stays | capture:dark-two-rats.c1 |
| Reset countdown | `28m` / `27:41` / `45s` | FES field 14 (minutes to reset), projected; code 06 04 | Redrawn at 1 Hz | bartle:FE4 FES, code 06 04 |
| Recording (`scope: later`) | `rec` chip: `later/session-transcript.md` | Local transcript state | On toggle | reference |

EVIDENCE FES on the wire carries a 15th field that Bartle does not document: a weather letter.
With the code bytes removed, replies read e.g. `... N N N N 45 R` (dark-two-rats.c1),
`... N N N N 4 T` (blind-vs-water-snake.c1), `... N N N N 14 F` (invisible-player-joins.c1).
source: capture:dark-two-rats.c1, capture:blind-vs-water-snake.c1, capture:invisible-player-joins.c1

DECISION FES field 15 is the weather letter, and it is optional: a reply without it is valid and
leaves the last weather shown. Bartle's FES list stops at field 14 and says nothing against a 15th;
three independent captures show it. The letter meanings are Clio's FES notes (`protocol.md`, FES
line), which outrank the reference; the reference's table in 3.3 agrees with them.
source: director (evidence ranking: Bartle silent, then captures, then Clio);
capture:dark-two-rats.c1, capture:blind-vs-water-snake.c1, capture:invisible-player-joins.c1;
clio:FES.md

### 3.3 Field formatting

Stat colours (Mag, Str, Dex) by ratio = current x 100 / max (integer):

| Ratio | Colour | source |
|-------|--------|--------|
| current <= 0 or max <= 0 | slot 10 #16C60C | clio:src/sscore.c, reference |
| >= 100 | slot 10 #16C60C | clio:src/sscore.c, reference |
| 76-99 | slot 2 #13A10E | clio:src/sscore.c, reference |
| 36-75 | slot 11 #F9F1A5 | clio:src/sscore.c, reference |
| 16-35 | slot 3 #C19C00 | clio:src/sscore.c, reference |
| 6-15 | slot 1 #C50F1F | clio:src/sscore.c, reference |
| 0-5 | slot 9 #E74856 | clio:src/sscore.c, reference |

RULE Stamina's colour is the server's: the first code-99 colour byte inside the FES reply. Slots
1, 2, 3, 9, 10, 11 are used as sent; anything else, or none, is slot 10.
source: clio:src/telnet.l; reference
enforced_by: none: judgement

Score colour and delta:

| Case | Score colour | Wide delta | Compact delta | source |
|------|--------------|------------|---------------|--------|
| No baseline yet | slot 3 #C19C00 | none | none | reference |
| Score = baseline | slot 3 | ` (+0)` | ` +0`, upright | reference |
| Score > baseline | slot 2 #13A10E | ` (+N)` | ` +N`, italic | reference |
| Score < baseline | slot 1 #C50F1F | ` (-N)` | ` -N`, italic | reference |

RULE The baseline is the first non-zero score seen for a character in this app session, keyed by
host + persona. Leaving to the option menu drops the live baseline; returning to the same
character on the same host resumes it. The same name on another host is another character.
source: reference
enforced_by: none: judgement

Weather (field 15):

| Letter | Wide text | Glyph | Colour | Tooltip | source |
|--------|-----------|-------|--------|---------|--------|
| F | Sunny | U+2600 U+FE0E | slot 11 | Fine weather | reference |
| C | Cloud | U+26C5 U+FE0E | slot 7 | Cloudy | reference |
| R | Rain | U+2602 U+FE0E | slot 2 | Raining | reference |
| S | Snow | U+2744 U+FE0E | slot 7 | Snowing | reference |
| O | Ocast | U+2601 U+FE0E | slot 8 | Overcast | reference |
| T | Storm | U+26A1 U+FE0E | slot 2 | Stormy | reference |
| B | Blizd | U+2603 U+FE0E | slot 7 | Blizzard | reference |
| other / none | hidden | | | | reference |

Reset countdown:

| Case | Text | source |
|------|------|--------|
| No projection, or it has lapsed | hidden | reference |
| Uncertainty <= 1.5 s, >= 60 s left | `m:ss` | reference |
| Uncertainty <= 1.5 s, < 60 s left | `Ns` | reference |
| Otherwise, >= 60 s left | floored whole minutes, `Nm` (matches the game's own "approximately N minutes") | reference |
| Otherwise, < 60 s left | `Ns` | reference |
| Tooltip | `Time until reset (+/-Ns)`, uncertainty rounded up | reference |
| Code 06 04 ("Auto reset initiated, you have 120 seconds") | exact anchor: 120 s from that line | bartle:FE4 code 06 04 |
| Back at the option menu or disconnected | hidden | reference |

open: Whether the server floors or rounds FES field 14 is unknown; the reference assumes both in
different places. Needs a timed capture.
default: assume it floors: field 14 = N means the reset is at least N and less than N+1 minutes
away. This matches the game's own "approximately N minutes" reading in the table above.

### 3.4 Effect icons

FACT Icons are 26 px discs from `assets/status/`. Tooltip = the game line that turned it on (a
fixed word if none was seen).
source: reference

| Slot | Icon | On when | Off when | source |
|------|------|---------|----------|--------|
| Stamina buff | stamina.svg | 11 02 with your phrase containing `fitter` or `fitness`; an 11 03 partial wear-off keeps it | 11 03 full wear-off (`Your magical ... has worn off`) | reference |
| Stamina debuff | stamina, debuff tint | phrase `less fit` / `unfitness` | full wear-off | reference |
| Strength buff / debuff | strength.svg | `stronger` / `weaker`, `strength` / `weakness` | full wear-off | reference |
| Dexterity buff / debuff | dexterity.svg | `more adroit` / `less adroit`, `dexterity` / `clumsiness` | full wear-off | reference |
| Glow | glow.svg | 11 00 with your phrase containing `glowing` | 11 01 with `glowing` | reference |
| Deaf, Blind, Dumb, Crippled | deaf / blind / dumb / crippled.svg | FES Y | FES N | bartle:FE4 FES |

| Rule | source |
|------|--------|
| Only your own phrases count: they begin `You `, `Your `, or `Some of your `. `The zombie1 is now less adroit!` does not | reference |
| Buff and debuff of one stat are independent; both can show. The buff sits behind, offset 5 px right; the debuff is in front; a 1 px vertical split only when both show | reference |
| `Some of your magical X has worn off` = still active; `Your magical X has worn off` = cleared. Stack depth is not tracked | reference |
| The debuff icon is the stat icon desaturated to luma and tinted (r 0.58, g 0.34, b 0.52), so a lone debuff is not mistaken for a buff. `assets/status/` holds only the base art | reference |
| `invis.svg` is shipped but drawn nowhere | reference |
| Compact layout: ailments as glyphs joined by spaces: deaf U+1F442, blind U+1F441 U+FE0F, dumb U+1F444, crippled U+267F; buffs/glow not shown | reference |
| Order in the wide cluster: sta, str, dex, divider, glow, combat, divider, deaf, blind, dumb, crippled | reference |

open: The wide cluster also has a combat icon (combat.svg, crossed blades) lit while a fight is
believed open. It depends on combat tracking, which belongs to the later combat rail. Now or
later? Listed as later (section 8, `later/README.md`) pending this.
default: `scope: later` (`later/combat-panel.md`). In the now scope the wide cluster has no combat
slot.

### 3.5 Recording chip

FACT Recording chip and the session transcript it toggles: `scope: later`,
`later/session-transcript.md`.
source: director

### 3.6 Effective columns and narrow widths

Effective columns, and re-sending the width when they change: `connection.md` 4 and 4.1.

| Effective columns | Behaviour | source |
|-------------------|-----------|--------|
| >= 80 | Wide, one row. Weather shows glyph + word | reference |
| 76-79 | Wide, one row. Weather glyph only | reference |
| < 76 | Compact, two rows: a label row (`Sta Mag Str Dex Score Wth Rst`, 11 pt, #58A6FF) over a value row. Score at one point below the values, delta one point smaller again. No `Score:` label, no magnifier. Rec chip becomes `(R)` right-aligned in the spare space | reference |
| < 50 | Values 12 pt (13 pt otherwise); `/max` 2 pt below the value. `/max` hidden for any stat whose max is 100 or more | reference |

## 4. Dreamword (scope: now)

### 4.1 Capture

| Event | Effect | source |
|-------|--------|--------|
| Code 15 00 00 followed immediately by 1-14 letters a-z | dreamword set to those letters; they are also printed in the text (code colour) | bartle:FE4 code 15 00 00; reference |
| Code 15 00 01 | dreamword cleared | reference |
| Your own persona's line `<you> says "<word>"` with the exact current word (invisible form included) | cleared: speaking it uses it, even if the server sends no clear | reference |
| World reset lands (06 06 corroborated by the countdown), or the session ends by reset | cleared | reference |
| Login banner names a different world (host, port, reset number) | cleared | reference |
| Login banner names the world the word was captured in | the held word is restored, across reconnects | reference |
| Outside game mode only: text `(gasps|whispers|shouts|screams|hisses|murmurs) "<a-z, 1-14>"` | dreamword set | reference |

FACT Bartle names 15 00 00 as the dream word appearing in a dream, and 15 00 01 as the effect of
the dream word having been said.
source: bartle:FE4 code 15
open: The reference treats 15 00 01 as "cleared". Bartle's wording fits (someone said it). Confirm
with a capture. The pre-game text pattern has no capture behind it either.
default: build the table above as written: 15 00 01 clears the dreamword, and the pre-game text
pattern sets it.

### 4.2 Pill

| Property | Value | source |
|----------|-------|--------|
| Visible | only while a dreamword is held | reference |
| Position | floating over the terminal, top-right, 6 px from the top, 8 px from the right | reference |
| Box | #182A3E at 90% opacity, 2 px #FFD700 border, corner radius 10 | reference |
| Text | `"word"` in #00E5FF, the word bold, quotes regular | reference |
| Size | 13 pt (12 pt below 50 effective columns) + offset; offset is a setting, -2 to +4, default 0 (`dreamwordsizeoffset`) | reference |
| Tap | speak it: sends `"word"` (a say) through the ordered send channel; logs `dreamword spoken: word` to the wire log; focus returns to the command box | reference |

RULE Speaking the dreamword from the pill or Ctrl+D goes through the one ordered send channel and
flushes nothing out of order (Invariant #1). Ctrl+Shift+D (speak, then the typed command or
`sleep`) is specified in `keys.toml`.
source: director
enforced_by: none: judgement

## 5. Window title (scope: now)

| State | Title | source |
|-------|-------|--------|
| App start | `mucka <version>`, or the profile name when started for a profile | reference |
| Connected, at the option menu | `<profile> mucka <version>` | reference |
| Character identified | `<character>@<profile> mucka <version>` | reference |

open: product string in the title for MuckaII (`mucka`, `muckaii`, `MuckaII`)?
default: `muckaii`, in the reference's lower-case form: `<character>@<profile> muckaii <version>`.

## 6. Keep screen on (scope: later)

DECISION Keep screen on is `scope: later`: `later/keep-screen-on.md`.
source: director

## 7. Settings for now-scope UI features

| Setting | Where | Range | Default | `mucka.ini` key | source |
|---------|-------|-------|---------|-----------------|--------|
| Font size | Settings | 9-24 px | 15 (stored 0 = default) | `fontsize` | reference |
| Volume (master) | Settings / Sounds | 0-100 | 75 | `volume` | reference |
| Stat update interval (FES heartbeat) | Settings | off, or 5-30 s in steps of 5 | 10 s | `statupdate` | reference |
| Mute beep this session | Settings | on/off | off | not stored | reference |
| Mute beep permanently | Settings | on/off | off | `mutebeep` | reference |
| Save settings to this profile only | Settings | on/off | off | none of its own: MuckaII writes the edited keys to the profile layer and the rest fall through (`persistence.md`, Settings storage). Imported from the existence of `[settings:<profile>]` (`mucka-ini.md`) | reference, director |
| Default font size (all profiles) | Display | 9-24 | 15 | `defaultfontsize` | reference |
| Max columns | Display (global layer; a profile may override) | unset, or 20-160 | unset: min(80, fits) (`connection.md` 4) | `columns`; how its two forms import: `mucka-ini.md` | director |
| Dreamword size offset | Display | -2 to +4 | 0 | `dreamwordsizeoffset` | reference |
| Chat theme, colours, faces | Display | see 2.4 | Lamplight, faces 2.3 | see 2.4 | reference |
| Sounds: master, groups, rows, volumes, fallbacks, bell | Sounds | see `sounds.toml` | all on | see `sounds.toml` | reference |
| Keep screen on (`scope: later`: `later/keep-screen-on.md`) | Connect page, per profile | on/off | per platform | `keepscreenon` | reference |

DECISION MuckaII's settings are reorganised (`flow.md`, Settings) and have no Friends tab
(`fair-play.md`, Other players). Rejected: Mucka's tab layout.
source: director

DECISION The settings dialog has the buttons Apply, Save and Save as default, acting as `flow.md`
(Settings) states, plus Cancel, which closes the dialog without applying the edits.
source: director

DECISION In the now scope the menu (U+2630) holds Settings and About. Every other entry the
reference's menu had (Log Recording, Score, Orientation, Side Panel, Onlines, Compass, Combat,
Stats, Damage floats) belongs to a later feature (section 8; Orientation with Android).
source: director; agent

## 8. Later (scope: later)

FACT Later UI features (combat rail, metronome, edge and status icon; side panel and floating
widgets; radar compass; score graph; recording chip; drop overlay; keep screen on; watchwords; chat
mode) are indexed in `later/README.md`. Managed persona selection and the raw console are
`scope: now` (`flow.md`, Pre-game; `input.md`, Raw console); the Friends tab is rejected (`fair-play.md`).
source: director
