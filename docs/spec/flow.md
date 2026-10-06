# Flow: profile, pre-game, game, tombstone

FACT The client is always in exactly one of these states:

| State | Entered when | Left when | Shows | Scope |
|-------|--------------|-----------|-------|-------|
| Profile | the app starts; the user disconnects | a profile is connected (to Pre-game) | the profile list, global settings, the score graph | now (the score graph is later) |
| Pre-game | the connection is made; the game ends without a death; the Tombstone is dismissed | game mode is entered (to Game); disconnect (to Profile) | the game layout with its widgets disabled, plus the persona dialog when managed selection is on | now |
| Game | game mode is entered after the persona arrives in the tea room | the server returns to the Option menu (to Pre-game, or to Tombstone after a death) | the full game layout (`ui.md` section 0) | now |
| Tombstone | the game ended by death | the user dismisses it (to Pre-game) | how the persona died, and the last lines of the scroll | now |

source: director

| Feature | Scope |
|---------|-------|
| Profile list, profile page, per-profile advanced settings | `scope: now` |
| Global settings, Apply / Save / Save as default | `scope: now` |
| Score graph, and every way into it (Profile state, the Tombstone's name link) | `scope: later`: `later/score-graph.md` |
| Managed persona selection, and creating a persona in an open slot | `scope: now` |
| Persona deletion | `scope: later`: `later/persona-deletion.md` |
| Detecting the return to the Option menu | `scope: now` |
| Tombstone screen | `scope: now` |
| Telling the player that a setting's default changed | `scope: later`: `later/setting-default-change.md` |

source: director

The scope list itself is `INTERNAL.md`, Scope; these tags mark where each item lives and follow it.

## Profile

RULE The profile list is a manageable list ordered most recently used first. A profile combines a
server, an account and an optional persona, along with that profile's settings.
source: director
enforced_by: none: judgement

RULE By default a profile shows the server, port, MUD account and password, plus a checkbox for
managed persona selection. A way into that profile's advanced settings is also shown, so they can
be changed without logging in.
source: director
enforced_by: none: judgement

RULE The Profile state also gives access to the global settings. Its way into the score graph is
`scope: later` (`later/score-graph.md`).
source: director
enforced_by: none: judgement

## Pre-game

RULE Pre-game shows the game layout with its widgets disabled, so the player sees the game's banner,
splash and so on behind any dialog. With managed persona selection on, the input box is not shown.
source: director
enforced_by: none: judgement

RULE With managed persona selection on and no persona specified, a dialog in front of the login
process lists the available personas, most recently used first. If you play as slot 2 and then
`qq`, slot 2 is at the top the next time the list appears. The dialog also offers:
- adding a persona, when a slot is empty;
- "drop to menu". The MUD shell that persona selection drives is only a prompt system, so this
  falls through to the mode without persona selection.

source: director
enforced_by: none: judgement

RULE When a persona is specified, the client logs it in and enters game mode when it arrives in
the tea room.
source: director
enforced_by: none: judgement

RULE The now scope includes the ability to *create* a persona in an open slot. Persona deletion is
`scope: later` (`later/persona-deletion.md`).
source: director
enforced_by: none: judgement

FACT The persona dialog is a real input surface. While it is open it owns focus, which is an
Invariant #0 exception.
source: director

## Game

RULE The two god-priority features are the text input box and the text scroll in the middle.
Everything else gives way to them.
source: director
enforced_by: none: judgement

FACT How the side panels show by default at each width is `scope: later`, with the side panel:
`later/side-panel.md`.
source: director

RULE Game mode ends when the client detects a return to the Option menu.
source: director
enforced_by: none: judgement

How the server signals that return is `scope: now` and open: `protocol.md` "Game mode" (OPEN A5).

## Tombstone

RULE When the game ends by death, the player gets a chance to see what happened: the last several
lines of the scroll, styled according to what happened. A death is either permanent (the persona
is not updated) or temporary (for example, carrying a brand into the swamp).
source: director
enforced_by: none: judgement

RULE A permadeath adds an R.I.P. visual:
- the persona's name, which links to the score graph;
- a sub-heading with its last rank from the online list;
- a rule, then the last N lines of the scroll.

Clicking it opens a full-window version of the scroll, so the player can go back and see in more
detail how they goofed.
source: director
enforced_by: none: judgement

DECISION Until the score graph exists (`scope: later`), the name on the R.I.P. visual is inert:
it is shown but is not a link.
source: director

FACT These are known temporary deaths:
- taking a wrong turn in the sea and capsizing;
- leaving Dragon Island or the Isle of Woe in the wrong direction and drowning;
- capsizing when the weather changes at sea;
- jumping off the cliff without an open parachute;
- walking into the swamp carrying a flame.

source: director

open: The text that marks each temporary death, and the permadeath code (C08 C13 in
`connection.md`), still have to be confirmed against the wire corpus.
default: classify by the `connection.md` 1.2 landmarks: `08 13` is a permadeath, and a Died
landmark without `08 13` is a temporary death. No per-cause text is matched until it is mined.

## Settings

RULE The settings UI is reorganised and compact. The sound catalogue and volume management are
redesigned, not copied from Mucka.
source: director
enforced_by: none: judgement

RULE The settings buttons:
- **Apply** updates the current settings.
- **Save** applies them and saves them.
- **Save** has a drop-down that saves the settings as the default.

source: director
enforced_by: none: judgement

DECISION The buttons act on the layers in `persistence.md` "Settings storage":

| Button | Writes |
|--------|--------|
| Apply | nothing to disk. The edited values take effect for this run |
| Save (opened from a profile) | the edited keys to that profile's layer |
| Save (opened from global settings) | the edited keys to the global layer |
| Save as default | the edited values to the global layer, so every profile that does not set those keys itself takes them. Profile-layer values are left alone |
| Cancel | nothing. The dialog closes without applying the edits |

source: director

RULE The settings UI marks a setting whose value equals its built-in default.
source: director
enforced_by: none: judgement
