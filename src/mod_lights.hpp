#pragma once
#include "SSystem/SComponent/c_xyz.h"
#include "dolphin/types.h"

// Light on the ground and walls (the room/field model) from the mod's flames.
// The game lights its world models (BG) from "effect lights" only: of those registered, the one
// nearest the camera is used (d_kankyo: dKy_eflight_influence_id -> bgparts_active_light[1]). Point
// lights (dKy_plight_set) only light actors. So every lit lantern, torch and fire of ours offers
// itself here each frame, and the nearest to the camera becomes one effect light.
namespace mod_lights {
// A flame burning this frame: where, how far its light reaches (the game's power: a treasure
// chest's glow is 70), and its colour.
void offer(const cXyz& pos, f32 power, u8 r = 0xFF, u8 g = 0x9A, u8 b = 0x4A);
// Once a frame (mod_update): picks the nearest offer from the last frame and lights with it.
void tick();
}  // namespace mod_lights
