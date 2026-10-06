#include "game/control.h"
#include "game/fnv.h"
#include "game/log.h"
#include "game/rtti.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>
#include <windows.h>

namespace sm64nv {

namespace {

const uintptr_t kInputGlobals = 0x011F35CC;
// the game's own copy of the xinput state, the gamepad report follows the packet number
const uintptr_t kGamepadState = 0x011F35A8 + 4;
const uintptr_t kOSGlobals = 0x011DEA0C;
const uintptr_t kNoclip = 0x011C3C0D;
const uintptr_t kSceneGraph = 0x011DEB7C;
const uintptr_t kCameraZoom = 0x011E0B5C;
const float kZoom = 250;
const uintptr_t kChaseSetting = 0x011CD568;
const uintptr_t kVtblSetting = 0x01012114;
const float kChase = 250;
const uintptr_t kPlayerSetPos = 0x00931620;
const uintptr_t kPlayerSaveSlot = 0x0108AA90;
const uintptr_t kPlayerSave = 0x009590F0;
const uint32_t kHasKeyboard = 1 << 2;
const uint8_t kBlockedControls = 0x01 | 0x08 | 0x10 | 0x40;

using SetControls = void(__thiscall *)(void *, bool, uint8_t);
using ToggleFirstPerson = bool(__thiscall *)(void *, bool);
using SetPos = void(__thiscall *)(void *, const Vec3 *);
using SaveGame = void(__thiscall *)(void *, uint32_t);

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

// the save writes the control flags, pov and zoom so it gets the courier's
void __thiscall save_player(void *p, uint32_t changed) {
    uint8_t controls = field<uint8_t>(p, 0x680), third = field<uint8_t>(p, 0x64A);
    float zoom = *reinterpret_cast<float *>(kCameraZoom);
    if (g_taken) {
        field<uint8_t>(p, 0x680) = controls & ~(kBlockedControls & ~g_courier.controls);
        field<uint8_t>(p, 0x64A) = g_courier.third;
        *reinterpret_cast<float *>(kCameraZoom) = g_courier.zoom;
    }
    logf("save player taken=%d controls=%02X written=%02X", g_taken, controls, field<uint8_t>(p, 0x680));
    reinterpret_cast<SaveGame>(kPlayerSave)(p, changed);
    field<uint8_t>(p, 0x680) = controls, field<uint8_t>(p, 0x64A) = third;
    *reinterpret_cast<float *>(kCameraZoom) = zoom;
}

bool chase_setting_ok() {
    const char *name = field<const char *>(chase_setting(), 0x08);
    return fnv::vtbl_of(chase_setting()) == kVtblSetting && name && !strcmp(name, "fChaseCameraMax");
}

}

bool read_game_pad(Pad &pad, bool &toggle, bool &activate) {
    auto *g = *reinterpret_cast<uint8_t **>(kInputGlobals);
    if (!g || !(field<uint32_t>(g, 0x04) & kHasKeyboard)) return false;
    const GamepadState &gamepad = *reinterpret_cast<const GamepadState *>(kGamepadState);
    pad = merge_pads(read_pad(g + 0x18F8, g + 0x1B30), read_gamepad(gamepad));
    toggle = toggle_held(g + 0x18F8, gamepad);
    activate = activate_held(g + 0x18F8, gamepad);
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

bool take_player(const ControlState &courier, std::string &why) {
    fnv::TESObjectREFR *p = fnv::player();
    if (fnv::vtbl_of(p) != fnv::kVtblPlayerCharacter) why = "player vtbl";
    else if (vslot(p, 0x2A8) != kPlayerSetPos) why = "player setpos slot";
    else if (fnv::vtbl_of(body()) != fnv::kVtblBSFadeNode) why = "body vtbl=" + hex(fnv::vtbl_of(body()));
    else if (!chase_setting_ok()) why = "chase setting vtbl=" + hex(fnv::vtbl_of(chase_setting()));
    if (!why.empty()) return false;
    reinterpret_cast<SetControls>(0x0095F530)(p, true, kBlockedControls);
    reinterpret_cast<ToggleFirstPerson>(0x00950110)(p, false);
    // the switch leaves the camera at its 60 unit minimum which puts mario's cap in the lens
    *reinterpret_cast<float *>(kCameraZoom) = kZoom;
    // and the chase camera stops at 120 units which crops him
    field<float>(chase_setting(), 0x04) = kChase;
    hide_body();
    g_courier = courier, g_taken = true;
    ControlState cs = control_state();
    if ((cs.controls & kBlockedControls) == kBlockedControls && cs.hidden) return true;
    why = "readback controls=" + hex(cs.controls) + " hidden=" + std::to_string(cs.hidden);
    return false;
}

void release_player(const ControlState &saved) {
    void *p = fnv::player();
    g_taken = false;
    reinterpret_cast<SetControls>(0x0095F530)(p, false, kBlockedControls & ~saved.controls);
    reinterpret_cast<ToggleFirstPerson>(0x00950110)(p, !saved.third);
    *reinterpret_cast<float *>(kCameraZoom) = saved.zoom;
    field<float>(chase_setting(), 0x04) = saved.chase;
    // going back to first person culls the body again by itself
    if (body()) field<uint32_t>(body(), 0x30) &= ~1u;
    show_body();
}

void *body_parent() { return body() ? field<void *>(body(), 0x18) : nullptr; }

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
    return {field<uint8_t>(p, 0x680), *reinterpret_cast<uint8_t *>(kNoclip) != 0, field<uint8_t>(p, 0x64A) != 0,
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
