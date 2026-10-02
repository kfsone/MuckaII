# Later: side panel and floating widgets

FACT `scope: later`. The side panel is one of the five primary components of the game screen
(`ui.md` section 0):

| Component | Scope | Contents |
|-----------|-------|----------|
| Side panel | later | Optional, on the left, with sub-widgets: players online (text list), items carried (text list), items here (text list with a button per item), and navigation (compass rose plus the last 3 locations). |

source: director

FACT The navigation widget is meant to sit just left of the command input, so the input reads a
little like a shell prompt (`~w$` in a PS1).
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

RULE What game mode displays by default depends on the window or device width. On the phone, or
when the user sets a low maximum column count (44, say), the side panels are off by default. Small
expand and collapse controls (`>>`, `<<`) at the bottom of the status bar show and hide them, as
well as their menu options. The width thresholds belong to `ui.md` section 3.6.
source: director
enforced_by: none: judgement

FACT What the user saw of Mucka's side panel:

| Feature | What the user sees | source |
|---------|--------------------|--------|
| Side panel | Left panel with Online (who list, recent departures), Inventory, Items here, Map/compass; sections can float | reference |

DECISION Players-list entries are never clickable as a probe of whether a name is still in the
game's vocabulary. Mucka's click on an offline entry reveals invisible players
(`fair-play.md`, Other players). Rejected: Mucka's offline-entry probe.
source: director

FACT The side panel's settings keys (`showonline`, `showinventory`, `showitemshere`,
`showmapcompass`, `maxonlinedisplay`, `onlinenamesonly`, `onlineforgetwindow`, `floatonline`,
`floatcompass`) are in `mucka-ini.md`. Its probe rows (FEW and FEI in the beat) are in
`later/probe-cadence.md`. The compass is in `later/compass.md`.
source: director
