# Later: combat panel and combat rail

FACT `scope: later`. The combat panel is one of the five primary components of the game screen
(`ui.md` section 0). It is optional, and it is one vertical stack, top to bottom:

| Part | Share of height | Contents |
|------|-----------------|----------|
| Outcomes | about 0.1 | A list of combat outcomes |
| Badges | about 0.9 | One creature badge per opponent |
| Tick | fixed | The combat ticker |
| Player | rest | The player's own creature badge |

source: director

FACT What the user saw of Mucka's combat features:

| Feature | What the user sees | source |
|---------|--------------------|--------|
| Combat rail | A right-edge panel during fights: foes, their state, your stats per tick, weapon, flee button, damage floats | reference |
| Combat metronome | Two clicks (high and low percussion) bracketing each combat tick, switched from the rail | reference |
| Combat edge | A 1 px border on the terminal: amber during a fight, red at danger | reference |
| Combat status icon | Crossed-blades icon in the status bar while a fight is open | reference |

FACT The wide status-bar cluster in the reference also has a combat icon (`combat.svg`, crossed
blades) lit while a fight is believed open. It depends on combat tracking (`ui.md` 3.4).
source: reference

FACT Related entries elsewhere: Ctrl+W wields the rail's alternate weapon (`keys.toml`, scope later);
the metronome's playback rule is `metronome-book-ends` and its clicks are `Perc_Stick_hi.wav` and
`Perc_Stick_lo.wav` (`sounds.toml`); the inventory-in-combat probe is in `later/probe-cadence.md`;
the rail's settings keys `showcombatrail` and `showcombatstats` are in `mucka-ini.md`.
source: director
