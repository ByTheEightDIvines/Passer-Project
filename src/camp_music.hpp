#pragma once
// Campfire songs: the bard's guitar with the campers' singing, from the seven three-stem loops in
// res/music. Only one fire is ever in earshot, so one song plays at a time: when the bard starts, that
// song's three stems are loaded into three borrowed sound slots and started together (so they stay in
// step for good), and each singing stem's volume follows how many men and women are singing along.
class daNpcPasser_c;
namespace camp_music {
bool install();                                       // once, from mod_initialize
void tick();                                          // once a frame
void bardPlays(daNpcPasser_c* bard);                  // each frame the bard is playing
void singsAlong(daNpcPasser_c* singer);               // each frame a camper is singing along
bool isOurWave(unsigned wave);                        // for the wave lookup rescue (voice_banks)
}
