# Later: keep screen on

DECISION Keep screen on is `scope: later`, with anti-idle keepalive (`later/anti-idle.md`).
source: director

| Rule | source |
|------|--------|
| Per-profile setting "Keep screen on", on the connect page; `[profile:<name>] keepscreenon=yes|no` | reference |
| Default on for Android (and iOS), off on desktop | reference |
| Applied while the game view is showing; released when it is left. A settings dialog over the game view does not release it | reference |

FACT Need served: an idle screen-lock mid-session gets the player killed.
source: reference
