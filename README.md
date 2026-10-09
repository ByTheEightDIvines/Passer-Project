# Hyrule Field Passer Prototype

A Twilight Princess / Dusklight prototype for one road-walking passer in Hyrule Field stage `F_SP121`, room 5. Everything is done by code hooks plus Dusklight's StageService: no modified game files and no ROM-derived data are needed, so the whole mod builds from this repository.

## Prototype behavior

- A `MAN_a2` passer is added to the room at load time and follows the stock road route 0 (six points along the field road), with escape markers at both ends of the route.
- Link can lock onto the passer and ordinary weapon target hits can damage it. The first prototype uses three hits.
- Nearby enemies trigger the passer's vanilla run-away action.
- At zero health, a short knockback/fall is played, the Poe smoke and spark effect appears, then the actor is removed.
- A talk marker beside the passer uses Dusklight's Flow and Message services to show one custom line. The passer plays its existing talk animation during the conversation.

The prototype redirects shared enemy aim and range queries to the nearest living passer in room 5 when that passer is within 900 game units. This gives enemies using the common player-search helpers a passer target while preserving Link as the actual player actor. Enemy collision and AI target choice are separate systems, and enemies with special routines that read Link directly may still focus Link; the pursuit and attack behavior needs in-game verification.

## Build

Pushing to GitHub runs the Actions workflow: it builds the native module for Linux, macOS, iOS, Windows and Android, then merges them into one all-platform `.dusk` (`mod-combined` artifact, `tools/merge_mod.py`). Nothing else is needed.

Local build:

```powershell
cmake -S . -B build -DDUSKLIGHT_DIR="C:\path\to\dusklight-source" -DDUSK_GAME_EXE="C:\path\to\Dusklight\sdk\windows-amd64.lib"
cmake --build build --config Release
```

Install the `.dusk` through Dusklight and test `F_SP121`, room 5.

## Current limits

- The custom fall is procedural; the Bulblin animation has not been retargeted to the passer rig.
- Enemy targeting uses shared aim/range hooks; enemy-specific routines that access Link directly may need their own handling.
- The passer carries Link's stock lantern at night (18:00-06:00 game time, light world only). The lantern has no light source yet, and additional dialogue lines are not included.
- The package builds, but collision, talk prompt, animation, and particle behavior still need an in-game test in Dusklight.

