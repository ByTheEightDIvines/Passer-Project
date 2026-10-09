# Hyrule Field Passer Prototype

A local-first Twilight Princess / Dusklight prototype for a road-walking passer in Hyrule Field. The initial overlay is limited to stage `F_SP121`, room 5; it does not add routes in Eldin Bridge or the rooms that connect directly to Kakariko Village.

## Current prototype

- Replaces room 5's room archive with a version containing the authored road path and one `MAN_a2` passer (`Passer`, type 8) assigned to path 1.
- Adds a `TagEsc` waypoint on that same road path so the vanilla passer's existing escape behavior has a route to follow.
- Packages as a Dusk asset overlay. It does not include the experimental native hook draft as compiled code.

## Build the asset overlay

Use a complete Dusklight source checkout and a compatible CMake toolchain:

```powershell
cmake -S . -B build -DDUSK_DIR="C:\path\to\dusklight"
cmake --build build
```

Dusk writes `hyrule_field_passer_prototype.dusk` under the build output's `mods` directory. Install it through Dusklight and load `F_SP121`, room 5, to inspect the road passer.

## Experimental behavior hooks

[`experimental/passer_hooks.cpp`](experimental/passer_hooks.cpp) is an uncompiled development draft for room-local lock-on attention, damage tracking, nearby-enemy flee, a procedural fall, and Poe death particles. It is included as source for continued development only. It has not been verified in game and is not part of the installable overlay build.

Dialogue changes, a lantern prop, custom Bulblin animation retargeting, enemy AI target selection, and cyan recoloring are not implemented.

## Data and scope

The repository contains only the room 5 overlay needed for the prototype. It does not contain the ROM dump or the rest of the extracted game files. Keep future testing confined to `F_SP121`, room 5 until the route and actor behavior have been checked in game.

