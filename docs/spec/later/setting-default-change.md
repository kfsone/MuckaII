# Later: telling the player a setting's default changed

RULE A far-future item (`scope: later`): tell the player when a setting's default has changed, and
ask whether to keep their value or switch.
source: director
enforced_by: none: judgement

FACT The settings store holds only the keys the user has changed, which is what makes this possible
(`persistence.md`, Settings storage).
source: director
