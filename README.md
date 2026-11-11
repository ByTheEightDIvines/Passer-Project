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
- After three enemy hits it is launched away from the enemy, backflipping through the air like a Wind Waker
  Bokoblin, and bursts into smoke when it lands.

## Spot picker (scouting tool)

For choosing where camps, Beedle's tent and the road network go. Stand Link on a spot and log it:

- From this mod's page in the Mods window (shows Link's live position, the logged list, Undo, Copy all, Clear all), or
- With the controller: hold **L + R** and tap the D-pad. **Up** = road point, **Right** = camp, **Left** = Beedle's tent,
  **Down** = undo the last spot. A toast confirms each one.

Spots are saved as they are logged to `spots.tsv` in the mod's data folder, one tab-separated line each
(kind, stage, room, x, y, z, facing, day, time). **Copy all to clipboard** gives the same text to paste into chat.

## Tents (test placement)

The camp tents are a custom actor (`PsrTent`) whose models and collision ship in the dusk as a new archive,
`overlay/res/Object/PsrTent.arc` (nothing on the disc is replaced). Until camp spots are picked, the Mods window page
has buttons to drop a small tent, a big tent or Beedle's tent in front of Link, and one to remove them again.
The generator scripts are in `tools/tents/`.

## Build

The mod builds like the other Dusklight mods: CMake fetches the pinned Dusklight version (`DUSKLIGHT_VERSION` in
`CMakeLists.txt`), and GitHub Actions builds every platform and merges them into one `.dusk`. Pushes to `main` publish
the combined bundle to the `dist` branch.

```sh
cmake -B build -G Ninja
cmake --build build
```

## Current limits

- The death flight is procedural (a held flinch pose plus a code-driven tumble); there is no hand-made animation.
- Enemies with special routines that read Link directly may still go after Link.
- No lantern at night and only one talk line.
