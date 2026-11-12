#pragma once

#include <cstdio>

// In-game scouting tool for picking camp, Beedle's tent and road-network spots.
namespace spot_picker {
void initialize();
void update();
// Opens a file in the mod's data folder (UTF-8 safe). Returns nullptr if there's no data folder.
std::FILE* openDataFile(const char* name, const char* mode);
}  // namespace spot_picker
