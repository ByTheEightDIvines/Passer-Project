#pragma once
// Beedle's shop: its own stage (R_PSR00), entered through the curtain of his tent. Going in, the mod
// adds a spawn point on the grass in front of the tent's door (the stage service) and loads the shop;
// walking back out of the shop's doorway loads the field again at that spawn point.
#include "SSystem/SComponent/c_xyz.h"
namespace shop_stage {
bool install();
// The tent's curtain was reached: in we go. doorOut: on the grass in front of the tent; yawOut: facing
// away from it; fieldRoom: the field room the tent stands in.
void enter(const cXyz& doorOut, s16 yawOut, int fieldRoom);
bool inShop();            // the shop stage is loaded
void tick();              // once a frame: leaving through the doorway
}
