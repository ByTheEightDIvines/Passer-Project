# Dynamic Travelers: handover, 2026-11-19 (late): Link's camp, summon crash

Supersedes `HANDOVER_2026-11-19-camp.md` for the current state and the crash. That file is still right for
what the camp does, the dialogue and the code map; read both.

Repo: `ByTheEightDIvines/Passer-Project`. Clone used: `/home/claude/pp` (re-clone in a new session; the
sandbox is wiped). Dusklight source: v2.0.3 (`git clone --depth 1 --branch v2.0.3
https://github.com/TwilitRealm/dusklight.git`, plus `git submodule update --init --depth 1 extern/aurora`
for a local test compile). Local build: `cmake` in `/home/claude/build` (clang, linux) just to check it
compiles; the real build is CI, which writes `dynamic_travelers.dusk` to the `dist` branch.

## State right now

- Last code commit: **03ef222**, pushed, CI green, and **installed** on the user's PC as
  `Desktop\Dusklight\mods\Dynamic Travelers.dusk` (md5 `59e4bf4a33dd9fd7cf63390d7a1f736b`, checked by
  staging it back), copy at `Desktop\Passer Project\builds\DT_03ef222.dusk`.
- Its `PsrPack.arc` is renumbered (see below), md5 `60d67f236e3e1ed84d1b84d637eb7296`. The user's source
  copy at `Desktop\Passer Project\assets\overlay\res\Object\PsrPack.arc` was updated to the same file.
  **Any future dusk must use this PsrPack.arc**; the code no longer knows the old effect numbers.
- The user has **not played 03ef222 yet**.
- The summon still has the **diagnostic spacing** from f6796e0: sparkle at frame 22, fire and bench shown at
  40, tent made at 58 (normally they were 22 / 28 / 28). And it logs two lines per frame (below).

### Next steps

1. The user summons a camp in 03ef222 (Mods window: "Give Midna the camping gear"; then in the field call
   Midna, More… > Set up camp).
2. Whatever happens, stage the diag log yourself (path at the bottom). Don't ask the user to watch screens or
   time anything: they can't catch a one-frame event, and they said so.
3. **If it crashed:** the last `pcamp   heaps free KB: …` and `pcamp   summon fN …` lines are the state on
   the frame it went down. Look for a heap near 0, the emitter or particle count at its maximum, or a jump
   between the last two frames. Also symbolize the newest `sentry\reports\*.dmp` (tools/debug; see below).
4. **If it didn't crash:** the shortcut was the cause, or part of it. Put the summon timing back (pieces a
   few frames after the sparkle, tent with them), keep logging for one more run, then remove the per-frame
   lines.
5. Then chunk 2 and chunk 3 (unchanged; see the earlier handover).

## What changed tonight (03ef222)

### Our effects go through the game's own routine (the user asked: no shortcuts for particle effects)

- Before: `psr_freeze::emit` called `JPAEmitterManager::createSimpleEmitterID` directly with our pack slot (2)
  and its own copy of the game's group choice. It skipped everything else `dPa_control_c::set` does.
- Now: `emit()` calls **`dComIfGp_particle_set(id, &pos, &angle, &scale)`**, the game's normal routine, so our
  effects get exactly the setup the game's own get (group change, lighting callbacks, colours, alpha).
- How the routine reaches our pack: it picks a pack only from the number
  (`dPa_control_c::getRM_ID`: top bit set → slot 1, the stage's; else slot 0). A **post-hook on
  `getRM_ID`** (`PackLookup` in freeze.cpp) returns slot 2 for numbers `0xF0xx`, and only after
  `ensureSlot()` has given the manager its third slot. Every other number is untouched.
  - Checked in the shipped exe: `dPa_control_c::set` really calls `getRM_ID` through its thunk (not
    inlined), so the hook takes effect. `getResourceManager(u8)` asserts `id < ridMax`; `ensureSlot()` sets
    `ridMax` to 3.
- **Renumbering.** Our pack used the game's numbers (three even clashed with the always-loaded pack:
  0x108, 0x110-0x112). All 65 game packs were scanned: the highest game number is 0x8C84, and nothing uses
  0xF000 and up. psr_fx.jpc's 17 effects are now 0xF000 + index (`tools/debug/jpcreid.py`):

  | new | was | what | name in code (freeze.hpp) |
  |---|---|---|---|
  | F000, F001 | 8818, 8819 | ice breaking | kFxIceBreakA/B |
  | F002 | 881A | ice block | kFxIceBlock |
  | F003-F008 | 8840-8845 | chest puff (unused now) | kFxChestPuff (first) |
  | F009 | 0108 | grey death smoke | kFxDeathSmoke |
  | F00A-F00C | 0110-0112 | old women's spell | kFxSpellGlow/Sparks/Burst |
  | F00D, F00E | 86BA, 86C2 | Midna's pop (summon, 3 spots) | kFxMidnaPop/Pop2 |
  | F00F, F010 | 86BC, 86BD | Midna's hand glow (unused) | kFxMidnaHand/Hand2 |

- This touches **every** effect of ours: the freeze spell (ice), the death smoke and the spell burst, as
  well as the summon. All of them should be re-checked in game.

### Other

- Summon logs, every frame: `pcamp   heaps free KB: game … zelda … archive … command … | emitters n/max
  particles n/max`, then `pcamp   summon fN fill X% zoom Z <- step`.
- The player camp's actor heap is 0x40000 (was 0x20000), the same as the traveller camp's.
- **Kept on purpose:** three sparkles, one per piece. The user said so explicitly; don't cut to one.

## The summon crash: everything known so far

- **The crash.** aurora's per-frame vertex buffer (`VertexBufferSize`, 5 MB, mapped and not owned) is full:
  `fifo::draw_prim → push_verts → push → ByteBuffer::resize → abort()`, code 0x40000015, on aurora's fifo
  worker thread.
- **Latest dump** (`4ca20b25….dmp`): the push that failed was only **0x546 bytes** (one ordinary triangle
  list, opcode 0x90). The requested size was 0x500216. So no single giant draw: the whole frame added up to
  more than 5 MB, about 2.5 times the normal 1.3-2.1 MB.
- **Frames aren't merged.** Each frame gets its own packet, and `begin_frame` maps a fresh buffer.
- **The diag run (f6796e0):** fill steady at 34-37% from f1 to f40. The diary is flushed every line, and
  there's no f41 line, so the crash is the frame the **fire and bench are first drawn (f40)**, with the
  sparkle 18 frames old. The tent wasn't made yet.
- **Ruled out, with evidence:**
  - **The models.** `coro_stones`, `coro_wood` and `psr_bench2` are byte-identical in every build, including
    5968f75, the run that didn't crash. PsrPack/PsrCamp md5 are the same across 079e8f7, 5968f75 and
    f6796e0. No misaligned files in the arc. The traveller camp draws the same models in the same way
    (`mDoExt_J3DModel__create(…, 0x80000, 0x11000084)`, settingTevStruct 0x20, MAJI, `mDoExt_modelUpdateDL`)
    and has never crashed.
  - **The normals fix.** It touched only PsrTent.arc, and the tent didn't exist yet at f40.
  - **The spot.** Both crash camps were near (-69000, -27000), field room 10 (F_SP121 R10_00). The only thing
    placed nearby is a `kytag3` 948 units away, with params 0x000003B0: a wolf-senses scent path that draws
    nothing.
  - **Shadows and grass:** the camp does neither.
  - **The sparkle on paper.** 0x86BA: sphere, fixed density, divNum 5, so 4·5·5+2 = 102 particles per burst;
    maxFrame 1 (one shot), life 53, shape 9 (dir-billboard). 0x86C2: about 25 particles, life 12. Work word
    0x2 (group 1), no light callbacks. Six emitters in all: roughly 600 quads, far too few for 3 MB. Yet
    every summon version with a sparkle crashed (Link's warp sparkle first, then Midna's) except one lucky
    run. That's why the shortcut was the prime suspect.
- **Lighting difference** (not a cause, just noted): the player camp doesn't copy `room_no`/`YukaCol` from
  the ground, as camp_fire and the tents do. The defaults are safe (`dKy_tevstr_init(home.roomNo, 0xFF)`).
- **Game heaps.** Dusklight reports them only in its F4 ImGui overlay, not in any log. The game log is
  buffered and loses its tail in a crash (the newest one stops mid-line), so heap warnings from the last
  second never reach the file. Hence the mod now logs heaps itself.
- **Ideas if 03ef222 still crashes:** a heap at 0 points to setup memory (`fopAcM_entrySolidHeap` retries
  bigger on failure and logs `登録失敗`). A full emitter or particle pool would show in the counts. If
  everything looks normal, the next suspect is aurora's handling of the first draw of these materials in
  that light setup. Try one cut at a time, logging the fill per frame.

## Tools

- `tools/debug/`:
  - **Crash-dump reading with real names** (see its README). Stage `Desktop\Dusklight\dusklight.exe` and take
    its `.symdb` section (symgen manifest; zstd payload at +0x48, streaming decompress via ctypes
    `libzstd.so.1`). Pickle the code entries (`code.pkl` + `strs.bin`), then run
    `symb.py <dmp> <thread|all>` and `stackvals.py` (raw values around frames; set the thread id).
    The fifo worker is the crashing thread; aurora's render worker sits in `render_worker::worker_main`.
  - **Disassembly works here:** `llvm-objdump -d --start-address=0x140000000+rva dusklight.exe` (image base
    0x140000000). Use it to check that a function is really called before hooking it.
  - `jpcreid.py` (new tonight), plus RARC read/replace, JPC parse/merge, the BMG/flow reader and `peexp.py`.
- `tools/tents/normals/`: BMD reader and editor, the normal fix, and a lit renderer.
- `tools/music/make_ast.py` made psr_sunsong.ast (loop flag at 0x0E set to 0).
- The game's particle packs are now staged at `/mnt/user-data/uploads/Rom Dump/files/res/Particle/`
  (all 65; re-stage in a new session).

## The user (important)

- Not a programmer: plain terms, no code, a chatty, slightly goofy tone. Talk as you go; ask before big or
  ambiguous changes, but just make simple edits.
- **Never ask them to do something humanly impossible** (catch a one-frame moment, read numbers mid-crash).
  Make the mod record it instead.
- **No shortcuts:** do things the way the game does them (its own routines), adding hooks from inside the
  mod where the game lacks a way in. No custom Dusklight build.
- **Debug from evidence:** don't throw out guesses as answers. They push back hard on wrong theories (the
  camera, "location") and are usually right to. Don't blame their other mods (randomizer, luau etc. are
  loaded).
- Use the game's assets first. It's not a replacement mod (add, never replace game assets or slots). No game
  assets in the repo. Check changes against Dusklight 2.0.3's source before building.
- Model changes: render first, for approval. Backface culling off everywhere; no liberties with textures.
  Every skinned model must pass the DRW1 check (their notes: tools/drw/README_DRW1.md).
- Never change NPC skin tones; NPCs never comment on skin.
- **Install:** SendUserFile, then device_commit_files (force) to `Desktop\Dusklight\mods\Dynamic
  Travelers.dusk` and `Desktop\Passer Project\builds\DT_<sha>.dusk`. Then stage it back and compare md5
  every time.
- **Packing:** fetch the build with `git fetch origin dist:refs/remotes/origin/dist`, then
  `git show origin/dist:dynamic_travelers.dusk` (`gh run download` is blocked by the proxy). Start from the
  previous full dusk (`/home/claude/pack/DT_03ef222.dusk` this session; otherwise stage the user's
  `builds\DT_03ef222.dusk`), swap `lib/*` + `mod.json`, and replace changed overlay files in the zip by hand.
- **Folder access:** requests are often blocked on the first try; send twice. Granted this session:
  - `Desktop\New Modding\Rom Dump`
  - `Desktop\Passer Project`
  - `Desktop\Dusklight` and its `mods` subfolder
  - `AppData\Roaming\TwilitRealm\Dusklight`
- **Pushing:** needs the repo attached with push access (`add_repo` with access `push`).

## Paths

- **Diag log:** `C:\Users\brian\AppData\Roaming\TwilitRealm\Dusklight\mod_data\com.bytheeightdivines.dynamictravelers\travellers_diag.log`
- **Crash dumps:** `…\TwilitRealm\Dusklight\sentry\reports\*.dmp`.
- **Game logs:** `…\TwilitRealm\Dusklight\logs\dusklight-YYYYMMDD-HHMMSS.log` (buffered, so the tail is lost
  in a crash).
- **ROM dump:** `Desktop\New Modding\Rom Dump\files\res\` (Particle\common.jpc + Pscene*.jpc, Stage\F_SP121\
  R*_00.arc for field rooms, Msgus\bmgres.arc).
- **Assets:** `Desktop\Passer Project\assets\overlay\res\Object` and `…\overlay\Audiores\Stream`.
