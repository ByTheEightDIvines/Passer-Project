# Hyrule Field Passer Prototype

A Twilight Princess / Dusklight prototype for one road-walking passer in Hyrule Field stage `F_SP121`, room 5. The room overlay and behavior hooks stay scoped to that stage and room.

## Prototype behavior

- A `MAN_a2` passer follows authored road route 1, with escape markers on that route.
- Link can lock onto the passer and ordinary weapon target hits can damage it. The first prototype uses three hits.
- Nearby enemies trigger the passer's vanilla run-away action.
- At zero health, a short knockback/fall is played, the Poe smoke and spark effect appears, then the actor is removed.
- A talk marker beside the passer uses Dusklight's Flow and Message services to show one custom line. The passer plays its existing talk animation during the conversation.

Enemy collision hits and enemy AI target selection are separate systems. The current hooks make the passer a valid collision target and trigger its flee response, but do not yet redirect every enemy's target choice from Link to a passer. That behavior must be implemented and verified for the field enemy actors before this prototype can claim it.

## Build locally

The public repository intentionally excludes all ROM-derived files. The local prototype uses the prepared room archive built from your ROM dump, which contains the placed passer and the authored road route. The stock ROM's room 5 archive does not contain that prototype placement, so pass the prepared archive as the input:

```powershell
python tools/build_overlay.py --room-archive "C:\path\to\prepared\F_SP121\R05_00.arc"
cmake -S . -B build -DDUSK_DIR="C:\path\to\dusklight-source" -DDUSK_GAME_EXE="C:\path\to\Dusklight\sdk\windows-amd64.lib"
cmake --build build --config Release
```

The generated overlay and finished `.dusk` stay local and are ignored by Git. The Windows package was built successfully at `build/mods/hyrule_field_passer_prototype.dusk`; it contains the native module and the local room overlay. Install it through Dusklight and test `F_SP121`, room 5.

## Current limits

- The custom fall is procedural; the Bulblin animation has not been retargeted to the passer rig.
- Enemy AI target retargeting is not implemented yet; the current prototype only enables attack collision and threat-based fleeing.
- Lantern behavior and additional dialogue lines are not included in this first pass.
- The package builds, but collision, talk prompt, animation, and particle behavior still need an in-game test in Dusklight.

