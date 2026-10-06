# Later: chat mode

FACT This file is only the chat-only view filter. Chat colouring is `scope: now` (`ui.md` section 2).
source: director

RULE A latching Chat toggle filters the terminal to chat lines only (lines the server marks as a
speaker message: shout, say, tell, emote; see `protocol.md`). It is a view filter only: typing,
Enter, hotkeys and everything sent are unchanged, and nothing is prefixed to outgoing lines.
enforced_by: none: judgement
source: reference

RULE Chat mode behaves as follows.
enforced_by: none: judgement
source: reference

| Aspect | Behaviour |
|---|---|
| Turning on | The terminal repaints from a chat-only history, up to 3000 lines deep, so older chat survives. |
| Turning off | The terminal repaints the full history as it would stand had the filter never been on. |
| Cues while on | Box placeholder `chat...` (otherwise `enter command...`); the box frame turns yellow; the toggle is lit and flashes amber when non-chat output arrives. |
| Ends | When game mode ends and on disconnect. |
| Focus | Toggling returns focus to the box. |
source: reference

open: Scope of chat mode (not on the `INTERNAL.md` now list).
default: later.

FACT The session transcript ignores the chat filter (`later/session-transcript.md`).
source: reference
