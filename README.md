# Hyrule Field Passer Prototype

A Twilight Princess / Dusklight prototype for one road-walking passer in Hyrule Field stage `F_SP121`, room 5. The room overlay and behavior hooks stay scoped to that stage and room.

## Prototype behavior

- A `MAN_a2` passer follows authored road route 1, with escape markers on that route.
- Link can lock onto the passer and ordinary weapon target hits can damage it. The first prototype uses three hits.
- Nearby enemies trigger the passer's vanilla run-away action.
- At zero health, a short knockback/fall is played, the Poe smoke and spark effect appears, then the actor is removed.
- A talk marker beside the passer uses Dusklight's Flow and Message services to show one custom line. The passer plays its existing talk animation during the conversation.

The prototype redirects shared enemy aim and range queries to the nearest living passer in room 5 when that passer is within 900 game units. This gives enemies using the common player-search helpers a passer target while preserving Link as the actual player actor. Enemy collision and AI target choice are separate systems, and enemies with special routines that read Link directly may still focus Link; the pursuit and attack behavior needs in-game verification.

## Build

The public repository intentionally excludes all ROM-derived files. The room overlay (the prepared `F_SP121\R05_00.arc`, which contains the placed passer and the authored road route) is built locally from your own ROM dump and never committed.

**All-platform bundle.** Pushing to GitHub runs the Actions workflow. It builds the native module for Linux, macOS, iOS, Windows and Android, then merges them into one `mod-combined` artifact (`tools/merge_mod.py`). That bundle has no room data yet. Add your local overlay to it:

```powershell
python tools/build_overlay.py --room-archive "C:\path\to\prepared\F_SP121\R05_00.arc"
python tools/inject_overlay.py path\to\hyrule_field_passer_prototype.dusk -o hyrule_field_passer_prototype_final.dusk
```

**Local Windows-only build** (no CI):

```powershell
python tools/build_overlay.py --room-archive "C:\path\to\prepared\F_SP121\R05_00.arc"
cmake -S . -B build -DDUSKLIGHT_DIR="C:\path\to\dusklight-source" -DDUSK_GAME_EXE="C:\path\to\Dusklight\sdk\windows-amd64.lib"
cmake --build build --config Release
```

The generated overlay and finished `.dusk` stay local and are ignored by Git. Install the `.dusk` through Dusklight and test `F_SP121`, room 5.

## Current limits

- The custom fall is procedural; the Bulblin animation has not been retargeted to the passer rig.
- Enemy targeting uses shared aim/range hooks; enemy-specific routines that access Link directly may need their own handling.
- Lantern behavior and additional dialogue lines are not included in this first pass.
- The package builds, but collision, talk prompt, animation, and particle behavior still need an in-game test in Dusklight.

