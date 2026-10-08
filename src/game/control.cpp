#include "game/control.h"
#include "core/calls.h"
#include "game/fnv.h"
#include "game/log.h"
#include "game/rtti.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>
#include <vector>
#include <windows.h>

namespace sm64nv {

namespace {

const uintptr_t kInputGlobals = 0x011F35CC;
// the game reads a pad through this thunk, a jump the import slot or a plugin aims
const uintptr_t kPadThunk = 0x009F996E, kPadImport = 0x00FDF394;
const uintptr_t kOSGlobals = 0x011DEA0C;
// the game's own setting for a player without collision, read by the player every frame
// the console's toggle is for the whole game and stops anyone falling over while it is on
const uintptr_t kPlayerCollision = 0x011E0B64, kPlayerCollisionName = 0x0108B9BC;
const uintptr_t kSceneGraph = 0x011DEB7C;
const uintptr_t kCameraZoom = 0x011E0B5C;
const float kZoom = 250;
const uintptr_t kChaseSetting = 0x011CD568;
const uintptr_t kVtblSetting = 0x01012114;
const float kChase = 250;
const uintptr_t kPlayerSetPos = 0x00931620;
const uintptr_t kPlayerSaveSlot = 0x0108AA90;
const uintptr_t kPlayerSave = 0x009590F0;
// where the activate key fetches its sound for nothing to use, and what it calls there
const uintptr_t kNothingSoundCall = 0x009433B7;
const uint32_t kHasKeyboard = 1 << 2;
const uint8_t kBlockedControls = 0x01 | 0x08 | 0x10 | 0x40;
// mario needs the camera whatever the game's scenes want
const uint8_t kLooking = 0x02;

using SetControls = void(__thiscall *)(void *, bool, uint8_t);
using ToggleFirstPerson = bool(__thiscall *)(void *, bool);
using SetPos = void(__thiscall *)(void *, const Vec3 *);
using SaveGame = void(__thiscall *)(void *, uint32_t);
using SoundForm = void *(__cdecl *)();
using PadRead = DWORD(WINAPI *)(DWORD, void *);
using MenuMode = uint8_t(__cdecl *)();

template <typename T> T &field(void *obj, size_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(obj) + off); }
uintptr_t vslot(void *obj, size_t off) { return (*static_cast<uintptr_t **>(obj))[off / 4]; }

std::string hex(uintptr_t v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%08X", (unsigned)v);
    return buf;
}

uint8_t *os_globals() { return *reinterpret_cast<uint8_t **>(kOSGlobals); }

bool has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void *body() { return fnv::player()->renderState ? fnv::player()->renderState->niNode : nullptr; }

void *chase_setting() { return reinterpret_cast<void *>(kChaseSetting); }

// a setting keeps its value one word in and its name after that
uint8_t &player_noclip() { return field<uint8_t>(reinterpret_cast<void *>(kPlayerCollision), 0x04); }

bool culled(void *node) { return field<uint32_t>(node, 0x30) & 1; }

std::span<void *> body_parts() {
    if (!body()) return {};
    return {field<void **>(body(), 0xA0), field<uint16_t>(body(), 0xA6)};
}

// outdoors the game shows the body's own node again every frame so mario hides its parts
std::vector<void *> g_hidden_parts;

bool parts_hidden() {
    std::span<void *> parts = body_parts();
    return !parts.empty() && std::all_of(parts.begin(), parts.end(), [](void *p) { return !p || culled(p); });
}

void show_body() {
    for (void *part : body_parts())
        if (part && std::count(g_hidden_parts.begin(), g_hidden_parts.end(), part)) field<uint32_t>(part, 0x30) &= ~1u;
    g_hidden_parts.clear();
}

// the courier's own state while mario has the player
ControlState g_courier;
bool g_taken;
// the game switched looking off while mario had the player and gets that back on release
bool g_looking_off;
// and so with a package of the game's that walks the player, which stops its look code too
bool g_walked;

// the save writes the control flags, pov and zoom so it gets the courier's
void __thiscall save_player(void *p, uint32_t changed) {
    uint8_t controls = field<uint8_t>(p, 0x680), third = field<uint8_t>(p, 0x64A);
    float zoom = *reinterpret_cast<float *>(kCameraZoom);
    if (g_taken) {
        field<uint8_t>(p, 0x680) = (controls & ~(kBlockedControls & ~g_courier.controls)) | (g_looking_off ? kLooking : 0);
        field<uint8_t>(p, 0x64A) = g_courier.third;
        *reinterpret_cast<float *>(kCameraZoom) = g_courier.zoom;
    }
    logf("save player taken=%d controls=%02X written=%02X", g_taken, controls, field<uint8_t>(p, 0x680));
    reinterpret_cast<SaveGame>(kPlayerSave)(p, changed);
    field<uint8_t>(p, 0x680) = controls, field<uint8_t>(p, 0x64A) = third;
    *reinterpret_cast<float *>(kCameraZoom) = zoom;
}

int g_hushed;

PadRead g_read_pad;
// the first pad as it really is
GamepadState g_pad;
PacketGate g_packets;

// a, x and the triggers are mario's, the game would act on them or click at them
// in a menu the game gets the whole pad
DWORD WINAPI read_pad_for_game(DWORD index, void *state) {
    DWORD failed = g_read_pad(index, state);
    if (failed || index) return failed;
    // the report follows a packet number
    auto *pad = reinterpret_cast<GamepadState *>(static_cast<uint8_t *>(state) + 4);
    if (g_packets.fresh(*static_cast<uint32_t *>(state))) g_pad = *pad;
    if (g_taken && !reinterpret_cast<MenuMode>(0x00702360)()) *pad = game_share(g_pad);
    return failed;
}

// a call of the game's that ours sits on, what it called before is passed on to
struct CallHook {
    const char *name;
    uintptr_t site;
    void *ours;
    uintptr_t next;
};

std::string module_of(uintptr_t addr) {
    HMODULE mod = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &mod))
        GetModuleFileNameA(mod, path, sizeof path);
    const char *base = strrchr(path, '\\');
    return base ? base + 1 : path;
}

// a plugin's call put there before or after us is the one ours passes on to
bool claim_call(CallHook &h, std::string &why) {
    auto *site = reinterpret_cast<uint8_t *>(h.site);
    uint32_t was = call_target(h.site, site);
    if (was == reinterpret_cast<uintptr_t>(h.ours)) return true;
    if (!was) {
        why = "code at " + hex(h.site) + " is no call";
        return false;
    }
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
        why = "protect error=" + std::to_string(GetLastError());
        return false;
    }
    h.next = was;
    uint8_t code[5];
    call_code(h.site, reinterpret_cast<uintptr_t>(h.ours), code);
    memcpy(site + 1, code + 1, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    logf("control hook name=%s site=%08X next=%08X module=%s", h.name, (unsigned)h.site, (unsigned)h.next, module_of(h.next).c_str());
    return true;
}

// with movement off the game clicks at its activate key
void *__cdecl nothing_sound();
CallHook g_sound_call{"activate_sound", kNothingSoundCall, reinterpret_cast<void *>(&nothing_sound), 0};

void *__cdecl nothing_sound() {
    if (!g_taken) return reinterpret_cast<SoundForm>(g_sound_call.next)();
    g_hushed++;
    return nullptr;
}

void __thiscall set_control_flags(void *p, uint8_t flags);
CallHook g_control_call{"control_view", 0x0095F577, reinterpret_cast<void *>(&set_control_flags), 0};

void __thiscall set_control_flags(void *p, uint8_t flags) {
    using SetFlags = void(__thiscall *)(void *, uint8_t);
    reinterpret_cast<SetFlags>(g_control_call.next)(p, flags);
    // control changes request first person even when a door script needs to activate again
    if (g_taken && p == fnv::player() && !reinterpret_cast<MenuMode>(0x00702360)() && !field<uint8_t>(p, 0x64C)) {
        float zoom = *reinterpret_cast<float *>(kCameraZoom);
        reinterpret_cast<ToggleFirstPerson>(0x00950110)(p, false);
        *reinterpret_cast<float *>(kCameraZoom) = zoom;
    }
}

bool chase_setting_ok() {
    const char *name = field<const char *>(chase_setting(), 0x08);
    return fnv::vtbl_of(chase_setting()) == kVtblSetting && name && !strcmp(name, "fChaseCameraMax");
}

}

bool read_game_pad(Pad &pad, bool &toggle, bool &activate, bool &options) {
    auto *g = *reinterpret_cast<uint8_t **>(kInputGlobals);
    if (!g || !(field<uint32_t>(g, 0x04) & kHasKeyboard)) return false;
    pad = merge_pads(read_pad(g + 0x18F8, g + 0x1B30), read_gamepad(g_pad));
    toggle = toggle_held(g + 0x18F8, g_pad);
    activate = activate_held(g + 0x18F8, g_pad);
    options = options_held(g + 0x18F8);
    return true;
}

bool hook_player_save(std::string &why) {
    auto *slot = reinterpret_cast<uintptr_t *>(kPlayerSaveSlot);
    if (*slot != kPlayerSave) {
        why = "slot=" + hex(*slot) + " want=" + hex(kPlayerSave);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) {
        why = "protect error=" + std::to_string(GetLastError());
        return false;
    }
    *slot = reinterpret_cast<uintptr_t>(&save_player);
    VirtualProtect(slot, 4, old, &old);
    return true;
}

// the game's own movement off and the view behind the player, far enough back to see mario
void grip(void *p) {
    reinterpret_cast<SetControls>(0x0095F530)(p, true, kBlockedControls);
    reinterpret_cast<ToggleFirstPerson>(0x00950110)(p, false);
    // the switch leaves the camera at its 60 unit minimum which puts mario's cap in the lens
    *reinterpret_cast<float *>(kCameraZoom) = kZoom;
    // and the chase camera stops at 120 units which crops him
    field<float>(chase_setting(), 0x04) = kChase;
}

float look_stick() { return g_pad.rx / 32767.0f; }

std::string pad_mode() {
    auto *ui = *reinterpret_cast<uint8_t **>(0x011D8A80);
    // four bytes on the player stop the game's look code, the last for a package walking him
    char held[8];
    auto *p = reinterpret_cast<const uint8_t *>(fnv::player());
    snprintf(held, sizeof held, "%d%d%d%d", p[0x798] != 0, p[0x799] != 0, p[0x79A] != 0, p[0x79B] != 0);
    return "pad_active=" + std::to_string(*reinterpret_cast<uint8_t *>(0x011F35C8)) + " keys_and_mouse=" +
           std::to_string(ui ? ui[0x7D] : -1) + " held_by=" + held;
}

// what the thunk reaches now, ours when it already jumps to us
bool pad_reader(uintptr_t &to, std::string &why) {
    Jump j;
    auto *site = reinterpret_cast<const uint8_t *>(kPadThunk);
    if (!read_jump(site, kPadThunk, j) || (j.through_slot && j.to != kPadImport)) {
        why = "thunk at " + hex(kPadThunk) + " is not a jump the game or a plugin made";
        return false;
    }
    to = j.through_slot ? *reinterpret_cast<uintptr_t *>(j.to) : j.to;
    if (!to) why = "import slot empty";
    return to != 0;
}

bool hook_pad(std::string &why) {
    uintptr_t to;
    if (!pad_reader(to, why)) return false;
    if (to == reinterpret_cast<uintptr_t>(&read_pad_for_game)) return true;
    auto *site = reinterpret_cast<uint8_t *>(kPadThunk);
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
        why = "protect error=" + std::to_string(GetLastError());
        return false;
    }
    g_read_pad = reinterpret_cast<PadRead>(to);
    int32_t rel = (int32_t)(reinterpret_cast<uintptr_t>(&read_pad_for_game) - (kPadThunk + 5));
    site[0] = 0xE9;
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    logf("control pad hook thunk=%08X reads_through=%08X", (unsigned)kPadThunk, (unsigned)to);
    return true;
}

// a plugin loaded after this one may put its jump over the thunk, then ours goes over that
void keep_pad_hooked() {
    static bool said;
    std::string why;
    if (!hook_pad(why) && !std::exchange(said, true)) logf("refused: pad rehook %s", why.c_str());
}

bool hook_control_view(std::string &why) { return claim_call(g_control_call, why); }

bool hook_activate_sound(std::string &why) { return claim_call(g_sound_call, why); }

int take_hushed() { return std::exchange(g_hushed, 0); }


using ControlsOff = bool(__thiscall *)(void *, uint8_t);
// where an actor picking its blow asks whether the player's movement is off
const uintptr_t kCombatControlsCall = 0x008A04B2;

bool __thiscall controls_off_for_combat(void *p, uint8_t mask);
CallHook g_combat_call{"combat", kCombatControlsCall, reinterpret_cast<void *>(&controls_off_for_combat), 0};

bool __thiscall controls_off_for_combat(void *p, uint8_t mask) {
    return !g_taken && reinterpret_cast<ControlsOff>(g_combat_call.next)(p, mask);
}

bool hook_combat_check(std::string &why) { return claim_call(g_combat_call, why); }

using SayCombat = uint32_t(__thiscall *)(void *, void *, uint32_t, int, int, char, void *);
// the game's combat dialogue, which also speaks the courier's cries when hurt or dying
const uintptr_t kSayCombat = 0x009839B0;
// its frame setup, which moves to a trampoline as is
const uint8_t kSayCombatStart[10] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x8B, 0x94, 0xF1, 0x00};
SayCombat g_say_combat;
int g_quiet_voices;

// mario cries out for the courier, the courier's own voice would sound over him
uint32_t __thiscall say_combat(void *dialogue, void *actor, uint32_t topic, int type, int kind, char now, void *target) {
    if (g_taken && actor == fnv::player()) return g_quiet_voices++, 0;
    return g_say_combat(dialogue, actor, topic, type, kind, now, target);
}

bool hook_courier_voice(std::string &why) {
    auto *site = reinterpret_cast<uint8_t *>(kSayCombat);
    if (!std::equal(std::begin(kSayCombatStart), std::end(kSayCombatStart), site)) {
        why = "code at " + hex(kSayCombat) + " is not the game's own";
        return false;
    }
    auto *tramp = static_cast<uint8_t *>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    DWORD old;
    if (!tramp || !VirtualProtect(site, 10, PAGE_EXECUTE_READWRITE, &old)) {
        why = "memory error=" + std::to_string(GetLastError());
        return false;
    }
    memcpy(tramp, kSayCombatStart, 10);
    uint8_t back[5];
    call_code(reinterpret_cast<uintptr_t>(tramp) + 10, kSayCombat + 10, back);
    back[0] = 0xE9;
    memcpy(tramp + 10, back, 5);
    g_say_combat = reinterpret_cast<SayCombat>(tramp);
    uint8_t jump[5];
    call_code(kSayCombat, reinterpret_cast<uintptr_t>(&say_combat), jump);
    jump[0] = 0xE9;
    memcpy(site, jump, 5);
    memset(site + 5, 0x90, 5);
    VirtualProtect(site, 10, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 10);
    logf("control hook name=courier_voice site=%08X trampoline=%08X", (unsigned)kSayCombat, (unsigned)reinterpret_cast<uintptr_t>(tramp));
    return true;
}

int take_quiet_voices() { return std::exchange(g_quiet_voices, 0); }

void keep_calls_hooked() {
    static bool said;
    std::string why;
    for (CallHook *h : {&g_combat_call, &g_sound_call, &g_control_call})
        if (!claim_call(*h, why) && !std::exchange(said, true)) logf("refused: %s rehook %s", h->name, why.c_str());
}

bool take_player(const ControlState &courier, std::string &why) {
    fnv::TESObjectREFR *p = fnv::player();
    if (fnv::vtbl_of(p) != fnv::kVtblPlayerCharacter) why = "player vtbl";
    else if (vslot(p, 0x2A8) != kPlayerSetPos) why = "player setpos slot";
    else if (fnv::vtbl_of(body()) != fnv::kVtblBSFadeNode) why = "body vtbl=" + hex(fnv::vtbl_of(body()));
    else if (!chase_setting_ok()) why = "chase setting vtbl=" + hex(fnv::vtbl_of(chase_setting()));
    else if (field<uintptr_t>(reinterpret_cast<void *>(kPlayerCollision), 0x08) != kPlayerCollisionName) why = "collision setting name";

    if (!why.empty()) return false;
    grip(p);
    player_noclip() = 1;
    hide_body();
    g_courier = courier, g_taken = true;
    ControlState cs = control_state();
    if ((cs.controls & kBlockedControls) == kBlockedControls && cs.hidden && cs.noclip) return true;
    why = "readback controls=" + hex(cs.controls) + " hidden=" + std::to_string(cs.hidden) + " noclip=" + std::to_string(cs.noclip);
    return false;
}

void release_player(ControlState &saved) {
    void *p = fnv::player();
    g_taken = false;
    if (g_looking_off) saved.controls |= kLooking, reinterpret_cast<SetControls>(0x0095F530)(p, true, kLooking);
    if (g_walked) field<uint8_t>(p, 0x79B) = 1;
    if (g_looking_off || g_walked) logf("control handed back looking_off=%d walked=%d", g_looking_off, g_walked);
    g_looking_off = g_walked = false;
    reinterpret_cast<SetControls>(0x0095F530)(p, false, kBlockedControls & ~saved.controls);
    reinterpret_cast<ToggleFirstPerson>(0x00950110)(p, !saved.third);
    *reinterpret_cast<float *>(kCameraZoom) = saved.zoom;
    field<float>(chase_setting(), 0x04) = saved.chase;
    player_noclip() = saved.noclip;
    // going back to first person culls the body again by itself
    if (body()) field<uint32_t>(body(), 0x30) &= ~1u;
    show_body();
}

int hold_player() {
    void *p = fnv::player();
    uint8_t had = field<uint8_t>(p, 0x680);
    bool loose = (had & kBlockedControls) != kBlockedControls || !field<uint8_t>(p, 0x64A);
    uint8_t &walked = field<uint8_t>(p, 0x79B);
    if (!loose && !(had & kLooking) && !walked) return -1;
    if (had & kLooking) reinterpret_cast<SetControls>(0x0095F530)(p, false, kLooking), g_looking_off = true;
    if (loose) grip(p);
    int was = had | (walked ? 0x100 : 0);
    if (walked) walked = 0, g_walked = true;
    return was;
}

void *body_parent() { return body() ? field<void *>(body(), 0x18) : nullptr; }

bool player_position_pending() { return field<void *>(fnv::player(), 0x1EC) != nullptr; }

void move_player(Vec3 pos) {
    void *p = fnv::player();
    reinterpret_cast<SetPos>(vslot(p, 0x2A8))(p, &pos);
}

bool hide_body() {
    bool hid = false;
    for (void *part : body_parts()) {
        if (!part || culled(part)) continue;
        field<uint32_t>(part, 0x30) |= 1;
        // parts the game culled itself are not ours to show again
        if (!std::count(g_hidden_parts.begin(), g_hidden_parts.end(), part)) g_hidden_parts.push_back(part);
        hid = true;
    }
    return hid;
}

void focus_game() {
    HWND w = os_globals() ? field<HWND>(os_globals(), 0x08) : nullptr;
    if (!has_focus() && IsWindow(w)) SetForegroundWindow(w);
}

bool camera_pos(Vec3 &out) {
    void *sg = *reinterpret_cast<void **>(kSceneGraph);
    void *cam = sg ? field<void *>(sg, 0xAC) : nullptr;
    if (!rtti_is(cam, ".?AVNiCamera@@")) return false;
    out = field<Vec3>(cam, 0x8C);
    return true;
}

ControlState control_state() {
    void *p = fnv::player();
    return {field<uint8_t>(p, 0x680), player_noclip() != 0, field<uint8_t>(p, 0x64A) != 0,
            body() && (culled(body()) || parts_hidden()), *reinterpret_cast<float *>(kCameraZoom),
            field<float>(chase_setting(), 0x04), has_focus(), os_globals() && field<uint8_t>(os_globals(), 0x03)};
}

std::string describe(const ControlState &cs) {
    char buf[128];
    snprintf(buf, sizeof buf, "controls=%02X noclip=%d third=%d hidden=%d zoom=%.1f chase=%.1f", cs.controls, cs.noclip,
             cs.third, cs.hidden, cs.zoom, cs.chase);
    return buf;
}

}
