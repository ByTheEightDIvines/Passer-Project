# Convoys: state of the build (2026-11-18)

Working notes for whoever picks this up next. The user is not testing until everything is built,
so nothing below has been played yet.

## Agreed design (everything settled with the user)
- **Before all twilight is cleared:** no moving convoys. A parked traveller wagon with three people
  standing by it sits on the road below each twilight wall that still stands:
  - Faron-Eldin wall: parked in the ravine below it, on the south-field side.
  - Eldin-Lanayru wall: parked in the north canyon, on the Eldin side.
  - The same three people at both; they recognise Link at the second. Each goes once its wall does.
    No thank-you, no packing up; Link is never there when it happens.
- **All twilight cleared:** the gorge traveller convoy runs Faron Woods -> Kakariko Gorge -> the
  east field -> Castle Town's east gate, every 12 game hours.
- **Eldin bridge warped back** (M_092, from the desert): the long route opens.
  - Royal convoy: Castle Town east -> the bridge -> north canyon -> north field -> west canyon ->
    Faron Woods, every 24 game hours.
  - West-gate traveller convoy: Castle Town's west exit -> west canyon -> Faron Woods, every 12 hours.
  - By then every blocker on that road is gone: both twilight walls, the escort gate (the Telma
    escort comes first), the bridge, rocks 4 (very likely the story's bomb wall: unconfirmed), and
    rocks 2 (removed by the mod).
- Nothing of ours appears while Telma's escort runs.
- The guards' camp at the field end of the Eldin bridge: up once Castle Town's twilight is lifted,
  permanent. After the repair the guards say it's fixed and they'll move on after one more ale,
  then stay.
- All dialogue is written at the very end; the build only marks where lines go.
- Royal convoy: one walking swordsman carries a royal bow (the Bulblin bow re-textured from the
  guards' colours and banner trim; show it in the 3D viewer first). His small shield goes on his
  back while he shoots.

## Built
- `src/wagon_actor.*` (`PsrWgn`): horse, harness and wagon put together the way the game's coach
  does it (Coach.arc), driven by `driveTo()`. The wagon trails the hitch toward its rear axle. Wheels
  roll; the game's coach collision applies. Royal pieces come from `PsrWagon.arc` (overlay):
  `royal_body`, `royal_harness`, `royal_cover`, the four `royal_*_wheel` (each in its wheel joint's
  frame), and `royal_cargo` / `trav_cargo` (in the wagon's frame). Exported by
  `tools/convoy/export_royal_scene.py` (the approved royal11 scene, split into parts) plus the export
  steps in this session's log.
  - Fire: a Bulblin arrow sticking in the wagon sets it alight (the dBgW arrow-stick callback; never
    the bowman's arrows). It burns down in about 50 seconds using Telma's coach fire effects
    (0x85E7..0x85F0). Monsters at a halted wagon also damage it. At full damage it's a wreck: the
    horse breaks loose and bolts down the road, the harness and wagon stay, and it smoulders.
- `src/convoys.*`: the three convoys on their clocks (game minutes), moving on paper while Link is
  away. Each comes out in the world (out of sight) when Link is within 7500 and goes back past 10500.
  Riders sit on the bench; escorts walk at the wagon's sides, placed by hand ("held"). A monster
  within 1800 stops the wagon and lets the escorts fight (the passers' own AI); after a quiet spell
  they fall back in and it rolls on. Wrecked: everyone runs (fighters may stand), and the convoy ends
  once Link is far away. The parked wagons are handled here too (`tickNod`).
- `src/convoy_routes.inc`: the three roads baked by `tools/convoy/gen_routes.py`, with ground height
  and room every ~150 units. The long road has a stretch with no field data (the old route's straight
  "join up onto the northwest road", about points 1220-1265); the wagon follows the probed ground
  there. **Check this stretch in game.**
- Roster: 48 slots (`kSlots`; walk paths 0x40..0x6F, flee 0x80..0xAF). 30 travellers, 3 guards at
  the bridge camp, and the rest are convoy crew (`crewClaim` and friends in travellers.cpp).

- **Guards** (types 0 MAN_a, 1 MAD_a; styles spear / sword / archer): the game's own spear
  (object.arc `b_spear.bmd`), tall or small shield, sword and scabbard from PsrPack (`psr_g_*`), drawn
  from the holds in `src/guard_gear.inc` (walking and fighting holds differ). They fight from their
  own stances (`psr_g_spear/sword_<body>`), spearmen thrust, everyone blocks frontal blows 70% of the
  time. The shield arm rides the arm layer (`psr_garm_A/D_<body>`) except on the bench and in a fight.
  Spearmen walk and stand with the men's stick walk/wait (Mspecial). The royal archer has the royal
  bow and quiver (`psr_bow_royal` / `psr_quiver_royal`). Guard arms live in the gear heap (0x60000).
- **Castle Town side** (`src/convoy_gates.inc`, `tools/convoy/gen_gates.py`): the gorge and long
  roads carry on into F_SP122 room 17 over the east bridge to the east gate; the west road starts at
  the west gate in room 8 and crosses the west bridge into the field. Each point knows its stage; a
  convoy is only out in the stage Link is in.
- **Dialogue markers** (`src/dialogue_marks.*`): `dialogue::spotFor(actor)` names the spot (nod
  wall 1/2, guard camp bridge out/back, gorge/west/royal convoy) with a placeholder line each. Not
  hooked to talking yet: that comes with the real lines.

## Still to do
1. **Dialogue:** write the lines, then hook `spotFor` into the talk marker.
2. **Riders' cutscene sit poses** (A/C/D/E/F), if wanted; riders use the bench sit for now.
3. **In-game test** (the user's final test): Telma's escort untouched, the long road's data hole,
   the F_SP122 stretches, guard gear fits, rocks 2 gone, the pennant size.
