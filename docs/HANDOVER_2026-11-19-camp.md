# Dynamic Travelers: handover, 2026-11-19 (evening): Link's camp

Repo: `ByTheEightDIvines/Passer-Project`. Clone used: `/home/claude/pp` (re-clone in a new session; the
sandbox is wiped). Dusklight source: v2.0.3 (`git clone --depth 1 --branch v2.0.3
https://github.com/TwilitRealm/dusklight.git`, plus `git submodule update --init --depth 1 extern/aurora`
for a local test compile).

## State right now

- Last commit: **f6796e0**, pushed, CI green, and **installed** on the user's PC (md5 `5a8f21e2…`), with a copy
  at `Desktop\Passer Project\builds\DT_f6796e0.dusk`.
- f6796e0 is a **diagnostic build** for the summon crash (see "Open: the summon crash" below). The user hasn't
  played it yet.
- Assets folder is up to date with the dusk: `PsrPack.arc` (Midna's sparkle in psr_fx.jpc), `PsrTent.arc`
  (normal fixes), `Audiores\Stream\psr_sunsong.ast`.

### Next steps
1. Ask the user to summon a camp in this build (Mods window: "Give Midna the camping gear"; then in the field
   call Midna, More… > Set up camp).
2. If it crashes: read `travellers_diag.log` (path below). The last `pcamp   summon fN fill …` line names the
   step: `<- sparkle` (frame 22), `<- fire and bench drawn` (40), `<- tent made` (58). Then fix that step.
3. If it doesn't crash: the fill numbers show the frame-by-frame load. Then put the steps back together, or
   leave them spread out if that looks fine, and drop the per-frame logging.
4. Then chunk 2 (the story bits) and chunk 3 (Link on the bench), below.

## Link's camp: what's built (plain terms, for the user)

- **Midna's menu.** In Hyrule Field (both stages), with the gear, never in twilight or during Telma's escort,
  her last choice reads "More…" and opens "Set up camp" / "Talk to Midna". Elsewhere her menu is untouched.
- **Summon.** She says "Fine, fine. One camp, coming right up!" and the camp appears on the flattest open
  ground near Link, clear of traveller camps, Beedle's tent and roads. If there's no flat ground: "Ugh, the
  ground's way too lumpy around here. Find somewhere flatter!" With a camp already up: "We already have a
  camp, remember? Want me to drag it over here?" (Yes/No). One camp at a time; it's kept in the save.
- **Pieces.** Coro's fire without the pot, one 2-seat bench, a small A-tent. Midna's sword-and-shield sparkle
  pops each piece in.
- **Fire.** Only human Link lights it, with a lantern swing (as Coro's fire). The wolf spark was removed at the
  user's request.
- **Sleep.** Check the tent from by its door: "Go to sleep?" Yes: the tent shuts and glows, Link is hidden,
  the camera pulls back and the sky turns 12 hours over the length of Sun's Song (5.6 s, the user's
  harp-and-oboe recording, a stream like Beedle's shop music; field music ducked). Then Midna pops out of
  Link's shadow through her own talk: "Rise and shine! You know you snore, right?" (morning) or "Finally! You
  slept the whole day away, sleepyhead." (evening). She is not involved in going to bed.
- **Tents.** All tents' normals fixed (flaps now two-layered; the wall, guard and Beedle tents' smoothed
  normals flattened). The user approved the renders. The guard tent's door curtains may z-fight (two layers
  at the same depth); not yet seen in game.

## Code map (player_camp.cpp)

- Actor `PsrPCmp` (actor names are at most 7 letters). Parameter bit 0 = just summoned (play the scene).
- Midna flow (group 0, from zel_00.bmg):
  - "Talk" edges 0x0113/0x0119/0x0128/0x012f each go through `psr_camp_allowed`.
  - Label overrides on message IDs 2041/2040/2005/2004 (English only).
  - Her talk's two edges into her menu (0x0139, 0x013c → 0x019e) go through `psr_camp_wake` for the wake line.
- Camp talk (group 8): "Go to sleep?" and its event.
- Camera: Dusklight's camera service (`register_camera_operator`), plus `guardZoom()`, which reads aurora's
  vertex fill (`travellers::vertexFill()`) and draws the camera in past 55% full.
- Scenes are speak events (`fopAcM_orderSpeakEvent`). During the summon, the actor's `current.pos` stands by
  Link, so the game's talk camera has nothing far to frame.
- Clock: `dKy_instant_timechg` (turns the date over at midnight).
- Tent: `PSR_TENT_PLAYER` (0x100) param bit: shut and lit while `player_camp::sleeping()`; no pause in its scenes.
- Test buttons are on the Mods window page (spot_picker.cpp).
- Save blob `player_camp`.

## Open: the summon crash (five times now)

- **Symptom.** Every time, the cause is aurora's per-frame vertex buffer (5 MB) overflowing:
  `fifo::draw_prim → gfx::push_verts → push → ByteBuffer::resize → abort()`; the stack shows ~5,247,020
  bytes in it. The sleep scene has never crashed.
- **Fixed so far:**
  - Link's warp sparkle, swapped for Midna's (it wasn't the cause).
  - The wide summon camera, now closer and guarded.
  - The game's own talk camera framing Link and the far-off camp: the camera is now held from the start, and
    the talk anchor stands by Link. Since then the scene starts properly ("summon scene starts" in the diary).
- **Still left.** The overflow now comes in a single frame, from under 55% (the guard never fired) to over
  100%, somewhere around summon frames 16–45. Suspects: the sparkle (6 emitters from our pack), the first
  draw of the fire and bench, or creating the tent mid-event. f6796e0 separates them and logs every frame.
  Debug from the diary; don't guess, and don't blame the user's other mods (randomizer, luau etc. are loaded).

## Still to do (the user's plan)

- **Chunk 2, story bits:**
  - A pile of gear near Coro's spot in Faron twilight. Midna takes it: "Ooh, what's this? Somebody's whole
    camp, just lying here! Heh… finders keepers. I'll hang onto it. Might come in handy later."
  - The first time in Hyrule Field: "Look at all this open space! …Hey, was that a yawn? You look ready to keel
    over. Remember that camping stuff I swiped back in the woods? Call me whenever you want to set up camp. I'll
    do the heavy lifting… as usual."
  - Save flags `hasGear` / `told` already exist; the test button sets both.
- **Chunk 3, Link on the bench:** the only seated pose found is the canoe one (`ANM_WAIT_SIT`). Render it on
  the bench first (the user's rule) before putting it in the game.
- **From the earlier handover:**
  - test 6c513f4's convoy and escort changes;
  - the guard camp east of the Bridge of Eldin;
  - bard music diagnostics (stand by a bard, read `music … alive`);
  - the possible follow-ups listed there.

## Tools added to the repo

- `tools/debug/`: crash-dump reading with real names (see its README). In short:
  1. Stage `Desktop\Dusklight\dusklight.exe`.
  2. Take its `.symdb` section (a symgen manifest). The zstd payload starts at +0x48; use streaming decompress
     via ctypes `libzstd.so.1`.
  3. Pickle the code entries, then run `symb.py`.
- `tools/debug/`: RARC replace, JPC merge (how Midna's sparkle 0x86BA/0x86C2/0x86BC/0x86BD came from
  Pscene011 into psr_fx.jpc), the BMG/flow reader.
- `tools/tents/normals/`: BMD reader and editor, the normal fix, and a lit renderer.
- `tools/music/make_ast.py` made psr_sunsong.ast. The loop flag at 0x0E was then set to 0, so it plays once.

## The user (important)

- Not a programmer: plain terms, no code, a chatty, slightly goofy tone. Talk as you go; ask before big
  behaviour changes.
- Model changes: render first, for approval. Keep backface culling off everywhere, and never take liberties
  with textures.
- Never change NPC skin tones; NPCs never comment on skin. Use the game's assets first. It's not a replacement
  mod (add, never replace). No game assets in the repo. Check changes against Dusklight 2.0.3's source before
  building.
- **Install:** SendUserFile, then device_commit_files (force) to `Desktop\Dusklight\mods\Dynamic Travelers.dusk`
  and `Desktop\Passer Project\builds\DT_<sha>.dusk`. Then stage it back and compare md5 every time.
- **Packing:** start from the previous full dusk and swap `lib/*` + `mod.json` from the dist branch
  (`gh api -H "Accept: application/vnd.github.raw"
  "repos/ByTheEightDIvines/Passer-Project/contents/dynamic_travelers.dusk?ref=dist"`). Overlay files
  (PsrPack/PsrTent arcs, the Audiores stream) are replaced in the zip by hand when they change.
- **Folder access:** requests are often blocked on the first try. Retry once; if it's still blocked, the user
  can add the folder in the desktop app (+ > Add folder). Folders granted this session:
  - `Desktop\New Modding\Rom Dump`
  - `Desktop\Passer Project`
  - `Desktop\Dusklight` and its `mods` subfolder
  - `AppData\Roaming\TwilitRealm\Dusklight`
- **Pushing:** needs the repo attached with push access (`add_repo` with access `push`).

## Paths

- **Diag log:** `C:\Users\brian\AppData\Roaming\TwilitRealm\Dusklight\mod_data\com.bytheeightdivines.dynamictravelers\travellers_diag.log`
- **Crash dumps:** `…\TwilitRealm\Dusklight\sentry\reports\*.dmp`. Logs: `…\Dusklight\logs\` (buffered, so the
  last lines before a crash are lost; the diary is flushed per line).
- **ROM dump:**
  - `Desktop\New Modding\Rom Dump\files\res\` (Msgus\bmgres.arc = zel_00.bmg, Particle\Pscene*.jpc,
    Object\Demo*.arc, Stage\…)
  - Midna's sword-and-shield scene is Demo10_01 (demo10.stb); her magic sound is `Z2SE_D10_MDN_MAGIC`.
- **Assets:** `Desktop\Passer Project\assets\overlay\res\Object` and `…\overlay\Audiores\Stream`.
