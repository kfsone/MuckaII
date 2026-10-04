# Later: side panel and floating widgets

FACT `scope: later`. The side panel is one of the five primary components of the game screen
(`ui.md` section 0): optional, on the left, with sub-widgets players online (text list), items
carried (text list), items here (text list with a button per item), and navigation (compass rose
plus the last 3 locations).
source: director

FACT The navigation widget is meant to sit just left of the command input, so the input reads a
little like a shell prompt (`~w$` in a PS1).
source: director

RULE The players-online widget and the navigation widget each have a detach button. Detaching one
shows a floating copy that can be dragged around inside the main window. The docked and floating
copies can both be visible (for example the compass in the side panel and a second copy beside the
combat panel). Rejected: Mucka collapsing the docked copy.
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

| Feature | What the user saw in Mucka | source |
|---------|----------------------------|--------|
| Side panel | Left panel with Online (who list, recent departures), Inventory, Items here, Map/compass; sections can float | reference |

DECISION Players-list entries are never clickable as a probe of whether a name is still in the
game's vocabulary (`fair-play.md`, Other players). Rejected: Mucka's offline-entry probe.
source: director

FACT Settings keys (`showonline`, `showinventory`, `showitemshere`, `showmapcompass`,
`maxonlinedisplay`, `onlinenamesonly`, `onlineforgetwindow`, `floatonline`, `floatcompass`) are in
`mucka-ini.md`; probe rows (FEW and FEI in the beat) in `later/probe-cadence.md`; the compass in
`later/compass.md`.
source: agent
