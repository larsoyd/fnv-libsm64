#include "core/audio.h"
#include "core/config.h"
#include "core/dds.h"
#include "core/frame.h"
#include "core/geo.h"
#include "core/mesh.h"
#include "core/rom.h"
#include "core/stall.h"
#include "core/step.h"
#include "core/surfaces.h"
#include "core/window.h"
#include "game/collision.h"
#include "game/control.h"
#include "game/fnv.h"
#include "game/log.h"
#include "game/nvse.h"
#include "game/render.h"
#include "game/sound.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <windows.h>

using namespace sm64nv;

namespace {

const char *kVersion = "0.1.0";
const uint32_t kMenuFrames = 60;
const uint32_t kSettleFrames = 120;
const uint32_t kScenarioTimeout = 3000;
const float kCollisionRadius = 3000;
// game units, the window follows mario and faces turn toward him out to reach
const float kWindowRadius = 2000;
const float kFaceReach = 200;
// sm64 units around mario's middle for the faces a stall line lists
const float kStallRange = 80;
const size_t kStallFaces = 8;
const float kMaxScaleErr = 0.5f;
const int kLandTicks = 90;
const int kRunTicks = 60;
const float kRenderSpawnAhead = 150;
const int kRenderFaceTick = 60;
const int kRenderShotTicks[2] = {70, 110};
const int kRenderRunTick = 80;
const int kRenderTicks = 120;
const Vec3 kLight{0.4f, -0.6f, 0.7f};
const char *kTextureDirs[] = {"Data\\Textures", "Data\\Textures\\sm64nv"};
// the loader looks under data for this
const char *kTexturePath = "textures\\sm64nv\\mario.dds";
const int kJumpFrom = 160, kJumpTo = 200;
const int kControlShotTick = 262;
const int kControlTicks = 280;
// the game finishes the switch back to first person a few frames after the call
const int kRestoreTicks = 10;

struct ScriptLine {
    int tick;
    const char *line;
};
// dinput codes esc 1, tab 15, W 17, S 31, D 32, F 33, ctrl 29, grave 41
// shift 42, M 50, space 57 and 256 is the left mouse button
const ScriptLine kControlScript[] = {
    {40, "HoldKey 17"}, {55, "ReleaseKey 17"},
    {80, "player.SetAngle Z 90"}, {85, "HoldKey 17"}, {100, "ReleaseKey 17"},
    {125, "HoldKey 32"}, {137, "ReleaseKey 32"},
    {160, "HoldKey 57"}, {163, "ReleaseKey 57"},
    {205, "HoldKey 42"}, {208, "ReleaseKey 42"},
    {215, "HoldKey 256"}, {218, "ReleaseKey 256"},
    {230, "HoldKey 29"}, {245, "ReleaseKey 29"},
};

struct Move {
    const char *name;
    int from, to;
};
const Move kControlMoves[] = {{"west", 40, 55}, {"east", 85, 100}, {"strafe", 125, 137}};

const ScriptLine kMenugateScript[] = {
    {40, "HoldKey 1"}, {43, "ReleaseKey 1"}, {50, "HoldKey 17"}, {65, "ReleaseKey 17"},
    {75, "HoldKey 1"}, {78, "ReleaseKey 1"}, {90, "HoldKey 17"}, {105, "ReleaseKey 17"},
    {115, "HoldKey 41"}, {118, "ReleaseKey 41"}, {120, "HoldKey 57"}, {123, "ReleaseKey 57"},
    {125, "HoldKey 17"}, {140, "ReleaseKey 17"}, {141, "HoldKey 32"}, {146, "ReleaseKey 32"},
    {150, "HoldKey 41"}, {153, "ReleaseKey 41"}, {160, "player.SetAngle Z 90"}, {165, "HoldKey 17"}, {180, "ReleaseKey 17"},
};
const Move kMenugateMoves[] = {{"pause", 50, 65}, {"resumed", 90, 105}, {"console", 125, 148}, {"closed", 165, 180}};

struct ControlScript {
    std::span<const ScriptLine> lines;
    std::span<const Move> moves;
    std::span<const int> shots;
    int end, jump_from = -1, jump_to = -1;
};
const int kControlShots[] = {kControlShotTick};
const ControlScript kControl{kControlScript, kControlMoves, kControlShots, kControlTicks, kJumpFrom, kJumpTo};
const ControlScript kMenugate{kMenugateScript, kMenugateMoves, {}, 190};

const ScriptLine kReleaseScript[] = {
    {40, "HoldKey 17"}, {55, "ReleaseKey 17"},
    {60, "HoldKey 41"}, {63, "ReleaseKey 41"}, {66, "HoldKey 50"}, {69, "ReleaseKey 50"}, {72, "HoldKey 41"}, {75, "ReleaseKey 41"},
    {85, "HoldKey 50"}, {88, "ReleaseKey 50"}, {100, "HoldKey 17"}, {120, "ReleaseKey 17"},
    {125, "HoldKey 33"}, {128, "ReleaseKey 33"}, {140, "HoldKey 50"}, {143, "ReleaseKey 50"},
    {150, "player.SetAngle Z 90"}, {155, "HoldKey 17"}, {175, "ReleaseKey 17"}, {180, "HoldKey 50"}, {183, "ReleaseKey 50"},
    {200, "HoldKey 50"}, {202, "ReleaseKey 50"}, {206, "HoldKey 50"}, {208, "ReleaseKey 50"},
    {210, "HoldKey 50"}, {212, "ReleaseKey 50"},
};
const Move kReleaseMoves[] = {{"mario", 40, 55}, {"courier", 100, 120}, {"again", 155, 175}};
const ScriptLine kLeaveScript[] = {
    {40, "HoldKey 17"}, {55, "ReleaseKey 17"}, {60, "coc GSProspectorSaloonInterior"}, {190, "pcb"},
    {200, "HoldKey 17"}, {210, "ReleaseKey 17"}, {225, "HoldKey 50"}, {228, "ReleaseKey 50"},
    {245, "player.SetAngle Z 180"}, {255, "HoldKey 17"}, {270, "ReleaseKey 17"},
};
const Move kLeaveMoves[] = {{"mario", 40, 55}, {"courier", 200, 210}, {"again", 255, 270}};
const int kLeaveShots[] = {215, 272};
const ControlScript kLeave{kLeaveScript, kLeaveMoves, kLeaveShots, 285};

#define PAD_IDLE "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0000"
#define PAD_DPAD_DOWN "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"
const ScriptLine kGamepadScript[] = {
    {40, "pad lx=0 ly=32767 rx=0 ry=0 lt=0 rt=0 buttons=0000"}, {55, PAD_IDLE},
    {60, "pad lx=0 ly=0 rx=32767 ry=0 lt=0 rt=0 buttons=0000"}, {70, PAD_IDLE},
    {80, "player.SetAngle Z 90"}, {85, "pad lx=17000 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0000"}, {100, PAD_IDLE},
    {120, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {123, PAD_IDLE},
    {150, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=4000"}, {153, PAD_IDLE},
    {170, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=255 buttons=0000"}, {185, PAD_IDLE},
    {195, PAD_DPAD_DOWN}, {198, PAD_IDLE}, {210, PAD_DPAD_DOWN}, {213, PAD_IDLE},
};
const Move kGamepadMoves[] = {{"stick", 40, 55}, {"look", 58, 74}, {"half", 85, 100}};
const ControlScript kGamepad{kGamepadScript, kGamepadMoves, {}, 230};

// a backflip into the wall south of the start, then the gap behind the leaning shelf
const ScriptLine kWallsScript[] = {
    {40, "player.SetAngle Z 0"}, {45, "mario 1925 1856 7360 0"},
    {55, "HoldKey 29"}, {63, "HoldKey 57"}, {66, "ReleaseKey 57"}, {75, "ReleaseKey 29"}, {110, "spot backflip"},
    {112, "HoldKey 31"}, {180, "ReleaseKey 31"},
    {185, "player.SetAngle Z 270"}, {190, "mario 2144.9 1830.9 7360 270"}, {195, "spot gap"},
    {200, "HoldKey 17"}, {290, "ReleaseKey 17"}, {295, "spot gap_out"},
};
const ControlScript kWalls{kWallsScript, {}, {}, 305};

// tab opens the pip-boy and closes it again
const ScriptLine kPipboyScript[] = {
    {40, "state"}, {45, "HoldKey 15"}, {48, "ReleaseKey 15"}, {110, "state"},
    {130, "HoldKey 15"}, {133, "ReleaseKey 15"}, {200, "state"},
};
const ControlScript kPipboy{kPipboyScript, {}, {}, 215};

const int kReleaseShots[] = {177};
const ControlScript kPlay{{}, {}, {}, INT32_MAX};
const int kStatusTicks = 900;

const ControlScript kRelease{kReleaseScript, kReleaseMoves, kReleaseShots, 230};

nvse::PluginHandle g_handle;
const nvse::ConsoleInterface *g_console;
std::string g_dir;
Config g_config;
std::vector<uint8_t> g_rom;
std::vector<uint8_t> g_texture(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);
uint32_t g_frames, g_settled;
bool g_ready, g_done;

MarioTicks g_ticks;
Geo g_drawn;

struct Sim {
    Frame frame{};
    int32_t id = -1;
    int ticks = 0;
    SM64MarioState state{};
    Vec3 run_start{}, run_mid{};
    float floor_z = 0, min_z = 0;
};
Sim g_sim;
SurfaceWindow g_window{{}, 0, 0};
std::vector<uint32_t> g_owners;
uint32_t g_window_loads;
StallWatch g_stall;
FixedStep g_step;
MeshOut g_mesh, g_decal;

struct Smooth {
    int frames = 0, hitches = 0;
    float max_step = 0;
};

struct Control {
    Pad pad{};
    Smooth smooth;
    Vec3 last{}, move_from{};
    float jump_floor = 0, jump_peak = 0, max_gap = 0;
    int frames = 0, tick = 0, restore_check = 0;
    uint32_t action = 0;
    bool placed = false, blocked = false;
    Press toggle;
    ControlState saved{};
    fnv::TESObjectCELL *cell = nullptr;
    const ControlScript *script = nullptr;
};
Control g_ctl;

double g_frame_dt;

double frame_seconds() {
    static LARGE_INTEGER freq, last;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq), last = now;
    g_frame_dt = double(now.QuadPart - last.QuadPart) / freq.QuadPart;
    last = now;
    return g_frame_dt;
}

std::string read_text(const std::string &path) {
    std::vector<uint8_t> b = read_file(path);
    return {b.begin(), b.end()};
}

// libsm64's audio code prints on every note
MessageKinds g_lib_messages(64);

void libsm64_print(const char *msg) {
    if (g_lib_messages.first(msg)) logf("libsm64 %s", msg);
}

void on_post_load() {
    g_config = parse_config(read_text(g_dir + "Data\\NVSE\\Plugins\\sm64nv.ini"));
    if (!g_config.errors.empty()) {
        logf("refused: config %s", g_config.errors[0].c_str());
        return;
    }
    g_rom = read_file(g_config.rom);
    RomCheck rc = check_rom(g_rom);
    if (!rc.ok) {
        logf("refused: rom %s path=%s", rc.reason.c_str(), g_config.rom.c_str());
        return;
    }
    logf("rom ok sha256=%s", rc.sha256.c_str());
    sm64_register_debug_print_function(libsm64_print);
    sm64_global_init(g_rom.data(), g_texture.data());
    sm64_audio_init(g_rom.data());
    for (const char *dir : kTextureDirs) CreateDirectoryA((g_dir + dir).c_str(), nullptr);
    std::string path = g_dir + kTextureDirs[1] + "\\mario.dds";
    std::vector<uint8_t> dds = atlas_dds(g_texture.data());
    if (!write_file(path, dds)) return logf("refused: texture write path=%s", path.c_str());
    logf("texture written path=%s bytes=%u", path.c_str(), (unsigned)dds.size());
    g_ready = true;
}

void finish(bool ok, const char *reason) {
    if (ok) logf("scenario done name=%s frames=%u", g_config.scenario.c_str(), g_frames);
    else logf("refused: scenario=%s reason=%s frame=%u", g_config.scenario.c_str(), reason, g_frames);
    g_done = true;
}

void run_console(const std::string &line) {
    unsigned ret = g_console ? g_console->runScriptLine(line.c_str(), nullptr) : 0;
    logf("console line=%s ok=%d ret=%u", line.c_str(), ret != 0, ret);
}

fnv::TESObjectCELL *loaded_cell() {
    fnv::TESObjectREFR *p = fnv::player();
    if (fnv::vtbl_of(p) != fnv::kVtblPlayerCharacter) return nullptr;
    fnv::TESObjectCELL *c = p->parentCell;
    bool has_3d = p->renderState && p->renderState->niNode;
    return has_3d && fnv::vtbl_of(c) == fnv::kVtblTESObjectCELL ? c : nullptr;
}

int count_refs(const fnv::TESObjectCELL *c) {
    int n = 0;
    for (const fnv::ListNode<fnv::TESObjectREFR> *it = &c->objectList; it; it = it->next) n += it->data != nullptr;
    return n;
}

fnv::TESObjectCELL *settle_cell() {
    if (g_frames == kMenuFrames) run_console("coc " + g_config.cell);
    if (g_frames <= kMenuFrames) return nullptr;
    fnv::TESObjectCELL *c = loaded_cell();
    g_settled = c ? g_settled + 1 : 0;
    return g_settled >= kSettleFrames ? c : nullptr;
}

void tick_cell_scenario() {
    fnv::TESObjectCELL *c = settle_cell();
    if (!c) return;
    fnv::TESObjectREFR *p = fnv::player();
    logf("cell id=%08X interior=%d pos=%.1f,%.1f,%.1f refs=%d", c->form.refID, c->cellFlags & 1, p->pos[0], p->pos[1],
         p->pos[2], count_refs(c));
    finish(true, "");
}

void log_collision(const CollisionStats &st, size_t tris, const SurfaceStats &ss) {
    logf("collision refs=%d bodies=%d tris=%u floors=%u walls=%u ceilings=%u degenerate=%u", st.refs, st.bodies, (unsigned)tris,
         ss.floors, ss.walls, ss.ceilings, ss.degenerate);
    logf("havok scale samples=%d max_err=%.3f node=%.3f,%.3f,%.3f body=%.3f,%.3f,%.3f", st.scale_samples,
         st.scale_max_err, st.scale_worst_node.x, st.scale_worst_node.y, st.scale_worst_node.z, st.scale_worst_body.x,
         st.scale_worst_body.y, st.scale_worst_body.z);
    for (const auto &[name, n] : st.skipped_types) logf("collision skipped type=%s count=%d", name.c_str(), n);
    for (const auto &[layer, n] : st.skipped_layers) logf("collision skipped layer=%d count=%d", layer, n);
}

void sync_window(Vec3 feet) {
    if (!g_window.update(feet)) return;
    sm64_static_surfaces_load(g_window.loaded().data(), (uint32_t)g_window.loaded().size());
    g_window_loads++;
}

void log_window(const char *when) {
    logf("collision window when=%s loaded=%u gathers=%u flips=%u loads=%u", when, (unsigned)g_window.loaded().size(),
         g_window.stats.gathers, g_window.stats.flips, g_window_loads);
}

bool start_mario(fnv::TESObjectCELL *cell, float ahead) {
    fnv::TESObjectREFR *p = fnv::player();
    float h = p->rot[2];
    Vec3 at{p->pos[0] + std::sin(h) * ahead, p->pos[1] + std::cos(h) * ahead, p->pos[2]};
    CollisionStats st;
    std::vector<Tri> tris = gather_collision(cell, at, kCollisionRadius, st);
    write_obj((g_dir + "sm64nv_collision.obj").c_str(), tris);
    g_sim.frame = {at, g_config.scale};
    SurfaceStats ss;
    std::vector<SM64Surface> surfaces = build_surfaces(g_sim.frame, tris, ss, &g_owners);
    log_collision(st, tris.size(), ss);
    if (!st.scale_samples || st.scale_max_err > kMaxScaleErr) {
        logf("refused: havok scale samples=%d max_err=%.3f limit=%.1f", st.scale_samples, st.scale_max_err, kMaxScaleErr);
        return false;
    }
    Vec3 s = to_sm64(g_sim.frame, {at.x, at.y, at.z + 60});
    g_window = SurfaceWindow(std::move(surfaces), kWindowRadius * g_config.scale, kFaceReach * g_config.scale);
    g_window_loads = 0, g_stall = {};
    sync_window(s);
    log_window("spawn");
    g_sim.id = sm64_mario_create(s.x, s.y, s.z);
    g_ticks.reset(s);
    logf("mario create id=%d at=%.1f,%.1f,%.1f player_heading=%.3f", g_sim.id, at.x, at.y, at.z + 60, h);
    // face back toward the player, game heading h plus a half turn
    if (g_sim.id >= 0 && ahead > 0) sm64_set_mario_faceangle(g_sim.id, sm64_yaw_from_heading(h + 3.14159265f));
    return g_sim.id >= 0;
}

Vec3 mario_pos() { return to_game(g_sim.frame, {g_sim.state.position[0], g_sim.state.position[1], g_sim.state.position[2]}); }

void tick_mario(const SM64MarioInputs &in) {
    sync_window(g_ticks.cur_pos);
    g_ticks.tick(g_sim.id, in, g_sim.state);
    g_sim.ticks++;
}

void collide_tick() {
    bool running = g_sim.ticks >= kLandTicks;
    tick_mario(make_inputs(0, 0, running ? 1.0f : 0.0f, {}));
    Vec3 m = mario_pos();
    if (g_sim.ticks % 30 == 0)
        logf("mario tick=%d pos=%.1f,%.1f,%.1f action=%08X", g_sim.ticks, m.x, m.y, m.z, g_sim.state.action);
    if (g_sim.ticks == kLandTicks) {
        g_sim.run_start = m;
        g_sim.floor_z = g_sim.min_z = m.z;
        logf("mario landed z=%.1f player_z=%.1f action=%08X", m.z, fnv::player()->pos[2], g_sim.state.action);
    } else if (running) {
        g_sim.min_z = std::fmin(g_sim.min_z, m.z);
        if (g_sim.ticks == kLandTicks + kRunTicks / 2) g_sim.run_mid = m;
        if (g_sim.ticks < kLandTicks + kRunTicks) return;
        float dist = std::hypot(m.x - g_sim.run_start.x, m.y - g_sim.run_start.y);
        logf("mario ran ticks=%d dist=%.1f min_z=%.1f floor_z=%.1f end=%.1f,%.1f,%.1f action=%08X", kRunTicks, dist,
             g_sim.min_z, g_sim.floor_z, m.x, m.y, m.z, g_sim.state.action);
        logf("mario blocked moved_last=%.1f action=%08X", std::hypot(m.x - g_sim.run_mid.x, m.y - g_sim.run_mid.y),
             g_sim.state.action);
        finish(true, "");
    }
}

void render_tick() {
    float h = fnv::player()->rot[2];
    bool running = g_sim.ticks >= kRenderRunTick;
    tick_mario(make_inputs(h, running ? 1.0f : 0.0f, 0, {}));
    // game heading h plus a half turn puts mario's face toward the camera
    if (g_sim.ticks == kRenderFaceTick) sm64_set_mario_faceangle(g_sim.id, sm64_yaw_from_heading(h + 3.14159265f));
    for (int shot : kRenderShotTicks) {
        if (g_sim.ticks != shot) continue;
        Vec3 m = mario_pos();
        logf("mario render tris=%u decal_tris=%u pos=%.1f,%.1f,%.1f action=%08X", g_ticks.cur.tris, g_decal.tris, m.x, m.y, m.z,
             g_sim.state.action);
        take_screenshot();
        logf("screenshot requested tick=%d menu=%d", g_sim.ticks, menu_mode());
    }
    if (g_sim.ticks == kRenderTicks) finish(true, "");
}

void run_ticks(void (*tick)()) {
    for (int n = g_step.advance(frame_seconds()); n > 0 && !g_done; n--) tick();
}

void tick_collide_scenario() {
    if (g_sim.id >= 0) return run_ticks(collide_tick);
    fnv::TESObjectCELL *c = settle_cell();
    if (c && !start_mario(c, 0)) finish(false, "mario_create");
}

// frames land between 30 Hz ticks so draw the blend of the last two
Vec3 draw_mario() {
    float alpha = (float)g_step.alpha();
    g_ticks.draw(alpha, g_drawn);
    Vec3 m = to_game(g_sim.frame, g_ticks.pos(alpha));
    convert_mesh(g_sim.frame, g_drawn.view(), m, kLight, g_mesh);
    convert_decal(g_sim.frame, g_drawn.view(), m, kLight, g_decal);
    mario_mesh_update(g_mesh, g_decal, m);
    return m;
}

void spawn_drawn_mario(fnv::TESObjectCELL *c, float ahead) {
    std::string why;
    void *parent = *reinterpret_cast<void **>(static_cast<uint8_t *>(fnv::player()->renderState->niNode) + 0x18);
    if (!start_mario(c, ahead)) finish(false, "mario_create");
    else if (!mario_mesh_create(parent, kTexturePath, why)) finish(false, ("mesh " + why).c_str());
    frame_seconds();
}

void tick_render_scenario() {
    if (g_sim.id < 0) {
        if (fnv::TESObjectCELL *c = settle_cell()) spawn_drawn_mario(c, kRenderSpawnAhead);
        return;
    }
    run_ticks(render_tick);
    draw_mario();
}

std::string xyz(Vec3 v) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.1f,%.1f,%.1f", v.x, v.y, v.z);
    return buf;
}

bool log_camera(Vec3 m, float heading) {
    Vec3 c;
    if (!camera_pos(c)) return false;
    Vec3 d{c.x - m.x, c.y - m.y, c.z - m.z};
    float back = -(d.x * std::sin(heading) + d.y * std::cos(heading));
    float side = d.x * std::cos(heading) - d.y * std::sin(heading);
    logf("control camera back=%.1f side=%.1f up=%.1f zoom=%.1f", back, side, d.z, control_state().zoom);
    return true;
}

bool taken() { return g_sim.id >= 0; }

Vec3 player_pos() { return {fnv::player()->pos[0], fnv::player()->pos[1], fnv::player()->pos[2]}; }

void take_control() {
    fnv::TESObjectCELL *c = loaded_cell();
    if (!c) return logf("control take skipped tick=%d reason=no_cell", g_ctl.tick);
    g_ctl.cell = c;
    logf("control cell tick=%d id=%08X", g_ctl.tick, c->form.refID);
    g_ctl.saved = control_state();
    logf("control saved tick=%d %s", g_ctl.tick, describe(g_ctl.saved).c_str());
    g_ctl.restore_check = 0;
    std::string why;
    if (!take_player(why)) return finish(false, ("take " + why).c_str());
    focus_game();
    if (!sound_ready() && sound_open(why)) logf("sound open rate=%d", kAudioRate);
    else if (!sound_ready()) logf("sound unavailable reason=%s", why.c_str());
    if (!control_state().noclip) run_console("tcl");
    // the camera aims at courier eye height so tilt it down onto mario
    run_console("player.SetAngle X 20");
    ControlState cs = control_state();
    logf("control take controls=%02X noclip=%d hidden=%d", cs.controls, cs.noclip, cs.hidden);
    if (!cs.noclip) return finish(false, "noclip");
    spawn_drawn_mario(g_ctl.cell, 0);
}

void release_control(const char *reason) {
    sm64_mario_delete(g_sim.id);
    g_sim.id = -1;
    g_ctl.placed = false;
    mario_mesh_hide();
    release_player(g_ctl.saved);
    if (control_state().noclip != g_ctl.saved.noclip) run_console("tcl");
    logf("control release tick=%d reason=%s", g_ctl.tick, reason);
    g_ctl.restore_check = g_ctl.tick + kRestoreTicks;
}

void check_restored() {
    std::string now = describe(control_state());
    logf("control restored tick=%d %s shape_hidden=%d", g_ctl.tick, now.c_str(), mario_mesh_hidden());
    if (now != describe(g_ctl.saved)) finish(false, ("restore readback want " + describe(g_ctl.saved)).c_str());
}

const char *surface_kind(Vec3 n) { return n.y > 0.01f ? "floor" : n.y < -0.01f ? "ceiling" : "wall"; }

// what mario stands on and pushes against when the stick moves him nowhere
void log_stall(int t) {
    Vec3 feet = g_ticks.cur_pos;
    float floor = sm64_surface_find_floor_height(feet.x, feet.y, feet.z);
    SM64SurfaceCollisionData *hit = nullptr;
    float ceil = sm64_surface_find_ceil(feet.x, floor + 80, feet.z, &hit);
    std::vector<size_t> faces = g_window.nearby({feet.x, feet.y + 80, feet.z}, kStallRange);
    logf("stall tick=%d pos=%s action=%08X floor=%.1f ceil=%.1f faces=%u", t, xyz(mario_pos()).c_str(), g_sim.state.action,
         g_sim.frame.origin.z + floor / g_sim.frame.scale, g_sim.frame.origin.z + ceil / g_sim.frame.scale, (unsigned)faces.size());
    for (size_t k = 0; k < faces.size() && k < kStallFaces; k++) {
        const SM64Surface &s = g_window.loaded()[faces[k]];
        Vec3 n = dir_to_game(surface_normal(s)), c{0, 0, 0};
        for (const auto &v : s.vertices) c = {c.x + v[0] / 3.0f, c.y + v[1] / 3.0f, c.z + v[2] / 3.0f};
        logf("stall face kind=%s owner=%08X normal=%.2f,%.2f,%.2f at=%s", surface_kind(surface_normal(s)),
             g_owners[g_window.source(faces[k])], n.x, n.y, n.z, xyz(to_game(g_sim.frame, c)).c_str());
    }
}

void teleport_mario(const char *args) {
    Vec3 g;
    float deg;
    if (sscanf(args, "%f %f %f %f", &g.x, &g.y, &g.z, &deg) != 4) return finish(false, "teleport args");
    Vec3 s = to_sm64(g_sim.frame, g);
    sm64_set_mario_position(g_sim.id, s.x, s.y, s.z);
    sm64_set_mario_faceangle(g_sim.id, sm64_yaw_from_heading(deg * 3.14159265f / 180));
    sm64_set_mario_velocity(g_sim.id, 0, 0, 0);
    sm64_set_mario_forward_velocity(g_sim.id, 0);
    g_ticks.reset(s);
    logf("control teleport tick=%d to=%s heading=%.1f", g_ctl.tick, xyz(g).c_str(), deg);
}

// wall clock so a recording of the stream can be lined up with the windows
uint64_t unix_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000 - 11644473600000ull;
}

uint64_t g_sound_from;

void log_sound(const char *name) {
    SoundStats st = sound_take_stats();
    uint64_t now = unix_ms();
    logf("control sound name=%s peak=%d written=%u done=%u start_ms=%llu end_ms=%llu", name, st.peak, st.written, st.done,
         (unsigned long long)g_sound_from, (unsigned long long)now);
    g_sound_from = now;
}

void control_tick() {
    const ControlScript &s = *g_ctl.script;
    float cam = fnv::player()->rot[2];
    int t = ++g_ctl.tick;
    if (taken()) {
        // menus and the console still see the keys, mario must not
        Pad pad = g_ctl.blocked ? Pad{} : g_ctl.pad;
        tick_mario(make_inputs(cam, pad.right, pad.forward, pad.buttons));
        if (g_stall.feed(g_ticks.cur_pos, std::hypot(pad.right, pad.forward))) log_stall(t);
    }
    if (t == g_ctl.restore_check) check_restored();
    Vec3 m = taken() ? mario_pos() : player_pos();
    if (taken() && g_sim.state.action != g_ctl.action) {
        g_ctl.action = g_sim.state.action;
        logf("mario action tick=%d action=%08X pos=%s", t, g_ctl.action, xyz(m).c_str());
    }
    for (const Move &mv : s.moves) {
        if (t == mv.from) g_ctl.move_from = m, g_ctl.smooth = {};
        if (t != mv.to) continue;
        logf("control move name=%s cam=%.3f from=%s to=%s", mv.name, cam, xyz(g_ctl.move_from).c_str(), xyz(m).c_str());
        const Smooth &sm = g_ctl.smooth;
        logf("control smooth name=%s frames=%d hitches=%d max_step=%.2f", mv.name, sm.frames, sm.hitches, sm.max_step);
    }
    if (t == s.jump_from) g_ctl.jump_floor = g_ctl.jump_peak = m.z;
    if (t > s.jump_from && t <= s.jump_to) g_ctl.jump_peak = std::fmax(g_ctl.jump_peak, m.z);
    if (t == s.jump_to) logf("control jump rise=%.1f", g_ctl.jump_peak - g_ctl.jump_floor);
    // standing still makes no sound, the jump says wahoo
    if (t == s.jump_from - 15) sound_take_stats(), g_sound_from = unix_ms();
    if (t == s.jump_from) log_sound("quiet");
    if (t == s.jump_to) log_sound("jump");
    for (const ScriptLine &line : s.lines) {
        if (t != line.tick) continue;
        ControlState cs = control_state();
        logf("control focus tick=%d foreground=%d active=%d", t, cs.foreground, cs.active);
        if (!cs.foreground) return finish(false, "no_focus");
        // pad lines are for the virtual gamepad that follows this log
        if (!strncmp(line.line, "pad ", 4)) logf("control pad tick=%d %s", t, line.line + 4);
        else if (!strncmp(line.line, "mario ", 6)) teleport_mario(line.line + 6);
        else if (!strcmp(line.line, "state")) {
            logf("control state tick=%d %s", t, describe(control_state()).c_str());
            if (!log_camera(m, cam)) return finish(false, "camera");
        }
        else if (!strncmp(line.line, "spot ", 5))
            logf("control spot name=%s pos=%s action=%08X", line.line + 5, xyz(m).c_str(), g_sim.state.action);
        else run_console(line.line);
    }
    for (int shot : s.shots) {
        if (t != shot) continue;
        take_screenshot();
        logf("screenshot requested tick=%d menu=%d", t, menu_mode());
    }
    if (t % kStatusTicks == 0)
        logf("control status tick=%d frames=%u taken=%d blocked=%d pos=%s action=%08X lib_msgs=%u", t, g_frames, taken(),
             g_ctl.blocked, xyz(m).c_str(), g_sim.state.action, (unsigned)g_lib_messages.total());
    if (t % kStatusTicks == 0 && taken()) log_window("status");
    if (t < s.end) return;
    if (!log_camera(m, cam)) return finish(false, "camera");
    ControlState cs = control_state();
    logf("control follow frames=%d max_gap=%.2f", g_ctl.frames, g_ctl.max_gap);
    if (taken()) log_window("end");
    logf("control end third=%d hidden=%d player=%s mario=%s", cs.third, cs.hidden, xyz(player_pos()).c_str(), xyz(m).c_str());
    finish(true, "");
}

bool in_move(int t) {
    for (const Move &mv : g_ctl.script->moves)
        if (t >= mv.from && t < mv.to) return true;
    return false;
}

// a frame whose duration should carry visible motion but shows none is a hitch
void track_smooth(float step) {
    Smooth &sm = g_ctl.smooth;
    Vec3 a = g_ticks.prev_pos, b = g_ticks.cur_pos;
    float expected = std::hypot(b.x - a.x, b.y - a.y, b.z - a.z) / g_sim.frame.scale * g_frame_dt / FixedStep::kTick;
    if (expected < 0.05f) return;
    sm.frames++, sm.hitches += step < 0.01f;
    sm.max_step = std::fmax(sm.max_step, step);
}

void control_frame() {
    if (g_ctl.placed) {
        Vec3 p = player_pos();
        g_ctl.max_gap = std::fmax(g_ctl.max_gap, std::hypot(p.x - g_ctl.last.x, p.y - g_ctl.last.y, p.z - g_ctl.last.z));
        g_ctl.frames++;
    }
    Pad pad;
    bool toggle;
    if (!read_game_pad(pad, toggle)) return finish(false, "input_globals");
    if (pad != g_ctl.pad)
        logf("control input tick=%d forward=%.2f right=%.2f a=%d b=%d z=%d", g_ctl.tick, pad.forward, pad.right,
             pad.buttons.a, pad.buttons.b, pad.buttons.z);
    g_ctl.pad = pad;
    bool blocked = menu_mode();
    if (blocked != g_ctl.blocked) logf("control gate tick=%d blocked=%d menu=%d", g_ctl.tick, blocked, menu_mode());
    g_ctl.blocked = blocked;
    if (g_ctl.toggle.edge(toggle) && !blocked) taken() ? release_control("key") : take_control();
    if (g_done) return;
    run_ticks(control_tick);
    if (taken() && !blocked && sound_ready()) sound_pump();
    if (taken() && loaded_cell() != g_ctl.cell) release_control("cell");
    if (!taken() || g_done) return;
    if (hide_body()) logf("control rehide tick=%d", g_ctl.tick);
    Vec3 drawn = draw_mario();
    if (in_move(g_ctl.tick)) track_smooth(std::hypot(drawn.x - g_ctl.last.x, drawn.y - g_ctl.last.y, drawn.z - g_ctl.last.z));
    g_ctl.last = drawn;
    move_player(drawn);
    g_ctl.placed = true;
}

void tick_control_scenario(const ControlScript &script) {
    g_ctl.script = &script;
    if (g_ctl.cell) return control_frame();
    g_ctl.cell = settle_cell();
    if (g_ctl.cell) take_control();
}

void on_frame() {
    if (!g_ready || g_done) return;
    g_frames++;
    if (g_config.scenario == "boot" && g_frames == (uint32_t)g_config.frames) finish(true, "");
    else if (g_config.scenario == "cell") tick_cell_scenario();
    else if (g_config.scenario == "collide") tick_collide_scenario();
    else if (g_config.scenario == "render") tick_render_scenario();
    else if (g_config.scenario == "control") tick_control_scenario(kControl);
    else if (g_config.scenario == "menugate") tick_control_scenario(kMenugate);
    else if (g_config.scenario == "release") tick_control_scenario(kRelease);
    else if (g_config.scenario == "leave") tick_control_scenario(kLeave);
    else if (g_config.scenario == "play") tick_control_scenario(kPlay);
    else if (g_config.scenario == "gamepad") tick_control_scenario(kGamepad);
    else if (g_config.scenario == "walls") tick_control_scenario(kWalls);
    else if (g_config.scenario == "pipboy") tick_control_scenario(kPipboy);
    // play runs until the game closes once it has the player
    bool endless = g_config.scenario == "play" && g_ctl.cell;
    if (!g_done && !g_config.scenario.empty() && !endless && g_frames >= kScenarioTimeout) finish(false, "timeout");
}

void on_message(nvse::Message *m) {
    if (m->type == nvse::kMessagePostLoad) on_post_load();
    else if (m->type == nvse::kMessageMainGameLoop) on_frame();
}

}

extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const nvse::Interface *nvse, nvse::PluginInfo *info) {
    info->infoVersion = 1;
    info->name = "sm64nv";
    info->version = 1;
    if (nvse->isEditor) return false;
    g_dir = nvse->getRuntimeDirectory();
    log_open((g_dir + "sm64nv.log").c_str());
    if (nvse->runtimeVersion != nvse::kRuntime1_4_0_525) {
        logf("refused: runtime=%08X want=%08X", nvse->runtimeVersion, nvse::kRuntime1_4_0_525);
        return false;
    }
    return true;
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const nvse::Interface *nvse) {
    logf("loaded version=%s nvse=%08X", kVersion, nvse->nvseVersion);
    g_handle = nvse->getPluginHandle();
    g_console = static_cast<const nvse::ConsoleInterface *>(nvse->queryInterface(nvse::kInterfaceConsole));
    auto *msg = static_cast<const nvse::MessagingInterface *>(nvse->queryInterface(nvse::kInterfaceMessaging));
    if (!msg || !msg->registerListener(g_handle, "NVSE", on_message)) {
        logf("refused: messaging interface unavailable");
        return false;
    }
    return true;
}
