# UI: terminal look, chat colours, status bar, dreamword

FACT This file says what the launch UI looks like and how it updates, as observable behaviour.
Sounds are in `sounds.toml`. Per-code text colours are in `c1-codes.toml`. Key chords (Ctrl+D,
Ctrl+Shift+D) are in `keys.toml`. The FES heartbeat, reset probing and width negotiation are in
`connection.md`. Scrollback, selection and the F-key bar are in `input.md`.
source: director

RULE Every tap or click target in this file (dreamword pill, rec chip, score, menu rows) hands
keyboard focus back to the command input when it is done (Invariant #0).
source: director
enforced_by: none: judgement

RULE Glyphs are given as code points. The repo is ASCII; the program draws the character.
source: director
enforced_by: none: judgement

## 0. Game screen layout

FACT The in-game screen has five primary components:

| Component | Scope | Contents |
|-----------|-------|----------|
| Stats bar | launch | Runs along the top. Holds the status fields (section 3) and the settings menu, shown as a hamburger button. |
| Side panel | later | Optional, on the left, with sub-widgets: players online (text list), items carried (text list), items here (text list with a button per item), and navigation (compass rose plus the last 3 locations). |
| Terminal scroll | launch | The main component. Takes all the space the others leave. |
| Combat panel | later | Optional. See the combat panel table below. |
| Input box | launch | The command input, along the bottom. |

source: director

FACT The navigation widget is meant to sit just left of the command input, so the input reads a
little like a shell prompt (`~w$` in a PS1).
source: director

FACT The combat panel is one vertical stack, top to bottom:

| Part | Share of height | Contents |
|------|-----------------|----------|
| Outcomes | about 0.1 | A list of combat outcomes |
| Badges | about 0.9 | One creature badge per opponent |
| Tick | fixed | The combat ticker |
| Player | rest | The player's own creature badge |

source: director

RULE The players-online widget and the navigation widget each have a detach button. Detaching one
shows a floating copy that can be dragged around inside the main window. Both the docked copy and
the floating copy can be visible at the same time. This is a deliberate departure from Mucka, which
collapses the docked copy. For example, a player planning to run can keep the compass in the side
panel and drag a second copy next to the combat panel.
source: director
enforced_by: none: judgement

RULE Floating panels live inside the main window and are never separate OS windows, so they behave
the same on Android.
source: director
enforced_by: none: judgement

## 1. Terminal look (scope: launch)

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

FACT MUD2 sends only SGR colour and clear-screen, no cursor positioning.
source: director

## 2. Chat colouring (scope: launch)

open: `mucka-ini.md` marks the chat theme, colour and face keys `later`, since chat colouring is not
on the `INTERNAL.md` launch list; this section says launch. Which?

### 2.1 What counts as a chat part

| Part | Codes | Gets the colour of row |
|------|-------|------------------------|
| Speaker label and act/emote text | 09, 09 00, 09 04 .. 09 10 | Emotes |
| Shouted words (shout, yell, yodel, scream, holler, cheer) | 09 01 | Shout |
| Said words (say, whisper) | 09 02 | Say |
| Told words, both directions | 09 03 | Tell |

source: bartle:FE4 code 09; reference (which wordings use which subcode is the reference's
reading of its captures)

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

FACT Chat settings are global (`[settings]` only): `chattheme`, `emotecolor`, `saycolor`,
`shoutcolor`, `tellcolor` (6 hex digits, optional `#`), and faces `emoteown`, `emoteother`,
`sayown`, `sayother`, `shoutown`, `shoutother`, `tellown`, `tellother` (comma list of `italic`,
`bold`, `dim`, or `none`; anything else = default).
source: reference

### 2.5 Tell lines

RULE An inbound tell line (09 03, not starting `You tell your listeners`) has its sender's name
underlined; tapping it inserts `Name ` into the command box when the box is empty. `Someone` is
never a tappable sender.
source: reference
enforced_by: none: judgement

open: `input.md` (Box prefills) prepends a clicked player name to typed text whenever the caret is
at the start; this rule inserts only into an empty box. One rule or two?

## 3. Status bar (scope: launch)

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
| Recording | `rec` chip | Local transcript state | On toggle | reference |

EVIDENCE FES on the wire carries a 15th field that Bartle does not document: a weather letter.
With the code bytes removed, replies read e.g. `... N N N N 45 R` (dark-two-rats.c1),
`... N N N N 4 T` (blind-vs-water-snake.c1), `... N N N N 14 F` (invisible-player-joins.c1).
source: capture:dark-two-rats.c1, capture:blind-vs-water-snake.c1, capture:invisible-player-joins.c1
open: Bartle's FES list stops at "minutes to next reset". Treat field 15 as optional; the letter
meanings come from Clio's FES notes (`protocol.md`, FES line) and the reference (table 3.3).

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
believed open. It depends on combat tracking, which belongs to the later combat rail. Launch or
later? Listed as later in section 8 pending this.

### 3.5 Recording chip

| State | Wide | Compact | source |
|-------|------|---------|--------|
| Idle | hollow chip, #2D333B border, `rec` in #666666 | `(R)` in #666666 | reference |
| Recording | #C02020 border, U+25CF in #E74856, `rec` in white | `(R)` in #E74856 | reference |
| Tap | start or stop the plain-text transcript of the screen | same | reference |

FACT The same toggle is in the menu as "Log Recording". A `-record` command-line flag arms it on
first entering the game. The transcript is separate from the wire log, which is always on.
source: reference
open: `INTERNAL.md` lists the wire log at launch, not a transcript. Is the transcript (and so this
chip) launch or later?

### 3.6 Effective columns and narrow widths

FACT Effective columns = clamp(min(max-columns setting, columns that fit), 20, 160). With
max-columns = 0 (auto) it is the columns that fit. Before the first measurement it is the setting,
or 40 on auto (`connection.md`, section 4).
source: reference

| Effective columns | Behaviour | source |
|-------------------|-----------|--------|
| >= 80 | Wide, one row. Weather shows glyph + word | reference |
| 76-79 | Wide, one row. Weather glyph only | reference |
| < 76 | Compact, two rows: a label row (`Sta Mag Str Dex Score Wth Rst`, 11 pt, #58A6FF) over a value row. Score at one point below the values, delta one point smaller again. No `Score:` label, no magnifier. Rec chip becomes `(R)` right-aligned in the spare space | reference |
| < 50 | Values 12 pt (13 pt otherwise); `/max` 2 pt below the value. `/max` hidden for any stat whose max is 100 or more | reference |

RULE A width change that changes effective columns re-sends the terminal width to the server so
new output wraps at the new width (see `connection.md`).
source: reference
enforced_by: none: judgement

## 4. Dreamword (scope: launch)

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

## 5. Window title (scope: launch)

| State | Title | source |
|-------|-------|--------|
| App start | `mucka <version>`, or the profile name when launched for a profile | reference |
| Connected, at the option menu | `<profile> mucka <version>` | reference |
| Character identified | `<character>@<profile> mucka <version>` | reference |

open: product string in the title for MuckaII (`mucka`, `muckaii`, `MuckaII`)?

## 6. Keep screen on (scope: launch)

| Rule | source |
|------|--------|
| Per-profile setting "Keep screen on", on the connect page; `[profile:<name>] keepscreenon=yes|no` | reference |
| Default on for Android (and iOS), off on desktop | reference |
| Applied while the game view is showing; released when it is left. A settings dialog over the game view does not release it | reference |

FACT The need it serves: an idle screen-lock mid-session gets the player killed.
source: reference

open: `mucka-ini.md` asks whether `keepscreenon` is launch; it is not on the `INTERNAL.md` launch
list. This section says launch.

## 7. Settings for launch UI features

| Setting | Where | Range | Default | `mucka.ini` key | source |
|---------|-------|-------|---------|-----------------|--------|
| Font size | Settings | 9-24 px | 15 (stored 0 = default) | `fontsize` | reference |
| Max columns | Settings | auto, or 20-160 | auto | `columns` | reference |
| Volume (master) | Settings / Sounds | 0-100 | 75 | `volume` | reference |
| Stat update interval (FES heartbeat) | Settings | off, or 5-30 s in steps of 5 | 10 s | `statupdate` | reference |
| Mute beep this session | Settings | on/off | off | not stored | reference |
| Mute beep permanently | Settings | on/off | off | `mutebeep` | reference |
| Save settings to this profile only | Settings | on/off | off | `[settings:<profile>]` section exists | reference |
| Default font size (all profiles) | Display | 9-24 | 15 | `defaultfontsize` | reference |
| Default max columns (all profiles) | Display | 0 (auto) - 160 | 0 | `defaultcolumns`: stored only, nothing reads it (`mucka-ini.md`, scope later) | reference |
| Dreamword size offset | Display | -2 to +4 | 0 | `dreamwordsizeoffset` | reference |
| Chat theme, colours, faces | Display | see 2.4 | Lamplight, faces 2.3 | see 2.4 | reference |
| Sounds: master, groups, rows, volumes, fallbacks, bell | Sounds | see `sounds.toml` | all on | see `sounds.toml` | reference |
| Keep screen on | Connect page, per profile | on/off | platform (6) | `keepscreenon` | reference |

FACT Mucka's settings dialog has the tabs Settings, Display, Hotkeys, Sounds and Friends. Apply
previews changes live (e.g. chat colours restyle the pane); Save writes `mucka.ini`; Cancel
reverts.
source: reference

DECISION MuckaII's settings are reorganised (`flow.md`, Settings) and have no Friends tab
(`fair-play.md`, Other players). Rejected: Mucka's tab layout.
source: director

FACT The menu (U+2630) holds: Settings, About, Log Recording, Score, Orientation (Portrait /
Landscape / System), Side Panel, Onlines, Compass, Combat, Stats, Damage floats. Only Settings,
About, Log Recording and Orientation serve launch features.
source: reference

## 8. Later (scope: later)

FACT Recorded so they are not lost. No design here; each is re-justified before it is built.
source: director

| Feature | What the user sees | source |
|---------|--------------------|--------|
| Combat rail | A right-edge panel during fights: foes, their state, your stats per tick, weapon, flee button, damage floats | reference |
| Combat metronome | Two clicks (high and low percussion) bracketing each combat tick, switched from the rail | reference |
| Combat edge | A 1 px border on the terminal: amber during a fight, red at danger | reference |
| Combat status icon | Crossed-blades icon in the status bar while a fight is open | reference |
| Side panel | Left panel with Online (who list, recent departures), Inventory, Items here, Map/compass; sections can float | reference |

DECISION Players-list entries are never clickable as a probe of whether a name is still in the
game's vocabulary. Mucka's click on an offline entry reveals invisible players
(`fair-play.md`, Other players). Rejected: Mucka's offline-entry probe.
source: director
| Radar compass | A dial of the room's exits (from FEX); clicking an exit moves you | reference |

DECISION Compass exits stay clickable. A click sends the direction like a hotkey, and the
destination tooltip is allowed. A room name is never clickable (`fair-play.md`, Movement and
places).
source: director
| Score graph | `$SCORE` / tapping the score opens a graph of score over the session, with a skull U+2620 at a persona wipe | reference |
| Score tap target | Tapping the score (with a magnifier U+1F50D) opens the graph | reference |
| Friends tab | A settings tab for a friends list | reference |
| Guided login | Persona picker overlay that drives the login for you | reference |
| Raw console | A view of the raw byte stream | reference |
| Watchwords | User text triggers from `[watch]` in `mucka.ini` | reference |
