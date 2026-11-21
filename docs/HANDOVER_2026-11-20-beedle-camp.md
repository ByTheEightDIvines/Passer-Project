# Dynamic Travelers (Passer-Project): handover, 2026-11-20 evening

## Who / how to work with the user
- Not a programmer. Explain every build in plain terms, never show code.
- Likes personality: chatty, a bit goofy between task work.
- Answer their question first. Ask before any major behaviour change. Then commit and push.
- Model or animation changes: send renders first. Full model, unlit (textures only). Wait for approval before anything goes in the game.
- Never flat shading, never backface culling on their models. Rolled edges, not stitching. No texture liberties.
- Crashes: go by the evidence. Don't blame their other mods.
- Installing: overwrite `C:\Users\brian\Desktop\Dusklight\mods\Dynamic Travelers.dusk` directly, no need to ask. Always verify afterwards: re-stage the file and compare its sha256 with the build.
  - If the game is open, the file is locked. Ask them to close it.
- Ask before using computer use on their PC.

## Repo, build, install workflow
- **Repo:** GitHub `ByTheEightDIvines/Passer-Project`. The old `passer-project` URL redirects there. Branch `main`.
- **CI:** builds on push. The all-platform `.dusk` lands on the `dist` branch, fetched with `git fetch origin +dist:dist`, then `git show dist:dynamic_travelers.dusk`. That file is code only.
- **Assets:** the game-derived assets (overlay `.arc` files, voices, music) are not on GitHub.
- **Packaging (used all session):**
  1. Stage the installed `Dynamic Travelers.dusk` from their mods folder.
  2. Copy every entry, replacing `lib/*` and `mod.json` with the CI build's.
  3. If an asset changed, also replace its entry, e.g. `overlay/res/Object/PsrPack.arc`.
  4. Verify every entry, then install.
  - `tools/add_assets.py` is the repo's own route.
- **Install via the device bridge:**
  1. `SendUserFile` the package.
  2. `device_commit_files` with its `fileUuid` (a stagedPath under outputs gave HTTP 400) and `expectedMtimeMs` from a fresh stage.
  3. Re-stage and compare the sha256.
- **Local compile check:** `/home/claude/build` (ninja), if the container still has it. Otherwise rely on CI.

## Installed right now (verified)
- **Build:** `35853da`, `.dusk` sha256 `7cf818cb51e1452ba572c53a9a71c70164ffe798e79db85742eebb8d72e1c6d1`.
- **Archives:**
  - `PsrPack.arc` contains the new `bck/psr_m_getup.bck`.
  - `PsrCamp.arc` contains the new `bmdr/coro_wood_link.bmd`, which Link's camp now uses (no spit posts).
- **Backup copies:** everything from this session (build, both updated archives, sources, handover) is also in `Desktop\Passer Project\Session 2026-11-20\`.

## What this session did (all installed in b127b0e)
1. **`71a6ae9`, guard zooming, first cause found from the video and diary:**
   - A waiting guard kept leaving his turn to sprint after a swooping Kargarok.
   - Fixes:
     - Fighters on foot no longer claim flyers: E_kr, E_ba, or anything more than 150 above the ground under it (`foesNear`).
     - Chase speed 1.9×/1.8× → 1.4×.
     - Walk and run play rates match the real speed (`stepToward`, convoy crew).
     - Convoy catch-up 3.2× → 2.6× the wagon's pace.
   - The royal archer's `kGuardShieldBack` was rotated 180° from every other guard's back shield. It now has their orientation, pushed out the same depth over his bow and quiver.
2. **`6f17b32`:**
   - **Spear and shield:** spearmen and swordsmen carry them in hand on the march (walk, run, spear at ease: `fighter_anim::marching`). Sitting, the bench and everything else: both on the back. The royal archer is unchanged.
   - **Wreck culling:** a wrecked wagon was culled against the bolting horse's matrix (it blinked with camera direction). Now it isn't culled at all until the horse is gone, then it's culled against the wreck (`mCoachMtx`).
   - **Escort catch-up:** escorts far behind their place (>600) and out of view are snapped to it instead of streaking back.
   - **Diary:**
     - `jump` lines: any of ours moved >30 in one tick, and by what (own walk/fight, convoy, grounding).
     - `cull` lines: hidden/shown within 4000 of Link.
   - The user saw all guards except the archer vanish, then zoom back in, on the bridge by the royal convoy. Root cause is not proven. Check the `jump`/`cull` lines in their next log.
3. **`f02b2dc`, Beedle's own rules (from the user):**
   - **Where he is:**
     - He's away until 3 in-game hours after he last left Link's area, then placed on a road node 3300–5500 from Link.
     - Out of sight, behind Link preferred, in a province with camps (a room that holds an NK_CAMP node).
     - Never in his tent's room (F_SP122 room 8). Never moved along off screen.
     - Re-placed after 45 s if his room's spawner hasn't brought him in.
   - **No resting:** no rests, camps, town or night-time send-home.
     - That night-time step was why he was never seen: deep night sent him "home". He has no camp, so he went to "town", and evening held him there 4pm–6am.
   - **Fleeing:** run 2.2× → 1.6×. The postman dust only shows while fleeing.
   - **Can't die:** at 0 hearts he's knocked out. He's thrown, lands, lies about 45 s, and monsters ignore him.
     - Link can wake him by talking. Placeholder lines: "Nnngh... Wh-what hit me...?" → OHHHHH clip → "Mr. Customer! You woke me up!..." → today's ware.
     - Then he gets up with full hearts.
   - **Tent room is a hard rule:** flee paths stop at it. If he's ever standing in it, he flees away from the tent (`travellers::beedleKeepOut`).
   - **Sound:** the field "OHHHHH" was already correct (WW clip 39, resampled to 32 kHz; the same file as the tent).
   - Diary `beedle` line now shows away/back-time/waiting.
4. **`b127b0e`, Beedle's get-up animation (user loved it, "his bag is so big"):**
   - `psr_m_getup.bck`, 53 frames: from the end of `psr_m_land` (on his back on the pack) → sit up → squat (his `psr_bs_wait01`, feet put on the ground) → groggy head shake → `psr_injury_wait` stand. Then it blends to idle.
   - Script and README in `tools/anim/beedle_getup/`. It needs the handover zip's `work/w_anim` tools.
   - Known: the pack sinks into the ground in the vanilla landing pose. The user didn't ask for a fix.

## Open / next (the user's latest list, 19:14)
The user said: once these are done, "we can finally do the big AI build for the NPCs".

1. **Link's camp fire: simpler, no spit sticks.**
   - DONE and INSTALLED (`35853da`). The user hasn't seen it in game yet.
   - The posts and forks are 92 triangles of `coro_wood.bmd`, sharing no vertices with the woodpile. They were collapsed to a hidden point to make `coro_wood_link.bmd`.
   - `coro_wood.bmd` itself must stay: the travellers' camps hang their pot on it.
   - It's in PsrCamp.arc, and `kFireParts[1]` in `src/player_camp.cpp` points at it.
2. **Link's camp layout ("looks out of place next to the tent").** Now:
   - Fire at the camp centre.
   - Tent (a_tent ×1.3, footprint ±167 × ±275 scaled) at z −650, door facing the fire, so about 400 of empty ground between door and fire.
   - Bench (`psr_bench2`, 220 long) on a 230 ring at 0x1C00 (39° toward the open side), facing the fire.
   - Proposal not yet shown:
     - Tent closer (about −520).
     - Bench out to the side at about 0x3400–0x4000, ring about 250, angled at the fire.
     - Render top and 3/4 views for approval.
   - Constants: `kTentBack`, `kBenchAngle`, `kBenchRing`. The door check points derive from `kTentBack`.
3. **Bench collision:**
   - `tools/tents/dzb.py` `write_dzb(tris)` makes a box (bench bounds x ±110, y 0..44, z ±17).
   - Add it to PsrPack as `dzb/psr_bench2.dzb`; `rarcwrite` handles a new subdir.
   - Register a `dBgW` in player_camp like `tent_actor.cpp` (Set/Regist/Release/Move).
   - Only while the camp is shown (`mShown`).
4. **Sit on the bench:**
   - Research so far: Link has no free-play sit. `PROC_GET_READY_SIT` (`procCoGetReadySitInit`, d_a_alink_demo.inc) is an event (demo) proc. It plays `ANM_TRES_OPEN_SMALL` as a crouch, not a real seated loop.
   - Next: look for a seated Link animation (ANM list in d_a_alink.h) or an event or staff action that sits him.
   - Otherwise a custom approach: a talk/check point at the bench like the tent door check, then hold Link in a pose via an event with our own BCK. That needs a Link-rig animation.
   - Ask the user before going the custom-animation route.
5. **Bard music "was working, now it's not":**
   - The newest log (session 18:08) shows songs starting and playing fine. But Link was never within about 13,000 of a playing bard (audible within 2,600, `kFar`). The only camp visited was the guards' (no bard).
   - Bards play 19:00–05:00 only.
   - We asked the user which camp and time, or for a log after walking up to a bard at night. Waiting on that.
   - Side note seen in the 18:08 game log: `talk: the travellers' own talk (a message or its text) could not be registered (result 5)`. The people/camp-traveller talk may be broken. Investigate.
6. **Still pending from before:**
   - **Guard AI:** sticky claims (~2 s); the wait spot shouldn't move unless the foe moves well away.
   - **Tent vertex-buffer crash:** the new PsrTent.arc broke it; the old one works. The user chose to fix normals on the OLD tents instead (done earlier). Isolating which new tent file caused the crash is still open.
   - **Memory:** Zelda pool fix installed (`3425208`). Read the `memory` lines in the next diary.

## Logs and data on the PC (all through the device bridge)
- **Diary:** `C:\Users\brian\AppData\Roaming\TwilitRealm\Dusklight\mod_data\com.bytheeightdivines.dynamictravelers\travellers_diag.log`. It's overwritten each session.
- **Game logs:** `...\TwilitRealm\Dusklight\logs\dusklight-*.log`.
- **Folders:** connected folders include `Desktop\Passer Project`, `Desktop\Dusklight`, `Desktop\Modding Archive` (the Wind Waker dump) and `Videos`.
  - The Rom Dump (vanilla `res/Object`) was denied twice. Don't ask again; use mod archives and the handover zip.
- **Handover zip:** `Desktop\Passer Project\DT_handover_2026-11-18.zip`, 171 MB. Contains `work/w_anim` (bmd/bck/render/rarc/rarcwrite tools), `repo/tools`, and assets.

## Files in Desktop\Passer Project\Session 2026-11-20\
- `Dynamic Travelers (35853da).dusk`: the installed build.
- `assets/overlay/res/Object/PsrPack.arc` and `PsrCamp.arc`: the updated archives. Copy them over the asset folder if rebuilding from it.
- `assets/coro_wood_link.bmd` and `assets/psr_m_getup.bck`: the new pieces on their own.
- `source_35853da.zip`: the repo at this build.
- `tools_w_anim_getup.zip`: the model/animation tools used and the get-up script.
- `renders/`: the get-up GIF and sheet, the fire-wood preview.
