# Dynamic Travelers — handover 2026-11-18 (b)

Repo: ByTheEightDIvines/Passer-Project, main @ de6ae54. Dusklight v2.0.3.
Installed: `mods\Dynamic Travelers.dusk` = `builds\DT_de6ae54.dusk` (md5 36b05a607799530265a0eaa1e719bcf0, verified on the PC).
UNTESTED in game: Beedle's real shop (below). Test it first.

## Built at the end of this session (de6ae54): Beedle's tent as a real game shop
- `src/tent_beedle.cpp/.hpp`: `psrTentBeedle_c` is now a `dShopSystem_c` (daNpcT_c) shopkeeper, built on Malver's (clerkB) code and implementing the plan below. It compiles and CI links it on every platform, but it has not been run in game yet.
  - Seat: shop frame (55, 78.5, -370). Position held in `setAttnPos`, gravity 0.
  - Motions by name through the `setMotionAnm` override. Each message's `talk_anim` picks the motion: 1-4 talk, 5 praise.
  - Drawn with `daNpcT_c::draw` (game NPC lighting), no shadow.
  - Five `TAG_SHOPITM` tags (group 1, posIDs 1..5) on the cushions in the `chooseItem5` layout. Two `TAG_SHOPCAM` actors: `kCamEye` [0] (55,190,-150) and [1] (-310,330,60). Ware-view centre `kWareLook` (40,60,60), written into `mShopCamAction.field_0x54` every frame.
  - The shop opens only once every tag has its display: the game's shop reads display IDs when it finds the tags.
  - Bought or empty slots: `onFlag` + `onSoldOutItemFlag`, the display hidden (`daItemBase_c::hide`, not deleted, because SELECT_WAIT waits for it to settle home), and the tag deleted (it would show the display again every frame).
- `src/beedle_field.cpp`: the shop flows, i.e. greet G / keeper G+1 (q25 → "Anything else?" or psr_beedle_bye → "GOODBYE!"), and per ware M (can-buy → e13 select → offer + await_choice → options → yes → psr_beedle_buy → "THANK YOU!") with M+1 its description. Each ware has its own shop title and description. If a G/G+1 or M/M+1 pair is not back to back, a line goes to `travellers_diag.log` ("not back to back").
- Old marker talk removed (`tagPre`/`tagPost` return false, `talkTick` is empty).
- **Things to watch in the first test:**
  1. Talking to him opens the shop camera and the welcome.
  2. The stick moves the cursor over the wares, and the descriptions show.
  3. A gives the offer and Yes/No. Yes takes Rupees, gives the ware, says THANK YOU! with the praise anim, and the ware vanishes.
  4. B says GOODBYE! with the voice, and the camera returns.
  5. **Camera framing:** both views are untested. My render check failed (see camview.py) and needs fixing first. Tune `kCamEye`, `kWareLook` and `kMasterLookY`.
  6. Whether he sits at the right height (the seat came from the old actor).
  7. Lighting.
The previous full handover (`HANDOVER_2026-11-18.md`, `DT_handover_2026-11-18.zip`) still holds the overall design and the asset pipeline. This file covers what came after it.

## Done this session (all built into DT_6c39476)
- **Crashes:** Beedle's archive crash was the INF1 material order (see `tools/drw/inf1fix.py` and README_DRW1.md), not DRW1 or Live Reflections. Field and tent models are fixed (`P_Beedl2.arc`).
- **Travellers:** they walk and camp again. The story check now runs every 60 frames, and travellers are placed again once the field opens.
- **Beedle in the field:** never a fighter (runs from everything), lantern instead of a torch, smooth chainmail, bag normals fixed (`tools/beedle/fix_normals.py`).
- **Convoys:**
  - Escorts walk instead of sliding.
  - The travellers' wagon canvas is no longer black (K colours set the way Telma's coach does).
  - Drivers hold no lanterns. There are two wagon lanterns hanging by the handle (`psr_wgn_kntr.bmd` in `PsrWagon.arc`; constants in wagon_actor.cpp). You approved these.
- **Bombs:** everyone except the drivers runs. Anyone caught by the blast is thrown back unhurt (passer_prototype.cpp: `bombScare` / `blast`; convoys.cpp: `scareTick`).
- **Tent entry lantern:** the flame is `ID_ZI_J_KANTERA_FIRE` on the wick, with its own warm light.
- **Packaging:** the base is the previous full `.dusk`. Replace `lib/*` and `mod.json` with CI's `dist` build (`gh api ... contents/dynamic_travelers.dusk?ref=dist`). The overlay archives inside (PsrWagon v3, PsrPack with the renamed lantern, P_Beedl2) are already current.

## NEXT: Beedle's tent as a real game shop (researched, no code written yet)
You asked for:
- a real shop, examined from how the game's shops work;
- 5 wares;
- Beedle sitting on the counter, as in Wind Waker;
- "THANK YOU" when you buy and "GOODBYE" when you leave the dialogue.

### How the game's shops work (verified against Dusklight source and the ROM's zel_02.bmg, Kakariko Malo Mart)
**The shopkeeper** is a `dShopSystem_c` (a subclass of `daNpcT_c`). Template: `src/d/actor/d_a_npc_clerkb.cpp`, about 960 lines. Each frame, `Execute` runs this sequence:
1. `initShopSystem`
2. `setSellItemMax(n)`
3. `field_0xf60 = -1` (this is "moveCursor1" mode, used by normal shops)
4. `mShopCamAction.setCamDataIdx(0)`
5. `execute()`
6. `if (searchItemActor()) mShopCamAction.move()`

**Talking to the shopkeeper:**
- `evtTalk` calls `mShopCamAction.shop_cam_action_init()`, then switches the action to `shop()`.
- `shop()` calls `initTalk(mFlowNodeNo)` and `shop_init(false)`.
- After that, every frame it calls `mShopProcess = shop_process(this, &mFlow)` and `setMasterCamCtrPos(keeper pos + (0,150,0))`.
- When `shop_process` returns nonzero: `dComIfGp_event_reset()`, then the camera `Reset()` (if the result is 2) or `EventRecoverNotime()`.
- A result of 2 means an item-get demo played. `tend()` then sets `mSpeakEvent`, the talk restarts, and `shop_init(true)` resumes browsing.

**The wares** are `TAG_SHOPITM` actors:
- param = `group<<28 | posID<<24 | itemNo`. The group must equal the high nibble of the keeper's param. posID runs 1..5.
- `home.angle.x` = the item's flow id M.
- `home.angle.z` = save switches. Use 0xFFFF: we must never use the game's save switches.
- Each tag creates its own `daShopItem_c` display. `mItemCtrl.getItemIndex(i)` is the display's process id.

**The cameras** are 2 `TAG_SHOPCAM` actors (param & 0xF = 0 or 1, same group):
- camera 0 = the eye when looking at the keeper;
- camera 1 = the eye when looking at the wares.

**Flow ids used by moveCursor1.** These are passed through `getInitNodeIndex`. **Dusklight passes any id ≥ 0x8000 straight through as a custom node**, so our own GraphBuilder nodes work. No patch is needed.

| Where the cursor is / what happens | Flow used |
|---|---|
| On the keeper | keeper's `home.angle.x` + 1 (G+1) |
| Hovering an item | M+1, the description |
| Pressing A on an item | M, the buy flow |

- Custom nodes are allocated in sequence, but freed nodes get reused, so the M/M+1 and G/G+1 pairs **must be allocated back to back and checked**. If an id is not contiguous, burn an untyped `add_node` filler and retry.
- B goes to cursor 0, which is G+1.

**Vanilla flow layouts** (dump them with `msgtool/bmg.py`, flows 301–334 of `bmgres2_zel_02.bmg`):

- **Greeting G (301):**
  - "Hey. / Buy something." The last message ends with the raw tags `raw_tag(0,0x1b,{})`, `raw_tag(0,0x15,{})`, `raw_tag(0,4,{0,0})`.
  - `AUTOBOX 0` sets sendFlag 3, which is the shop's send control.
  - Malo's lines also carry a `{03…}` voice tag; we skip it.
- **Keeper flow G+1 (302):** branch on q25 (FLOW_QUERY_SHOP_CLERK, the B/cancel flag 7).
  - Result 1 (cancel) → GOODBYE line → end. The shop then finishes and the talk closes. Play `beedle_goodbye.wav` from a custom event here.
  - Result 0 → "Do you need something?", with the same 3 tags.
- **Buy flow M (for example 321 or 323):**
  - Checks: q6 Rupees, q21 item owned, q23 arrows fit.
  - Then:
    1. e13 SELECT_VERTICAL with params 00000002.
    2. Price message ending in `await_choice()` (0x20).
    3. Options message ("I'll buy." / "No thanks.").
    4. Branch q0 → yes: message → e3 take Rupees → (e24 sold out) → e17 give item (no demo), or e8 START_EVENT(1, item) for the get-item demo → e9 jump.
  - **Plan:** use our existing `psr_beedle_can_buy` query and `psr_beedle_buy` event (beedle_field.cpp) in place of the vanilla checks and e3/e17. The `THANK YOU!` message (plus the voice and praise anim) follows the event, then the flow ends.
- **Description M+1 (322):** one message, INF1 speaker 3, draw_type INSTANT.
- **Colours:** item name in colour 8 (orange), price in colour 6.

### Problems found in the vanilla code that our subclass must work around
1. **The 5-item camera** (`setCamDataIdx2`): `_debugSetCamera` sets the ware-view centre `field_0x54` to a hard-coded world point (167,220,120). Overwrite `mShopCamAction.field_0x54` (it is public) every frame after `searchItemActor`.
2. **Selling out:** `setSoldOut` only clears indices 0–2 and 6, and the display is hidden only on the demo path. After our buy event, do it ourselves:
   - delete the display (`getItemIndex(k)`);
   - `onFlag(k)` and `onSoldOutItemFlag(k)`;
   - record it in the tent's `g_soldMask`.
   Wares sold earlier today: still create their tags (`searchItemActor` needs count == sellItemMax), but delete their displays and flag them.
3. **`chooseItem5` layout:** cursor 0 is the keeper (to the left of 1 and 3). Top row is 1, 2; bottom row is 3, 4, 5. Moves: left from 1 or 3 goes to 0; up from 3 goes to 1, from 5 to 2; down from 1 goes to 3, from 2 to 5. On the 6 cushions (shop frame: x = ±130, z = -120/60/240) this fits if the ware camera looks from the −x side toward +x, so screen-left is toward Beedle:
   - far column (x = +130): 1 at z = -120, 2 at z = 240 (middle cushion left empty);
   - near column (x = -130): 3, 4, 5 at z = -120, 60, 240.

   First guess for camera 1: eye (-310, 330, 60), centre (40, 60, 60). My render check (`room/camview.py`) was not finished. Its first output looked wrong, so check the renderer before trusting it.
4. **Animation:** override `setMotionAnm` (virtual) to load Beedle's bcks **by name** from `P_Beedl`:
   - 0 = `psr_bs_wait01` (loop)
   - 1–4 = `psr_bs_talk01..04`
   - 5 = `psr_bs_praise` (once, then wait)

   Face data: all -1. Pick the talk motion through the messages' `talk_anim` INF1 byte (that's how `ctrlMsgAnm` drives it). Use the model `beedle_tent.bmd` with McaMorfSO flags (0, 0x11020285), as clerkB does. Draw through `daNpcT_c::draw()` so he is lit like a game NPC. That should fix the glow: `setEnvTevColor` needs the mAcch ground check.
5. **Seat:** shop frame (55, 78.5, -370), facing the shop's yaw. His origin sits inside the counter. Set gravity to 0, and restore `current.pos = home.pos` in `setAttnPos` before `setMtx()`. Set `eyePos` and the attention position yourself.
6. **Events:** `l_evtList` = {"",0}, {"DEFAULT_GETITEM",0}, {"NO_RESPONSE",0}; arc names {"", "P_Beedl"}. Talk distance index (3, 6), as clerkB.
7. **Remove the old imitation:** the KMsg marker and cushion talk in `tent_beedle.cpp` (`tagPre`/`tagPost`/`talkTick`) and the hooks in passer_prototype.cpp (lines ~3010, 3050, 3396). Keep the ware logic in beedle_field.cpp (`tentWares`, `wareDisplayItem`, `onCanBuy`, `onBuy`, `give`) and the voice wavs.
8. **Check before building:**
   - the `daNpcT_c` and `dShopSystem_c` constructors and virtuals resolve against the exe stub (FEATURES game);
   - run the DRW1/INF1 checks on any model change.

## Still open after the shop
- Much more dialogue for travellers, convoys and guards (you want it reviewed).
- Tuning every NPC voice (the last polish step).
- Optional: cutscene sit poses for riders.
- Your final test, including that Telma's escort is untouched.

## Files in this handover folder
- `assets/`:
  - `P_Beedl2.arc` (the fixed Beedle archive; ships as `P_Beedl.arc`)
  - `PsrPack.arc`
  - `PsrWagon_v3.arc` (ships as `PsrWagon.arc`)
  - `DT5_base.dusk` (the full package base)
- `tools/msgtool/bmg.py`: BMG INF1/FLW1/FLI1 reader and flow walker.
- `tools/room/camview.py`: shop camera-view renderer (unfinished check).
- `msg_dumps/`: the game's message BMGs extracted from your ROM's `res/Msgus` (reference only, not for shipping).

## Agreed next big feature: living travellers
See `PLAN_living_travellers.md` (also in the repo as `docs/PLAN_living_travellers.md`). Build it after Beedle's shop is tested.
- Stage 1: they wake up in their tents (home camps, locals and wanderers).
- Stage 2: relationships.
- Stage 3: smarter on the road.
- Stage 4: dialogue built from their needs and feelings.
