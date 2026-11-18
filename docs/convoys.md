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

- **First test fixes (2026-11-18):**
  - Convoys: the first time a kind is open in a play session, one is already partway along its road
    (they used to start at their gates on a random delay, so the user never met one). Every 10 s each
    convoy writes a `convoy` line to `travellers_diag.log` (where, stage, Link's distance, open, shown).
  - Boars (`E_WB`) are neutral: never a target, never fled from, never hunting; a Bulblin up on a boar
    (`E_RD` with `ride_mode` != 0) is always in reach, and a blow at him goes up to his height (it used
    to land on the boar: 100+ blows on boars in the diag log, hp never moving).
  - A walker standing on the end of its path (the swordsman guard spawned on his seat) now arrives; it
    used to loop "stuck" with a question mark forever.
  - Guards' shields: on the back, upright, crest out, whenever they sit (camp or wagon bench). Seated,
    the sit animation turned the hand over and the shield came out upside down. In a fight the shields
    use the castle soldiers' own fight holds.
  - Wagons: destroyed by anything, a wagon blazes for 10 s, turns into the burnt-out wreck 1.5 s in (the
    approved look; `PsrWagon.arc` `*_burnt.bmd`, made by `tools/convoy/make_burnt.py`, `burn.py`,
    `burnt_cover.py`), then smoulders.

## Still to do
0. **Voices: tune every NPC's voice** (the user's polish item, last).
1. **Dialogue:** write the lines, then hook `spotFor` into the talk marker.
2. **Riders' cutscene sit poses** (A/C/D/E/F), if wanted; riders use the bench sit for now.
3. **In-game test** (the user's final test): Telma's escort untouched, the long road's data hole,
   the F_SP122 stretches, guard gear fits, rocks 2 gone, the pennant size.

## Royal archer's gear (2026-11-18)
- `psr_bow_royal.bmd`: the bowman's bow file (same joints and skinning, posed by `bowJointCB` as before),
  arms slimmed, new texture: plum lacquer arms with a gold inlay line, silver/gold bands, red leather grip
  laced in gold, linen string. `tools/convoy/royal_archer/royal_bow.py`.
- `psr_quiver_royal.bmd`: new low-poly quiver (74 triangles) in the guards' scabbard textures (plum leather,
  banded metal rim and foot), a gold Triforce on each broad face, five card arrows.
  `royal_archer/royal_quiver.py`.
- On a guard (no pack) the quiver and bow no longer use `kUnderPack` (they sank into the body):
  `src/guard_quiver.inc` (`royal_archer/fit2.py`) puts the bow against his back (or the shield) and the
  quiver over it, Triforce out, per body (MAN_a / MAD_a) and per shield place. Render: `royal_archer/view2.py`.
- The archer's shield now goes on his back while his bow is in his hand (it was left in the bow hand).

## Talking (2026-11-18)
- Lines agreed with the user: `src/dialogue_marks.cpp` (`kLines`, wrapped at 38 characters, 4 lines a page).
- `passer_prototype.cpp`: the talk flow is registered again at start (it had been off since the traveller
  system: the camp cook's lines were never registered, so he couldn't talk). One KMsg talk marker
  (`talkTick`, `onTalkTagExecutePre/Post`) sits on the nearest person with a spot (`dialogue::spotFor`,
  within 450 of Link, not fighting, fleeing or dead); talking picks a random line of that spot.
- While the conversation runs (`dialogue::talking()`), `convoys::update` returns early (other events put
  the convoys away, which would make one vanish in front of Link); a walking/standing crew member being
  talked to stands and turns to Link (`placeHeld`). Nothing during Telma's escort.

## Beedle (2026-11-18)
- Model: `tools/beedle/` (export_blend.py in Blender 4.0, skinbmd.py the skinned writer, build_beedle.py on
  MAN_a's joint frames with his bone positions; WW backbone1+2 = the game's backbone). His WW animations:
  retarget_ww.py (`psr_bs_*_up.bck` upper body over standing legs for the field; `psr_bs_*.bck` whole, for
  the tent). Field archive `P_Beedl.arc` (pack_beedle.py).
- Field: traveller slot 29 (`travellers::isBeedle`), type 0 (men's skeleton, Mgeneral walk/run/stand), his
  look (traveller_gear `kBeedleLook`). Wanders road spots and camps (never sits), never into town, never
  any road through F_SP122 room 8 (his tent). Flees at 2.2x a full run, never cowers or screams, with the
  game's footstep dust (dPaPoT_c, as the postman). Lantern on his bag at night (`kBeedleHook`).
- Shop: beedle_field.cpp. One ware a day (save date), from those unlocked; usual price - 5; custom flow
  query/event check and buy; THANK YOU (beedle_0038) / GOODBYE (beedle_0037) on programs 0xED/0xEC, files
  `res/voices/beedle_thanks.wav` / `beedle_goodbye.wav`.
