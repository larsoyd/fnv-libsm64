#include "core/config.h"
#include "core/frame.h"
#include "core/rom.h"
#include "core/surfaces.h"
#include "game/collision.h"
#include "game/fnv.h"
#include "game/log.h"
#include "game/nvse.h"

#include <cmath>
#include <string>

using namespace sm64nv;

namespace {

const char *kVersion = "0.1.0";
const uint32_t kMenuFrames = 60;
const uint32_t kSettleFrames = 120;
const uint32_t kScenarioTimeout = 3000;
const float kCollisionRadius = 3000;
const int kLandTicks = 90;
const int kRunTicks = 60;

nvse::PluginHandle g_handle;
const nvse::ConsoleInterface *g_console;
std::string g_dir;
Config g_config;
std::vector<uint8_t> g_rom;
std::vector<uint8_t> g_texture(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);
uint32_t g_frames, g_settled;
bool g_ready, g_done;

float g_geo_pos[9 * SM64_GEO_MAX_TRIANGLES], g_geo_normal[9 * SM64_GEO_MAX_TRIANGLES];
float g_geo_color[9 * SM64_GEO_MAX_TRIANGLES], g_geo_uv[6 * SM64_GEO_MAX_TRIANGLES];
SM64MarioGeometryBuffers g_geo{g_geo_pos, g_geo_normal, g_geo_color, g_geo_uv, 0};

struct Sim {
    Frame frame{};
    int32_t id = -1;
    int ticks = 0;
    SM64MarioState state{};
    Vec3 run_start{}, run_mid{};
    float floor_z = 0, min_z = 0;
};
Sim g_sim;

std::string read_text(const std::string &path) {
    std::vector<uint8_t> b = read_file(path);
    return {b.begin(), b.end()};
}

void libsm64_print(const char *msg) { logf("libsm64 %s", msg); }

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

bool start_mario(fnv::TESObjectCELL *cell) {
    fnv::TESObjectREFR *p = fnv::player();
    Vec3 at{p->pos[0], p->pos[1], p->pos[2]};
    CollisionStats st;
    std::vector<Tri> tris = gather_collision(cell, at, kCollisionRadius, st);
    write_obj((g_dir + "sm64nv_collision.obj").c_str(), tris);
    g_sim.frame = {at, g_config.scale};
    SurfaceStats ss;
    std::vector<SM64Surface> surfaces = build_surfaces(g_sim.frame, tris, ss);
    log_collision(st, tris.size(), ss);
    sm64_static_surfaces_load(surfaces.data(), (uint32_t)surfaces.size());
    Vec3 s = to_sm64(g_sim.frame, {at.x, at.y, at.z + 60});
    g_sim.id = sm64_mario_create(s.x, s.y, s.z);
    logf("mario create id=%d at=%.1f,%.1f,%.1f", g_sim.id, at.x, at.y, at.z + 60);
    return g_sim.id >= 0;
}

Vec3 mario_pos() { return to_game(g_sim.frame, {g_sim.state.position[0], g_sim.state.position[1], g_sim.state.position[2]}); }

void tick_collide_scenario() {
    if (g_sim.id < 0) {
        fnv::TESObjectCELL *c = settle_cell();
        if (c && !start_mario(c)) finish(false, "mario_create");
        return;
    }
    bool running = g_sim.ticks >= kLandTicks;
    SM64MarioInputs in = make_inputs(0, 0, running ? 1.0f : 0.0f, {});
    sm64_mario_tick(g_sim.id, &in, &g_sim.state, &g_geo);
    g_sim.ticks++;
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

void on_frame() {
    if (!g_ready || g_done) return;
    g_frames++;
    if (g_config.scenario == "boot" && g_frames == (uint32_t)g_config.frames) finish(true, "");
    else if (g_config.scenario == "cell") tick_cell_scenario();
    else if (g_config.scenario == "collide") tick_collide_scenario();
    if (!g_done && !g_config.scenario.empty() && g_frames >= kScenarioTimeout) finish(false, "timeout");
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
