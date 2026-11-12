// Spot picker: a scouting tool for choosing camp, tent and road spots in-game.
//
// Stand Link where you want something and log the spot, either from this mod's page in the
// Mods window or with a controller shortcut (hold L + R, then tap the D-pad):
//   Up    = road point (Beedle's / the travellers' road network)
//   Right = camp
//   Left  = Beedle's tent
//   Down  = undo the last spot
// Every spot is saved straight away to spots.tsv in this mod's data folder, so nothing is lost
// if the game closes, and "Copy all" puts the whole list on the clipboard to paste into chat.

#include "global.h"

#include "spot_picker.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "SSystem/SComponent/c_math.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_controller_pad.h"
#include "mods/service.hpp"
#include "mods/svc/host.h"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"
#include "mods/svc/actor.h"
#include "tent_actor.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
// Declared here instead of including <windows.h>, whose macros clash with the game headers.
extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(
    unsigned int codePage, unsigned long flags, const char* text, int textLength, wchar_t* out,
    int outLength);
constexpr unsigned int kCodePageUtf8 = 65001;
#endif

IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(HostService, svc_host);

namespace spot_picker {
namespace {

enum class Kind : int { Road = 0, Camp = 1, BeedleTent = 2 };
constexpr const char* kKindNames[] = {"road", "camp", "beedle_tent"};
constexpr const char* kKindLabels[] = {"Road point", "Camp", "Beedle's tent"};

struct Spot {
    Kind kind = Kind::Road;
    std::string stage;
    int room = -1;
    f32 x = 0.0f, y = 0.0f, z = 0.0f;
    int angleY = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
};

std::vector<Spot> g_spots;
std::string g_filePath;
UiElementHandle g_readout = 0;
UiListHandle g_list = 0;
bool g_listDirty = true;
bool g_hotkeysEnabled = true;

std::FILE* openFile(const std::string& path, const char* mode) {
#ifdef _WIN32
    // The data folder can sit under a user name with non-ASCII characters; narrow fopen would
    // fail there, so convert the UTF-8 path to UTF-16.
    auto widen = [](const char* text) {
        const int size = MultiByteToWideChar(kCodePageUtf8, 0, text, -1, nullptr, 0);
        std::wstring wide(size > 0 ? size - 1 : 0, L'\0');
        if (size > 0) {
            MultiByteToWideChar(kCodePageUtf8, 0, text, -1, wide.data(), size);
        }
        return wide;
    };
    return _wfopen(widen(path.c_str()).c_str(), widen(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

int countOf(Kind kind) {
    int count = 0;
    for (const auto& spot : g_spots) {
        count += spot.kind == kind ? 1 : 0;
    }
    return count;
}

// Tab-separated, one spot per line: easy for a script to read back and for a person to skim.
std::string toLine(const Spot& spot) {
    char line[256];
    std::snprintf(line, sizeof(line), "%s\t%s\t%d\t%.1f\t%.1f\t%.1f\t%d\t%d\t%02d:%02d",
                  kKindNames[static_cast<int>(spot.kind)], spot.stage.c_str(), spot.room, spot.x,
                  spot.y, spot.z, spot.angleY, spot.day, spot.hour, spot.minute);
    return line;
}

std::string describe(const Spot& spot, int number) {
    char text[200];
    std::snprintf(text, sizeof(text), "%d. %s - %s room %d (%.0f, %.0f, %.0f)", number,
                  kKindLabels[static_cast<int>(spot.kind)], spot.stage.c_str(), spot.room, spot.x,
                  spot.y, spot.z);
    return text;
}

void saveAll() {
    if (g_filePath.empty()) {
        return;
    }
    std::FILE* file = openFile(g_filePath, "wb");
    if (file == nullptr) {
        svc_log->warn(mod_ctx, "spot picker: could not write spots.tsv");
        return;
    }
    std::fputs("# kind\tstage\troom\tx\ty\tz\tangleY\tday\ttime\n", file);
    for (const auto& spot : g_spots) {
        std::fputs((toLine(spot) + "\n").c_str(), file);
    }
    std::fclose(file);
}

void loadAll() {
    g_spots.clear();
    std::FILE* file = openFile(g_filePath, "rb");
    if (file == nullptr) {
        return;  // first run: nothing logged yet
    }
    char line[512];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        char kind[32] = {};
        char stage[16] = {};
        Spot spot;
        if (std::sscanf(line, "%31[^\t]\t%15[^\t]\t%d\t%f\t%f\t%f\t%d\t%d\t%d:%d", kind, stage,
                        &spot.room, &spot.x, &spot.y, &spot.z, &spot.angleY, &spot.day,
                        &spot.hour, &spot.minute) != 10) {
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            if (std::strcmp(kind, kKindNames[i]) == 0) {
                spot.kind = static_cast<Kind>(i);
            }
        }
        spot.stage = stage;
        g_spots.push_back(spot);
    }
    std::fclose(file);
}

void toast(const char* title, const std::string& body) {
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.title_rml = title;
    desc.body_rml = body.c_str();
    desc.duration_ms = 2500;
    svc_ui->push_toast(mod_ctx, &desc);
}

// Link's spot right now, or false when there is no Link to measure (title screen, loading).
bool currentSpot(Spot& out) {
    auto* player = dComIfGp_getPlayer(0);
    const char* stage = dComIfGp_getStartStageName();
    if (player == nullptr || stage == nullptr || stage[0] == '\0') {
        return false;
    }
    out.stage = stage;
    out.room = dComIfGp_roomControl_getStayNo();
    out.x = player->current.pos.x;
    out.y = player->current.pos.y;
    out.z = player->current.pos.z;
    out.angleY = static_cast<u16>(player->shape_angle.y);
    out.day = dComIfGs_getDate();
    out.hour = dKy_getdaytime_hour();
    out.minute = dKy_getdaytime_minute();
    return true;
}

void logSpot(Kind kind) {
    Spot spot;
    if (!currentSpot(spot)) {
        toast("Spot picker", "Can't log a spot right now: Link isn't in the world.");
        return;
    }
    spot.kind = kind;
    g_spots.push_back(spot);
    saveAll();
    g_listDirty = true;

    const int number = countOf(kind);
    char title[64];
    std::snprintf(title, sizeof(title), "%s #%d logged", kKindLabels[static_cast<int>(kind)],
                  number);
    char body[128];
    std::snprintf(body, sizeof(body), "%s room %d (%.0f, %.0f, %.0f)", spot.stage.c_str(),
                  spot.room, spot.x, spot.y, spot.z);
    toast(title, body);
    svc_log->info(mod_ctx, ("spot picker: " + toLine(spot)).c_str());
}

void undoLast() {
    if (g_spots.empty()) {
        toast("Spot picker", "Nothing to undo.");
        return;
    }
    const Spot removed = g_spots.back();
    g_spots.pop_back();
    saveAll();
    g_listDirty = true;
    toast("Spot removed", describe(removed, static_cast<int>(g_spots.size()) + 1));
}

void copyAll() {
    std::string text = "# kind\tstage\troom\tx\ty\tz\tangleY\tday\ttime\n";
    for (const auto& spot : g_spots) {
        text += toLine(spot) + "\n";
    }
    if (svc_ui->set_clipboard_text(mod_ctx, text.c_str()) == MOD_OK) {
        char body[64];
        std::snprintf(body, sizeof(body), "%d spots copied to the clipboard.",
                      static_cast<int>(g_spots.size()));
        toast("Spot picker", body);
    } else {
        toast("Spot picker", "Couldn't reach the clipboard. The list is in spots.tsv too.");
    }
}

void clearAll() {
    g_spots.clear();
    saveAll();
    g_listDirty = true;
    toast("Spot picker", "All spots cleared.");
}

void refreshList() {
    if (g_list == 0 || !g_listDirty) {
        return;
    }
    std::vector<std::string> labels;
    labels.reserve(g_spots.size());
    for (size_t i = 0; i < g_spots.size(); ++i) {
        labels.push_back(describe(g_spots[i], static_cast<int>(i) + 1));
    }
    std::vector<UiListItem> items(g_spots.size());
    for (size_t i = 0; i < g_spots.size(); ++i) {
        items[i] = UI_LIST_ITEM_INIT;
        items[i].key = i + 1;
        items[i].label = labels[i].c_str();
    }
    svc_ui->list_set_items(mod_ctx, g_list, items.data(), items.size());
    g_listDirty = false;
}

// --- Mods window page ---

void onLogRoad(ModContext*, void*) { logSpot(Kind::Road); }
void onLogCamp(ModContext*, void*) { logSpot(Kind::Camp); }
void onLogTent(ModContext*, void*) { logSpot(Kind::BeedleTent); }
void onUndo(ModContext*, void*) { undoLast(); }
void onCopy(ModContext*, void*) { copyAll(); }

// Clearing needs a second press within a few seconds, so one stray click can't wipe a scouting
// session.
int g_clearArmedFrames = 0;
void onClear(ModContext*, void*) {
    if (g_clearArmedFrames > 0) {
        g_clearArmedFrames = 0;
        clearAll();
    } else {
        g_clearArmedFrames = 180;
        toast("Spot picker", "Press Clear all again to wipe every spot.");
    }
}

void getHotkeys(ModContext*, void*, UiControlValue* value) { value->bool_value = g_hotkeysEnabled; }
void setHotkeys(ModContext*, void*, const UiControlValue* value) {
    g_hotkeysEnabled = value->bool_value;
}

void onListPressed(ModContext*, UiListHandle, uint64_t key, void*) {
    if (key == 0 || key > g_spots.size()) {
        return;
    }
    const std::string line = toLine(g_spots[key - 1]);
    if (svc_ui->set_clipboard_text(mod_ctx, line.c_str()) == MOD_OK) {
        toast("Spot copied", describe(g_spots[key - 1], static_cast<int>(key)));
    }
}

// --- Test placement: drop a tent in front of Link to see it in the game before camps exist ---
std::vector<ActorId> g_testTents;
void spawnTestTent(u8 kind) {
    auto* player = dComIfGp_getPlayer(0);
    if (player == nullptr) {
        toast("Test tent", "Link isn't in the world right now.");
        return;
    }
    const s16 facing = player->shape_angle.y;
    const f32 dist = kind == PSR_TENT_A ? 320.0f : (kind == PSR_TENT_WALL ? 480.0f : 560.0f);
    ActorSpawnParams params{};
    params.parameters = kind;
    params.argument = -1;
    params.room_num = static_cast<int8_t>(dComIfGp_roomControl_getStayNo());
    params.position.x = player->current.pos.x + cM_ssin(facing) * dist;
    params.position.y = player->current.pos.y;
    params.position.z = player->current.pos.z + cM_scos(facing) * dist;
    params.angle.y = static_cast<s16>(facing + 0x8000);   // doorway faces Link
    params.scale.x = params.scale.y = params.scale.z = 1.0f;
    ActorId id{};
    if (svc_actor->create_actor_from_name(mod_ctx, PSR_TENT_NAME, &params, &id) == MOD_OK) {
        g_testTents.push_back(id);
        toast("Test tent placed", kind == PSR_TENT_A ? "Small tent" : (kind == PSR_TENT_WALL ? "Big tent" : "Beedle's tent"));
    } else {
        toast("Test tent", "Couldn't place it here.");
    }
}
void onTestA(ModContext*, void*) { spawnTestTent(PSR_TENT_A); }
void onTestWall(ModContext*, void*) { spawnTestTent(PSR_TENT_WALL); }
void onTestBeedle(ModContext*, void*) { spawnTestTent(PSR_TENT_BEEDLE); }
void onTestClear(ModContext*, void*) {
    for (ActorId id : g_testTents) {
        svc_actor->delete_actor(mod_ctx, id);
    }
    g_testTents.clear();
    toast("Test tents", "Removed.");
}

ModResult addButton(UiElementHandle pane, const char* label, UiPressedFn fn, const char* help) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_BUTTON;
    desc.label = label;
    desc.help_rml = help;
    desc.on_pressed = fn;
    return svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

ModResult buildPanel(ModContext*, UiElementHandle pane, void*, ModError*) {
    g_readout = 0;
    g_list = 0;
    svc_ui->pane_add_section(mod_ctx, pane, "Spot picker (scouting)");
    svc_ui->pane_add_text(mod_ctx, pane,
                          "Shortcut: hold L + R, then D-pad Up = road point, Right = camp, "
                          "Left = Beedle's tent, Down = undo.",
                          nullptr);
    svc_ui->pane_add_text(mod_ctx, pane, "", &g_readout);

    UiControlDesc toggle = UI_CONTROL_DESC_INIT;
    toggle.kind = UI_CONTROL_TOGGLE;
    toggle.label = "Controller shortcut";
    toggle.get = getHotkeys;
    toggle.set = setHotkeys;
    svc_ui->pane_add_control(mod_ctx, pane, &toggle, nullptr);

    addButton(pane, "Log road point here", onLogRoad, "A point on the road network.");
    addButton(pane, "Log camp here", onLogCamp, "Where a travellers' camp fire should go.");
    addButton(pane, "Log Beedle's tent here", onLogTent, "Where Beedle's big tent should go.");
    addButton(pane, "Undo last", onUndo, nullptr);
    addButton(pane, "Copy all to clipboard", onCopy,
              "Copies every spot as text, ready to paste into chat.");
    addButton(pane, "Clear all", onClear, "Press twice to wipe every spot.");

    svc_ui->pane_add_section(mod_ctx, pane, "Test tents (placed in front of Link)");
    addButton(pane, "Small tent here", onTestA, nullptr);
    addButton(pane, "Big tent here", onTestWall, nullptr);
    addButton(pane, "Beedle's tent here", onTestBeedle, nullptr);
    addButton(pane, "Remove test tents", onTestClear, nullptr);

    svc_ui->pane_add_section(mod_ctx, pane, "Logged spots (press one to copy it)");
    UiListDesc list = UI_LIST_DESC_INIT;
    list.on_pressed = onListPressed;
    svc_ui->pane_add_list(mod_ctx, pane, &list, &g_list);
    g_listDirty = true;
    refreshList();
    return MOD_OK;
}

ModResult updatePanel(ModContext*, void*, ModError*) {
    if (g_readout != 0) {
        Spot here;
        char text[200];
        if (currentSpot(here)) {
            std::snprintf(text, sizeof(text),
                          "Link: %s room %d (%.0f, %.0f, %.0f), day %d %02d:%02d. Logged: %d road, "
                          "%d camps, %d tents.",
                          here.stage.c_str(), here.room, here.x, here.y, here.z, here.day,
                          here.hour, here.minute, countOf(Kind::Road), countOf(Kind::Camp),
                          countOf(Kind::BeedleTent));
        } else {
            std::snprintf(text, sizeof(text), "Link isn't in the world right now.");
        }
        svc_ui->elem_set_text(mod_ctx, g_readout, text);
    }
    refreshList();
    return MOD_OK;
}

}  // namespace

std::FILE* openDataFile(const char* name, const char* mode) {
    const char* dir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dir) != MOD_OK || dir == nullptr) {
        svc_log->warn(mod_ctx, "openDataFile: no data folder");
        return nullptr;
    }
    const std::string path = std::string(dir) + "/" + name;
    std::FILE* f = openFile(path, mode);
    if (f == nullptr) svc_log->warn(mod_ctx, ("openDataFile: fopen failed for " + path).c_str());
    return f;
}

void initialize() {
    const char* dir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dir) == MOD_OK && dir != nullptr) {
        g_filePath = std::string(dir) + "/spots.tsv";
        loadAll();
    } else {
        svc_log->warn(mod_ctx, "spot picker: no data folder; spots won't be saved to disk");
    }

    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = buildPanel;
    panel.update = updatePanel;
    svc_ui->register_mods_panel(mod_ctx, &panel);

    char line[96];
    std::snprintf(line, sizeof(line), "spot picker ready, %d spots loaded",
                  static_cast<int>(g_spots.size()));
    svc_log->info(mod_ctx, line);
}

void update() {
    if (g_clearArmedFrames > 0) {
        --g_clearArmedFrames;
    }
    if (!g_hotkeysEnabled) {
        return;
    }
    // Don't react while a menu or window has the controller.
    bool menuOpen = false;
    if (svc_ui->is_any_document_visible(mod_ctx, &menuOpen) == MOD_OK && menuOpen) {
        return;
    }
    // Edge-detect the D-pad ourselves from the held buttons: the mod update can run more often
    // than the game reads the pad, so the game's one-frame "pressed" flag could fire twice.
    static u32 previous = 0;
    const u32 held = mDoCPd_c::getHold(PAD_1);
    const u32 pressed = held & ~previous;
    previous = held;
    if ((held & PAD_TRIGGER_L) == 0 || (held & PAD_TRIGGER_R) == 0) {
        return;
    }
    if (pressed & PAD_BUTTON_UP) {
        logSpot(Kind::Road);
    } else if (pressed & PAD_BUTTON_RIGHT) {
        logSpot(Kind::Camp);
    } else if (pressed & PAD_BUTTON_LEFT) {
        logSpot(Kind::BeedleTent);
    } else if (pressed & PAD_BUTTON_DOWN) {
        undoLast();
    }
}

}  // namespace spot_picker
