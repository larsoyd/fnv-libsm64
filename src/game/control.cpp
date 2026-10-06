#include "game/control.h"
#include "game/fnv.h"
#include "game/rtti.h"

#include <cstdio>
#include <windows.h>

namespace sm64nv {

namespace {

const uintptr_t kInputGlobals = 0x011F35CC;
const uintptr_t kOSGlobals = 0x011DEA0C;
const uintptr_t kNoclip = 0x011C3C0D;
const uintptr_t kSceneGraph = 0x011DEB7C;
const uintptr_t kCameraZoom = 0x011E0B5C;
const float kZoom = 250;
const uintptr_t kPlayerSetPos = 0x00931620;
const uint32_t kHasKeyboard = 1 << 2;
const uint8_t kBlockedControls = 0x01 | 0x08 | 0x10 | 0x40;

using SetControls = void(__thiscall *)(void *, bool, uint8_t);
using ToggleFirstPerson = bool(__thiscall *)(void *, bool);
using SetPos = void(__thiscall *)(void *, const Vec3 *);

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

}

bool read_game_pad(Pad &pad) {
    auto *g = *reinterpret_cast<uint8_t **>(kInputGlobals);
    if (!g || !(field<uint32_t>(g, 0x04) & kHasKeyboard)) return false;
    pad = read_pad(g + 0x18F8, g + 0x1B30);
    return true;
}

bool take_player(std::string &why) {
    fnv::TESObjectREFR *p = fnv::player();
    if (fnv::vtbl_of(p) != fnv::kVtblPlayerCharacter) why = "player vtbl";
    else if (vslot(p, 0x2A8) != kPlayerSetPos) why = "player setpos slot";
    else if (fnv::vtbl_of(body()) != fnv::kVtblBSFadeNode) why = "body vtbl=" + hex(fnv::vtbl_of(body()));
    if (!why.empty()) return false;
    reinterpret_cast<SetControls>(0x0095F530)(p, true, kBlockedControls);
    reinterpret_cast<ToggleFirstPerson>(0x00950110)(p, false);
    // the switch leaves the camera at its 60 unit minimum which puts mario's cap in the lens
    *reinterpret_cast<float *>(kCameraZoom) = kZoom;
    field<uint32_t>(body(), 0x30) |= 1;
    ControlState cs = control_state();
    if ((cs.controls & kBlockedControls) == kBlockedControls && cs.hidden) return true;
    why = "readback controls=" + hex(cs.controls) + " hidden=" + std::to_string(cs.hidden);
    return false;
}

void move_player(Vec3 pos) {
    void *p = fnv::player();
    reinterpret_cast<SetPos>(vslot(p, 0x2A8))(p, &pos);
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
            body() && (field<uint32_t>(body(), 0x30) & 1), *reinterpret_cast<float *>(kCameraZoom),
            has_focus(), os_globals() && field<uint8_t>(os_globals(), 0x03)};
}

}
