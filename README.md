# Hyrule Field Passer Prototype

A Twilight Princess / Dusklight test mod: one passerby on the road through Hyrule Field stage `F_SP121`, room 5.

## What it does

- When room 5 loads, the mod adds a `MAN_a2` passer at Link's entrance. No game files are replaced; the actor is
  added through Dusklight's StageService.
- The passer walks a route the mod builds in code: up the canyon road, over the bridge and into the field in room 0
  where the enemies are.
- At the end of the route it waits until the camera can no longer see it, then disappears.
- When scared it runs along the road, then stops and cowers instead of vanishing, and walks on once it is safe.
- Link can lock onto it and talk to it (one custom line, with its talk animation). The talk marker follows it.
- Only enemies can hurt it. Nearby enemies make it run away along the road, and enemies that use the common
  player-search helpers aim at the passer when it is within 900 units.
- After three enemy hits it falls with a short knockback, the Poe smoke and spark effect plays, and it is removed.

## Build

The mod builds like the other Dusklight mods: CMake fetches the pinned Dusklight version (`DUSKLIGHT_VERSION` in
`CMakeLists.txt`), and GitHub Actions builds every platform and merges them into one `.dusk`. Pushes to `main` publish
the combined bundle to the `dist` branch.

```sh
cmake -B build -G Ninja
cmake --build build
```

## Current limits

- The fall is procedural; the Bulblin animation is not retargeted to the passer rig.
- Enemies with special routines that read Link directly may still go after Link.
- No lantern at night and only one talk line.
