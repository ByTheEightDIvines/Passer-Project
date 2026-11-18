#include "global.h"

#include "mod_lights.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_camera_mng.h"

namespace mod_lights {
namespace {
struct Offer {
    cXyz pos;
    f32 power;
    u8 r, g, b;
};
constexpr int kMax = 64;
Offer g_offers[kMax];
int g_n = 0;
LIGHT_INFLUENCE g_light{};
bool g_on = false;
int g_frame = 0;

bool ours() {
    const int i = g_light.mIndex - 1;
    return i >= 0 && i < 5 && g_env_light.efplight[i] == &g_light;
}
}  // namespace

void offer(const cXyz& pos, f32 power, u8 r, u8 g, u8 b) {
    if (g_n < kMax) g_offers[g_n++] = Offer{pos, power, r, g, b};
}

void tick() {
    ++g_frame;
    const Offer* best = nullptr;
    if (auto* cam = reinterpret_cast<camera_class*>(dComIfGp_getCamera(0)); cam != nullptr) {
        f32 bestD = 1e30f;
        for (int i = 0; i < g_n; ++i) {
            const f32 d = g_offers[i].pos.abs(cam->view.lookat.eye);
            if (d < bestD) { bestD = d; best = &g_offers[i]; }
        }
    }
    if (best != nullptr) {
        g_light.mPosition = best->pos;
        g_light.mColor.r = best->r;
        g_light.mColor.g = best->g;
        g_light.mColor.b = best->b;
        // a flame's flicker
        g_light.mPow = best->power * (0.93f + 0.07f * cM_ssin(static_cast<s16>(g_frame * 2900)));
        g_light.mFluctuation = 1.0f;
        if (!ours()) dKy_efplight_set(&g_light);   // never twice; a stage change clears the table
        g_on = true;
    } else if (g_on) {
        if (ours()) dKy_efplight_cut(&g_light);
        g_light.mIndex = 0;
        g_on = false;
    }
    g_n = 0;
}
}  // namespace mod_lights
