#include "people.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "d/d_com_inf_game.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"
#include "mods/svc/message.h"
#include "mods/svc/save.h"
#include "travellers.hpp"

namespace people {
namespace {

// --- names (a draft, for the user to approve) -------------------------------------------------------
// Hyrule-flavoured, none of them anyone's in the game. Men, younger women and old women draw from
// their own lists; a bard gets a bard's name.
constexpr const char* kMen[] = {
    "Brennic", "Haldo", "Orrin", "Tavish", "Corwen", "Pell", "Dunstan", "Marrek", "Fennick", "Gorran",
    "Ulric", "Bram", "Teodor", "Kasimir", "Wendel", "Rowan", "Edric", "Aldous", "Barnaby", "Torvin",
    "Jessop", "Hamish", "Lothar", "Mattias", "Osric", "Reynard", "Silas", "Thaddeus", "Vance", "Hobb",
};
constexpr const char* kBards[] = {"Tobin", "Linus", "Calloway", "Fennimore", "Jasper", "Merrick", "Piper", "Lyle"};
constexpr const char* kWomen[] = {
    "Maribel", "Elsie", "Tamsin", "Rhosyn", "Brielle", "Odette", "Wren", "Juniper", "Petra", "Signe",
};
constexpr const char* kOldWomen[] = {
    "Granny Mabe", "Old Hester", "Morwenna", "Grizelda", "Nanna Bea", "Ysolde", "Old Thessaly", "Mother Gwen",
};
constexpr int kMenN = sizeof(kMen) / sizeof(kMen[0]);
constexpr int kBardsN = sizeof(kBards) / sizeof(kBards[0]);
constexpr int kWomenN = sizeof(kWomen) / sizeof(kWomen[0]);
constexpr int kOldN = sizeof(kOldWomen) / sizeof(kOldWomen[0]);
// one index space: men, then bards, women, old women
constexpr int kNameCount = kMenN + kBardsN + kWomenN + kOldN;
const char* nameAt(int i) {
    if (i < 0) return "";
    if (i < kMenN) return kMen[i];
    i -= kMenN;
    if (i < kBardsN) return kBards[i];
    i -= kBardsN;
    if (i < kWomenN) return kWomen[i];
    i -= kWomenN;
    return i < kOldN ? kOldWomen[i] : "";
}

constexpr const char* kTraitNames[kTraitCount] = {
    "hot-headed", "proud", "greedy", "cowardly", "kind", "gossip", "grumpy", "jokester", "generous", "brave",
    "cheerful", "shy",
};
// traits that can't go together
constexpr std::array<std::pair<u16, u16>, 4> kOpposites{{
    {T_GREEDY, T_GENEROUS}, {T_COWARD, T_BRAVE}, {T_GRUMPY, T_CHEERFUL}, {T_SHY, T_GOSSIP},
}};

bool oldWoman(int type) { return type == 20 || type == 27; }
bool woman(int type) { return oldWoman(type) || type == 28 || type == 29; }
constexpr int kBardBody = 4;

std::array<Person, kCount> g_people{};
bool g_ready = false;
bool g_dirty = false;
int g_dirtyAge = 0;
u32 g_seed = 0x5EED1234u;
u32 rnd() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

// --- the save blob ----------------------------------------------------------------------------------
constexpr const char* kBlob = "dt_people";
constexpr u32 kMagic = 0x44545050u;   // "DTPP"
constexpr u16 kVersion = 1;
constexpr int kRecord = 10;            // bytes per person
void encode(std::vector<u8>& out) {
    out.clear();
    auto put32 = [&](u32 v) { for (int k = 3; k >= 0; --k) out.push_back(static_cast<u8>(v >> (8 * k))); };
    auto put16 = [&](u16 v) { out.push_back(static_cast<u8>(v >> 8)); out.push_back(static_cast<u8>(v)); };
    put32(kMagic);
    put16(kVersion);
    put16(kCount);
    for (const Person& p : g_people) {
        out.push_back(p.used ? 1 : 0);
        out.push_back(p.type);
        out.push_back(p.name);
        put16(p.traits);
        out.push_back(static_cast<u8>(p.home));
        out.push_back(static_cast<u8>(p.slept));
        out.push_back(p.metLink ? 1 : 0);
        put16(p.since);
    }
}
bool decode(const u8* d, size_t n) {
    if (n < 8) return false;
    const u32 magic = (u32(d[0]) << 24) | (u32(d[1]) << 16) | (u32(d[2]) << 8) | d[3];
    const u16 ver = static_cast<u16>((d[4] << 8) | d[5]);
    const u16 count = static_cast<u16>((d[6] << 8) | d[7]);
    if (magic != kMagic || ver != kVersion || count != kCount || n < 8 + size_t(kRecord) * kCount) return false;
    const u8* r = d + 8;
    for (Person& p : g_people) {
        p.used = r[0] != 0;
        p.type = r[1];
        p.name = r[2];
        p.traits = static_cast<u16>((r[3] << 8) | r[4]);
        p.home = static_cast<s8>(r[5]);
        p.slept = static_cast<s8>(r[6]);
        p.metLink = r[7] != 0;
        p.since = static_cast<u16>((r[8] << 8) | r[9]);
        r += kRecord;
    }
    return true;
}
void writeBlob() {
    if (svc_save == nullptr) return;
    std::vector<u8> bytes;
    encode(bytes);
    if (svc_save->set_blob(mod_ctx, kBlob, bytes.data(), bytes.size()) != MOD_OK) {
        travellers::diagLine("people  could not keep the roster with the save (no slot yet?)");
    }
}

void (*g_onFresh)() = nullptr;
void (*g_onLoaded)() = nullptr;
void onNewSave(ModContext*, uint32_t, void*) {
    travellers::diagLine("people  new save: a new cast");
    g_ready = false;
    if (g_onFresh) g_onFresh();
}
void onSaveLoaded(ModContext*, uint32_t slot, void*) {
    size_t size = 0;
    std::vector<u8> buf;
    bool ok = false;
    if (svc_save != nullptr && svc_save->get_blob(mod_ctx, kBlob, nullptr, &size) == MOD_OK && size > 0) {
        buf.resize(size);
        ok = svc_save->get_blob(mod_ctx, kBlob, buf.data(), &size) == MOD_OK && decode(buf.data(), size);
    }
    if (ok) {
        g_ready = true;
        int n = 0;
        for (const Person& p : g_people) n += p.used ? 1 : 0;
        travellers::diagLine("people  save %u loaded: %d people", slot, n);
        if (g_onLoaded) g_onLoaded();
    } else {
        travellers::diagLine("people  save %u has no roster (an older save): a new cast", slot);
        g_ready = false;
        if (g_onFresh) g_onFresh();
    }
}

// --- talking to Link ---------------------------------------------------------------------------------
mods::flow::RegisteredMessage g_talkMsg;
std::vector<mods::flow::MessageOverride> g_overrides;
u16 g_talkNode = 0xFFFF;
int g_speaker = -1;
bool g_speakerNew = false;   // this talk is their introduction
std::vector<u8> g_text;

// What they say, by the first of their traits: when they first meet Link (introducing themselves; their
// name goes where "%s" is) and after that. A draft, for the user to review; Stage 4 replaces the "after"
// lines with talk about how they're doing, the people round them and the world.
struct Lines { const char* intro; const char* again; };
constexpr Lines kByTrait[kTraitCount] = {
    {"What? ...Oh. A traveller. I'm %s.\nDon't step on my bedroll.", "You again. Fine. What is it?"},
    {"You're speaking with %s. You may\nhave heard of me. ...No? Well.\nYou will.", "Ah, my admirer returns."},
    {"Name's %s. Got anything worth\nselling? I'm buying. Cheap.", "Back to sell me something, I hope?"},
    {"Ah! Oh... you're not a monster.\nI'm %s. Sorry. Jumpy.", "Oh, it's only you. Phew."},
    {"Hello there! I'm %s. You look\ntired. Sit by the fire a while.", "Good to see you again! Keeping well?"},
    {"Oh, a new face! I'm %s. Now, have\nyou heard...? No? Stick around.", "Oh, it's you! You'll never guess\nwhat I heard..."},
    {"%s. That's my name. That's all\nyou're getting.", "Hmph. Still around, are you."},
    {"I'm %s! Why did the Bulblin cross\nthe road? ...Nobody knows. They\njust do that.", "Hey, it's my favourite audience!"},
    {"Welcome, friend! I'm %s. If you\nneed anything, just ask.", "Friend! Come, warm yourself."},
    {"%s, at your service. If trouble\ncomes, stand behind me.", "Good to see you on your feet."},
    {"Hiya! I'm %s! Isn't it a lovely\nday for the road?", "Hiya again! What a day, huh?"},
    {"Um... hello. I'm %s.\n...That's all.", "...Oh. Hello again."},
};
int firstTrait(u16 traits) {
    for (int b = 0; b < kTraitCount; ++b) if (traits & (1u << b)) return b;
    return 10;   // (none: cheerful)
}

bool overrideFn(ModContext*, const MessageOverrideContext*, MessageTextData* out, void*) {
    const Person* p = get(g_speaker);
    if (p == nullptr) return false;
    const Lines& l = kByTrait[firstTrait(p->traits)];
    mods::flow::MessageBuilder b;
    b.box_kind(MESSAGE_BOX_TALK);
    if (g_speakerNew) {
        // the intro, with their name in the game's orange (as it shows names that matter)
        const char* fmt = l.intro;
        const char* at = std::strstr(fmt, "%s");
        if (at != nullptr) {
            b.text(std::string(fmt, at - fmt));
            b.text_color(MESSAGE_COLOR_ORANGE).text(nameAt(p->name)).text_color(MESSAGE_COLOR_DEFAULT);
            b.text(at + 2);
        } else {
            b.text(fmt);
        }
    } else {
        b.text(l.again);
    }
    g_text = b.build(MESSAGE_LANGUAGE_ENGLISH).text();
    out->text = g_text.data();
    out->text_size = g_text.size();
    return true;
}

}  // namespace

const char* traitName(int bit) { return bit >= 0 && bit < kTraitCount ? kTraitNames[bit] : "?"; }

Person* get(int slot) {
    if (slot < 0 || slot >= kCount || !g_people[slot].used) return nullptr;
    return &g_people[slot];
}
bool isPerson(int slot) { return get(slot) != nullptr; }
const char* name(int slot) {
    const Person* p = get(slot);
    return p != nullptr ? nameAt(p->name) : "";
}
bool ready() { return g_ready; }

void make(int slot, u8 type, s8 home, bool fighter) {
    if (slot < 0 || slot >= kCount) return;
    g_seed ^= static_cast<u32>(slot * 2654435761u) ^ static_cast<u32>(dComIfGs_getDate() * 40503u) ^ g_dirtyAge;
    Person p;
    p.used = true;
    p.type = type;
    p.home = home;
    p.slept = home;
    p.since = static_cast<u16>(dComIfGs_getDate());
    // a name nobody else on the roster has, from their own list
    int lo = 0, n = kMenN;
    if (type == kBardBody) { lo = kMenN; n = kBardsN; }
    else if (oldWoman(type)) { lo = kMenN + kBardsN + kWomenN; n = kOldN; }
    else if (woman(type)) { lo = kMenN + kBardsN; n = kWomenN; }
    const int start = static_cast<int>(rnd() % static_cast<u32>(n));
    p.name = static_cast<u8>(lo + start);
    for (int k = 0; k < n; ++k) {
        const int cand = lo + (start + k) % n;
        bool taken = false;
        for (int s = 0; s < kCount; ++s) {
            if (s != slot && g_people[s].used && g_people[s].name == cand) { taken = true; break; }
        }
        if (!taken) { p.name = static_cast<u8>(cand); break; }
    }
    // two or three traits, never a pair of opposites; fighters are never cowards
    const int want = 2 + static_cast<int>(rnd() % 2u);
    for (int tries = 0; tries < 40 && __builtin_popcount(p.traits) < want; ++tries) {
        const u16 t = static_cast<u16>(1u << (rnd() % kTraitCount));
        if (p.traits & t) continue;
        if (fighter && t == T_COWARD) continue;
        bool clash = false;
        for (auto [a, b] : kOpposites) {
            if ((t == a && (p.traits & b)) || (t == b && (p.traits & a))) clash = true;
        }
        if (!clash) p.traits |= t;
    }
    g_people[slot] = p;
    std::string traits;
    for (int b = 0; b < kTraitCount; ++b) {
        if (p.traits & (1u << b)) { if (!traits.empty()) traits += ", "; traits += kTraitNames[b]; }
    }
    travellers::diagLine("people  slot %d: %s (body %d), %s, home camp %d", slot, nameAt(p.name), type,
                         traits.c_str(), home);
    g_ready = true;
    markChanged();
}

void markChanged() { g_dirty = true; }

void tick() {
    ++g_dirtyAge;
    if (g_dirty && g_dirtyAge % 30 == 0) {
        g_dirty = false;
        writeBlob();
    }
}

void install(void (*onFresh)(), void (*onLoaded)()) {
    g_onFresh = onFresh;
    g_onLoaded = onLoaded;
    if (svc_save != nullptr) svc_save->observe_saves(mod_ctx, onNewSave, onSaveLoaded, nullptr, nullptr, nullptr);
}

ModResult registerTalk(mods::flow::GraphBuilder& graph, u16 group, const MessageLanguage* langs, int nLangs) {
    mods::flow::MessageBuilder b;
    b.box_kind(MESSAGE_BOX_TALK).text("...");
    std::vector<mods::flow::MessageVariant> v;
    for (int k = 0; k < nLangs; ++k) v.push_back(b.build(langs[k]));
    g_talkMsg = mods::flow::register_message(group, v);
    if (!g_talkMsg) return g_talkMsg.result();
    for (int k = 0; k < nLangs; ++k) {
        auto o = mods::flow::override_message_fn(group, g_talkMsg.id(), langs[k], overrideFn, nullptr);
        if (!o) return o.result();
        g_overrides.push_back(std::move(o));
    }
    auto n = graph.add_message(g_talkMsg.id());
    n.next(mods::flow::kEnd);
    g_talkNode = n.id();
    return MOD_OK;
}

u16 talkNode() { return g_talkNode; }

void talkStarted(int slot) {
    g_speaker = slot;
    Person* p = get(slot);
    g_speakerNew = p != nullptr && !p->metLink;
    if (p != nullptr && !p->metLink) {
        p->metLink = true;
        markChanged();
        travellers::diagLine("people  Link meets %s (slot %d)", nameAt(p->name), slot);
    }
}

}  // namespace people
