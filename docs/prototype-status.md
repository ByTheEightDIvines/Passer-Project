# Prototype status

| Behavior | Status |
| --- | --- |
| Passer added to F_SP121 room 5 by StageService (no file replacement) | Implemented; needs in-game check |
| Road route built in code (20 points along the middle of the road) | Implemented; needs in-game check |
| Walks from the top-right exit toward Link's entrance | Implemented; needs in-game check |
| Waits at the end of the road and disappears once out of view | Implemented; needs in-game check |
| Talk marker (`KMsg`) follows the passer; one custom line and talk animation | Implemented; needs in-game check |
| Link lock-on | Implemented; needs in-game check |
| Only enemies damage it; three hits | Implemented; needs in-game check |
| Flees from enemies along the road (two code-placed escape markers) | Implemented; needs in-game check |
| Wind Waker style death: launched backflipping away from the enemy, smoke burst on landing | Implemented; needs in-game check |
| Enemy targeting through shared aim/range helpers | Implemented; enemy-specific routines may still target Link |
| Bulblin animation retarget | Not implemented |
| Nighttime lantern | Not implemented |
| Spot picker: log camp / Beedle's tent / road spots from the Mods window or L + R + D-pad, saved to `spots.tsv` in the mod's data folder, copy all to clipboard | Implemented; needs in-game check |
| Camp tents (small A-tent, big wall tent, Beedle's tent with night glow): custom `PsrTent` actor, models + collision in a new `res/Object/PsrTent.arc` overlay; test buttons in the Mods window place one in front of Link | Implemented; needs in-game check |
