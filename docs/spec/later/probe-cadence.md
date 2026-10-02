# Later: probe cadence for later features

FACT `scope: later`. These rows of the probe cadence (`connection.md` 5.2) serve the side panel, the
combat rail and reset-time sampling. The launch rows stay in `connection.md`.
source: director

| Probe | When | Default | Scope | Source |
|-------|------|---------|-------|--------|
| FEW in the beat | every beat while the Online panel is shown | - | later | reference |
| FEI in the beat | only when inventory/room items are marked stale and either panel is shown | - | later | reference |
| Inventory (combat) | 100 ms after the last inventory-change line, moved past the next combat tick if within 200 ms of it. It may ride in front of the next typed command in the same write. | 100 ms | later | reference |
| Room-entry FEX | 1750 ms after a room description if no FEX list came | 1750 ms | later | reference |
| Reset-time sampling | FES-only probes, self-paced at about 501 ms or more, in a one-time burst to pin the reset instant | - | later | reference |
