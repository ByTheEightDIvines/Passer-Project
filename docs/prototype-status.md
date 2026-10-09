# Prototype status

| Behavior | Status |
| --- | --- |
| One MAN_a2 passer on road route 1 in F_SP121 room 5 | Room overlay builder implements it; needs in-game verification |
| Escape path | Two TagEsc markers are placed at route 1 endpoints; needs in-game verification |
| Link lock-on | Hook enables battle attention on the scoped passer; needs in-game verification |
| Damage | Existing passer collision cylinder is made weapon-targetable; three target hits reduce its health; needs in-game verification |
| Enemy pursuit | Shared enemy aim/range queries redirect to the nearest living passer within 900 units in the same room; needs in-game verification. Special enemies that read Link directly may need actor-specific hooks. |
| Flee from enemies | Nearby enemy presence feeds the vanilla passer fear/escape action; needs in-game verification |
| Death | Short knockback and procedural fall; actor removed after the effect window; needs in-game verification |
| Poe death effect | Poe smoke and spark particle IDs are emitted once at death; needs in-game verification |
| Dialogue | A colocated TagKMsg receives a runtime-created Flow node with one custom line; needs in-game verification |
| Talk animation | Passer's existing talk clip is selected while the TagKMsg conversation is active; needs in-game verification |
| Enemy AI targeting | Common aim/range helper hooks redirect target calculations to the passer; special-case target logic and actual attacks need in-game verification |
| Bulblin animation retarget | Not implemented |
| Nighttime lantern | Not implemented |

All runtime hooks are scoped to stage `F_SP121`, room 5, and passer type 8. The overlay builder reads the prepared local room archive derived from the user's ROM dump. The generated archive and `.dusk` are ignored by Git and must not be committed to the public repository. The source compiled and the package was created; no in-game behavior has been verified yet.

