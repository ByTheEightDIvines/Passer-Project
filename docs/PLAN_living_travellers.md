# Plan: living travellers (agreed 2026-11-18)

Build it in this order. Each stage ships and gets tested on its own.

## Stage 1: they wake up in their tents (fixes density and odd spawns)
- **Home camp per roster slot**, saved in mod_data alongside the roster.
  - Most travellers are **locals**: their home is a camp in the region where they first appear (Faron, Eldin, Lanayru, or the field around the castle).
  - **About 1 in 5 are wanderers**: no fixed home, they cross the whole open map, and they sleep at whichever camp is nearest at dusk.
- **Nobody is placed on a road anymore.** On load and at dawn, each traveller starts at the camp they slept at:
  - out of a tent, using the camp's tent and seat system;
  - or at their seat if Link is already watching.
  - Where they slept carries over from night to night.
  - New game, or the region isn't open yet: start at the nearest open camp to their home.
- **The day (the plan for each slot):**
  - **New road open today?** `refreshStory` (every 60 frames) compares against yesterday's open-room mask. When a road opens, one or two travellers in that area go out and walk it, and end the day at the nearest camp on the far side.
  - **Otherwise** they wander the roads they can reach from where they are. A twilight or story blocker is simply a closed road (`edgeAllowed`), so nobody tries to cross it.
  - **By dusk:** go home if home can be reached before night. If home is too far or blocked, go to the nearest camp with a free seat, and head home over the next days.
  - Locals stay within a reasonable radius of home (a road distance budget, about 2–3 camps). Wanderers have no budget.
- Keep: the roster, `rehomeStranded`, the convoy and guard gating, the escort lockout.

## Stage 2: relationships
- **Per traveller:** up to about 8 links (other roster slot plus a feeling score from -100 to 100, plus a few flags), saved with the roster.
- **Starting ties:** some pairs begin linked (old woman and grandson, brothers, a couple, old friends). They share a home camp and travel together from day one.
- **What changes the score:**
  - Up: sharing a fire for a night, laughing at their joke, fighting side by side, being saved by them, music at the same fire.
  - Down: losing a seat to them, being left behind in a fight (they ran), a joke nobody laughed at, mocking.
  - Slow drift back toward 0 when they don't see each other.
- **How it shows in behaviour:**
  - Friends: pick seats next to each other, walk together if they're heading the same way, wave or greet on the road, prefer the same camp.
  - Rivals: avoid sitting together, grumble when they pass, sometimes argue at the fire (talk anims plus an angry bubble).
  - Fighters step in for a friend being attacked, even a weaker fighter or one who would otherwise run.
  - Missing someone: if a friend or family member is far away or dead, they mention them.

## Stage 3: smarter on the road
- **Needs on the trail:** hunger, thirst and company already drain everywhere but are only acted on at camp. Add:
  - snacking or drinking on the road (a short stop: stand or sit on the verge, use the seated meal loop if a ground sit works);
  - heading to camp early when very hungry or tired.
- **Remembering danger:** where they were attacked, avoid that road for about a day, warn others at camp ("bulblins on the east road!"), and others reroute.
- **Liking places:** a small per-camp score from good nights (music, jokes, meals) and bad ones (attacks). Wanderers prefer camps they like.

## Stage 4: dialogue from their state (most traveller and fighter lines)
You want most lines to be about **how they feel and what they need, like real people**. Lines are built from the traveller's state at the moment of talking. Pick the strongest thing going on:
- **Needs:** "I'm starving, haven't eaten since Kakariko." / "My throat's dry as the Gerudo sands." / "Been walking alone all day; good to see a face."
- **Feelings about others:** "Have you seen my brother? He went toward Kakariko." / "If that loudmouth sits by me again tonight..." / "Me and Dorin go way back."
- **Their day:** "Got jumped on the east road this morning. Never again." / "Heading home to Ordon; two more nights." / "A new road opened past the bridge! I'm going to see it."
- **Mood:** fear after an attack, cheer after music, tiredness late in the day, grief over a dead friend.
- **Fighters:** the same, through a fighter's eyes: pride after a win, a grudge against a rival, wanting to protect a friend.
- Several variants per situation, drawn from each person's own rotation (`pick = slot*7 + turn`), so two people never say the same line.
- Written as a draft for you to review before it goes in.

## Notes
- **Where the current code lives:** `travellers.cpp` (`populate`/`placeTraveller`, `plan`, `newRoute`, `stepRestOrTown`, `edgeAllowed`, `refreshStory`, seats); `passer_prototype.cpp` (needs, bubbles, `tellJoke`, meals, fights, lines); `dialogue_marks.cpp` (current lines).
- **Saving:** the roster is already saved under `mod_data`. Home camp, the last camp slept at, links and danger memory get added to it. Raise the save version so old saves migrate.
