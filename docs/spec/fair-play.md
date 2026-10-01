# Fair play

FACT MUD2 is real-time (by the standards of 40 years ago), asynchronous, multi-player, PvE plus PvP,
and built from narrative and text. MuckaII keeps the player's eyes on the scroll and their hands on
the input box. Its features are *secretaries standing in the wings*: low-cognitive-load
presentation of information that might otherwise fall out of short-term memory. MuckaII does not
bejewel or bedazzle, does not distract the player from reading what is going on, and does not give
players super powers.
source: director

RULE This file outranks every other spec. Each feature spec names the line here that permits the
feature. A feature that cannot name one goes to the director before it is designed.
source: director
enforced_by: none: judgement

The lines below follow the director's wording, examples included. The examples are what make the
grey areas decidable, so they are never paraphrased away.

## Movement and places

RULE MUST NOT give players a way to click on a room name and "go there". That would be
sacrilegious to the spirit of MUD2's PvP.
source: director
enforced_by: none: judgement

RULE MUST NOT provide a way to tell apart different rooms that share a name (Dense Forest,
Passageway, Dally Lane, ...).
source: director
enforced_by: none: judgement

RULE CAN show a mouse-over tooltip on the compass naming the destination room.
source: director
enforced_by: none: judgement

RULE MUST NOT provide a way to scan rooms automatically and move towards something. For example,
`$SEARCH valuable23` running `qn` after every move and, on seeing `valuable23`, inserting the
appropriate `nw,g valuable23`. We will not do this.
source: director
enforced_by: none: judgement

## Objects

RULE CAN track the last places an object was seen listed in the FEI list. The tracking may need to
grade "certain" against "possible". Example: the player had an object at Entrance to Mine, then
went east four times into the dark, and after that the object was no longer in the FEI list. We
last saw it in a lit part of the Mine, but we do not know that is the last place we *had* it.
source: director
enforced_by: none: judgement

RULE CAN give players a `$LASTSAW <identity>` command that tells them where an object was last
seen, but not how to get there.
source: director
enforced_by: none: judgement

RULE CAN capture the output of `qn` commands into a command like `$LASTSAW`.
source: director
enforced_by: none: judgement

RULE CAN add a `$FETCH <identity>` command. After a `qn` command, it takes the player in that
direction and tries to get the item. The player starts it.
source: director
enforced_by: none: judgement

## Output

RULE CANNOT automatically generate assistive output in response to MUD commands. For example, it
would be wrong to modify the `qn` or `exits` output to show items we think we know about. It would
also be wrong to highlight an exit in any way that could be used for guided pathing
(`ne: passageway *GO THIS WAY*`, or highlighting it somehow).
source: director
enforced_by: none: judgement

RULE CAN reformat the output of a command without materially altering it, in the manner of
PowerShell formatters. For example, the `qn` and `look around` output is poorly formatted,
presented and ordered.
source: director
enforced_by: none: judgement

## Other players

RULE CANNOT do things that give players an extreme PvP advantage. Going invisible removes a player
from the who and FEW lists and from other telltales such as `value`. Mucka 1 lets you click an
offline entry in the players list and tests whether MUD2 still recognises the player's name. An
offline player's name is no longer in the live game's vocabulary, and an invisible player causes
the parse to abort with no error, which can be probed. Mucka 2 does not allow this.
source: director
enforced_by: none: judgement

DECISION There is no friends list and no friends settings tab. A friends list can be exploited as
an enemies list that warns you when a particular person logs in. "But you can see them on the
player list" does not hold, because seeing them is not a given: MuckaII deliberately tries not to
be distracting. Rejected: a friends list (Mucka has one).
source: director

## Automation

RULE CAN do things that affect or modify how a player responds to PvP. Hotkeys are acceptable: most
clients have them, and someone playing via raw telnet is an idiot.
source: director
enforced_by: none: judgement

RULE CANNOT trivially automate combat sequences. The `^N` slots and `$x` aliases can be used to make
an in-the-moment set of combat responses, but they are never saved or stored. Building one is a
high cognitive load and equates more to styling than to capability.
source: director
enforced_by: none: judgement

RULE CANNOT make it trivial for the player to automate significant portions of gameplay.
Location-specific hotkeys or actions might be tolerable. Once they become conditional or dynamic,
they move into, or beyond, a grey area.
source: director
enforced_by: none: judgement

RULE After the persona has finished arriving in the tea room, MuckaII never does anything
gameplay-affecting automatically. It always requires a manual input. A command the client injects
on its own initiative is a read-only probe (FES, FEW, FEI, FEX) and never changes game state. A
command that changes game state is sent only because the player typed it, pressed its key, or
started it (`$FETCH`). Injection sites are in `session.md`.
source: director
enforced_by: none: judgement

RULE Clicking a compass exit sends that direction, exactly as typing it would, and counts as a
hotkey. The compass may also show a tooltip naming the destination room. Nothing, including that
tooltip, ever lets the player click a room *name* to move.
source: director
enforced_by: none: judgement

open: Watchwords answer configured triggers in incoming text, which makes them conditional. They
need a charter review before they are built.
