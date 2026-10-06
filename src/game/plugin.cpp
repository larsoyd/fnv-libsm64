#include "core/arrival.h"
#include "core/attack.h"
#include "core/audio.h"
#include "core/config.h"
#include "core/dds.h"
#include "core/frame.h"
#include "core/geo.h"
#include "core/mesh.h"
#include "core/puffs.h"
#include "core/reach.h"
#include "core/regather.h"
#include "core/rom.h"
#include "core/stall.h"
#include "core/step.h"
#include "core/surfaces.h"
#include "core/trail.h"
#include "core/walls.h"
#include "core/window.h"
#include "game/actors.h"
#include "game/collision.h"
#include "game/control.h"
#include "game/doors.h"
#include "game/fnv.h"
#include "game/log.h"
#include "game/nvse.h"
#include "game/render.h"
#include "game/sound.h"

#include <algorithm>
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
constexpr float kCollisionRadius = 1500;
// game units, the window follows mario and faces turn toward him out to reach
constexpr float kWindowRadius = 1000;
// this far from the middle of the gathered box it is gathered again around him
constexpr float kRegatherMove = 400;
static_assert(kWindowRadius + kRegatherMove <= kCollisionRadius);
// further than this in one frame and it was the game that moved the player
const float kMovedGap = 1000;
const float kFaceReach = 200;
// sm64 units around mario's middle for the faces a stall line lists
const float kStallRange = 80;
const size_t kStallFaces = 8;
// game units across the ground, an actor this near is named on a stall too
const float kStallActor = 200;
const float kMaxLandErr = 1;
const int kLandTicks = 90;
const int kRunTicks = 60;
const float kRenderSpawnAhead = 150;
const int kRenderFaceTick = 60;
const int kRenderShotTicks[2] = {70, 110};
const int kRenderRunTick = 80;
const int kRenderTicks = 120;
const Vec3 kLight{0.4f, -0.6f, 0.7f};
const char *kTextureDirs[] = {"Data\\Textures", "Data\\Textures\\sm64nv"};
// the loader looks under data for these
const char *kTexturePath = "textures\\sm64nv\\mario.dds", *kPuffTexturePath = "textures\\sm64nv\\puffs.dds";
const int kJumpFrom = 160, kJumpTo = 200;
const int kControlShotTick = 262;
const int kControlTicks = 280;
// the game finishes the switch back to first person a few frames after the call
const int kRestoreTicks = 10;
// about as far as the courier reaches with the activate key
const float kDoorReach = 150;
// how long a used door swings and how often the world is gathered again meanwhile
const int kDoorSwingTicks = 60, kDoorLookTicks = 15;
// a place just arrived in may still be loading, so it is looked at again for this long
const int kArriveLookTicks = 120;
const uint32_t kActIdle = 0x0C400201;
// sm64 units, the game sets the player down a little over the ground at some doors
const float kStandDrop = 150;
// frames between tries to stand mario in a new place, and how many before giving up
const int kSeatEvery = 4, kSeatFrames = 600;
// game units, the game lowers a player without collision by 17.5 at most
const float kSettleMost = 25;
// game units, actors this near mario are solid to him
const float kActorReach = 600;

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
const Move kLeaveMoves[] = {{"mario", 40, 55}, {"saloon", 200, 210}, {"courier", 255, 270}};
const int kLeaveShots[] = {215, 272};
const ControlScript kLeave{kLeaveScript, kLeaveMoves, kLeaveShots, 285};

// let go in the house, sent to the saloon, then a save made with mario in control is loaded
const ScriptLine kAutotakeScript[] = {
    {60, "HoldKey 50"}, {63, "ReleaseKey 50"}, {75, "coc GSProspectorSaloonInterior"}, {160, "SaveGame sm64nvauto"},
    {175, "LoadGame sm64nvauto"}, {280, "player.SetAngle Z 270"}, {285, "HoldKey 17"}, {310, "ReleaseKey 17"},
};
const Move kAutotakeMoves[] = {{"loaded", 285, 310}};
const ControlScript kAutotake{kAutotakeScript, kAutotakeMoves, {}, 325};
// e far from any door, then e next to the way out of the house
const ScriptLine kDoorScript[] = {
    {40, "doors"}, {45, "mario 1925 1856 7360 0"}, {55, "HoldKey 18"}, {58, "ReleaseKey 18"},
    {70, "mario 2380 1560 7360 90"}, {80, "HoldKey 18"}, {83, "ReleaseKey 18"},
};
const int kDoorShots[] = {200};
const ControlScript kDoor{kDoorScript, {}, kDoorShots, 300};
// out of the house, a run east down the hill, along the road over a cell border, then a jump
const ScriptLine kOutsideScript[] = {
    {45, "mario 2380 1560 7360 90"}, {55, "HoldKey 18"}, {58, "ReleaseKey 18"},
    {150, "player.SetAngle Z 90"}, {160, "HoldKey 17"}, {230, "ReleaseKey 17"}, {240, "spot east"},
    {245, "player.SetAngle Z 106.5"}, {247, "mario -70000 2320 8400 106.5"}, {262, "HoldKey 17"}, {307, "ReleaseKey 17"},
    {317, "spot border"},
    {322, "player.SetPos X -71466.1"}, {322, "player.SetPos Y 1308.6"}, {322, "player.SetPos Z 8340"}, {407, "spot jumped"},
};
const Move kOutsideMoves[] = {{"east", 160, 230}, {"border", 262, 307}};
const ControlScript kOutside{kOutsideScript, kOutsideMoves, {}, 417};

// to a farm with a turning windmill, then a run east past it
const ScriptLine kWindmillScript[] = {
    {45, "cow WastelandNV -11 27"},
    {200, "player.SetAngle Z 87.1"}, {202, "mario -41650 115765 4420 87.1"}, {240, "HoldKey 17"}, {310, "ReleaseKey 17"},
    {320, "spot past"},
};
const ControlScript kWindmill{kWindmillScript, {}, {}, 330};

// over the highway bridge into primm, in sight of the hotel sign that keeps turning
const ScriptLine kPrimmScript[] = {
    {60, "player.SetAngle Z 90"}, {62, "mario -60780 -53450 5900 90"}, {110, "spot deck"}, {112, "HoldKey 17"},
    {240, "ReleaseKey 17"}, {250, "spot far"},
};
const ControlScript kPrimm{kPrimmScript, {}, {}, 260};

// a road to an airport, a running jump over a burned out car and a run at a chain link fence
const ScriptLine kAirportScript[] = {
    {45, "cow WastelandNV -5 16"},
    {200, "player.SetAngle Z 341.6"}, {202, "mario -19781 66160 4340 341.6"},
    {232, "HoldKey 17"}, {241, "HoldKey 57"}, {255, "ReleaseKey 57"}, {290, "ReleaseKey 17"}, {300, "spot car"},
    {305, "player.SetAngle Z 90"}, {307, "mario -19880 67752 4350 90"}, {335, "HoldKey 17"}, {385, "ReleaseKey 17"},
    {395, "spot fence"},
};
const ControlScript kAirport{kAirportScript, {}, {}, 405};
// off the pavement in front of an office block, twice, where each kerb used to start a slide
const ScriptLine kKerbScript[] = {
    {40, "player.SetAngle Z 136"}, {42, "mario 2832.8 41906.4 4480 136"}, {70, "HoldKey 17"}, {95, "ReleaseKey 17"},
    {105, "spot kerb_a"},
    {110, "player.SetAngle Z 152"}, {112, "mario 2790.2 41663.9 4480 152"}, {140, "HoldKey 17"}, {165, "ReleaseKey 17"},
    {175, "spot kerb_b"},
};
const ControlScript kKerb{kKerbScript, {}, {}, 185};
// across a terminal hall whose floor is two triangles far larger than what is gathered
const ScriptLine kTerminalScript[] = {
    {40, "player.SetAngle Z 90"}, {42, "mario 2641.3 1308 18260 90"}, {66, "state"}, {70, "spot hall"}, {72, "HoldKey 17"},
    {112, "ReleaseKey 17"}, {122, "spot hall_far"},
};
const int kTerminalShots[] = {68};
const ControlScript kTerminal{kTerminalScript, {}, kTerminalShots, 132};
// out of the house by its door and straight back in by the door outside
const ScriptLine kInoutScript[] = {
    {45, "mario 2380 1560 7360 90"}, {55, "HoldKey 18"}, {58, "ReleaseKey 18"},
    {175, "state"}, {185, "doors"}, {187, "mario -73280 1297 8760 270"}, {210, "HoldKey 18"}, {213, "ReleaseKey 18"},
    {305, "state"},
};
// the screen stays washed out for a few seconds after a door to the outside
const int kInoutShots[] = {180, 310};
const ControlScript kInout{kInoutScript, {}, kInoutShots, 325};

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
// walks into the nearly upright sides of the shelf by the south wall and back out
const ScriptLine kSteepScript[] = {
    {40, "player.SetAngle Z 292.5"}, {45, "mario 1696 1102 7360 292.5"}, {55, "HoldKey 17"}, {145, "ReleaseKey 17"},
    {150, "spot steep"}, {152, "player.SetAngle Z 112.5"}, {155, "HoldKey 17"}, {245, "ReleaseKey 17"},
    {250, "spot steep_out"},
};
const ControlScript kSteep{kSteepScript, {}, {}, 260};
// walks off the round table in the west room from three spots on its top
const ScriptLine kTableScript[] = {
    {40, "player.SetAngle Z 0"}, {45, "mario 1189.8 882.7 7416.3 0"}, {55, "HoldKey 17"}, {67, "ReleaseKey 17"},
    {72, "spot table_a"}, {75, "player.SetAngle Z 45"}, {80, "mario 1200 863.7 7416.3 45"}, {90, "HoldKey 17"},
    {102, "ReleaseKey 17"}, {107, "spot table_b"}, {110, "player.SetAngle Z 247.5"}, {115, "mario 1110 933.7 7416.3 247.5"},
    {125, "HoldKey 17"}, {137, "ReleaseKey 17"}, {142, "spot table_c"},
};
const ControlScript kTable{kTableScript, {}, {}, 150};
// into the oven, into the shut bathroom door, then through it once e has swung it open
const ScriptLine kSolidScript[] = {
    {40, "player.SetAngle Z 0"}, {45, "mario 1071.8 1010.5 7360 0"}, {55, "HoldKey 17"}, {95, "ReleaseKey 17"}, {105, "spot oven"},
    {110, "player.SetAngle Z 90"}, {115, "mario 1542.5 832.7 7360 90"}, {125, "HoldKey 17"}, {165, "ReleaseKey 17"},
    {175, "spot door_shut"}, {180, "HoldKey 18"}, {183, "ReleaseKey 18"},
    {260, "HoldKey 17"}, {300, "ReleaseKey 17"}, {310, "spot door_open"},
};
const ControlScript kSolid{kSolidScript, {}, {}, 320};
// holds the sound device to see the stall recovery bring the stream back for the next jump
const ScriptLine kSoundScript[] = {
    {40, "HoldKey 57"}, {43, "ReleaseKey 57"}, {70, "sound pause"}, {105, "sound status"},
    {110, "HoldKey 57"}, {113, "ReleaseKey 57"}, {140, "sound status"},
};
const ControlScript kSound{kSoundScript, {}, {}, 150};
// six seconds in the pause menu with the stream idle, then a jump has to be heard again
const ScriptLine kSoundPauseScript[] = {
    {40, "HoldKey 57"}, {43, "ReleaseKey 57"}, {60, "sound status"}, {65, "HoldKey 1"}, {68, "ReleaseKey 1"},
    {245, "HoldKey 1"}, {248, "ReleaseKey 1"}, {260, "sound status"}, {265, "HoldKey 57"}, {268, "ReleaseKey 57"},
    {295, "sound status"},
};
const ControlScript kSoundPause{kSoundPauseScript, {}, {}, 305};

// tab opens the pip-boy and closes it again
const ScriptLine kPipboyScript[] = {
    {40, "state"}, {45, "HoldKey 15"}, {48, "ReleaseKey 15"}, {110, "state"},
    {130, "HoldKey 15"}, {133, "ReleaseKey 15"}, {200, "state"},
};
const ControlScript kPipboy{kPipboyScript, {}, {}, 215};

// the doctor is stood on open floor, then a run east at him and a drop onto his head
const ScriptLine kActorsScript[] = {
    {42, "mario 2285 2065 7360 0"}, {50, "actor SetRestrained 1"}, {51, "actor SetPos X 2352"}, {51, "actor SetPos Y 1603.6"},
    {51, "actor SetPos Z 7360"}, {53, "player.SetAngle Z 90"}, {55, "mario 2202 1603.6 7360 90"}, {70, "actors 600"},
    {75, "spot west"}, {77, "HoldKey 17"}, {137, "ReleaseKey 17"}, {145, "spot blocked"},
    {150, "mario 2352 1603.6 7560 90"}, {215, "spot dropped"}, {220, "actor SetRestrained 0"},
};
const ControlScript kActors{kActorsScript, {}, {}, 230};
// a settler made next to mario: a punch facing away, one at him, a jump kick and a pound
const ScriptLine kAttackScript[] = {
    {40, "player.SetAngle Z 90"}, {42, "player.PlaceAtMe 00104F02 1"}, {55, "actor SetRestrained 1"},
    {60, "beside -85 0 270"}, {70, "actors 600"}, {75, "HoldKey 42"}, {77, "ReleaseKey 42"},
    {100, "beside -85 0 90"}, {105, "HoldKey 42"}, {107, "ReleaseKey 42"},
    {112, "puffs"}, {120, "actors 600"}, {125, "puffs"}, {135, "actor SetRestrained 0"},
    {140, "beside -85 0 90"}, {150, "HoldKey 57"}, {153, "ReleaseKey 57"}, {156, "HoldKey 42"}, {158, "ReleaseKey 42"},
    {165, "actors 600"}, {200, "actors 600"}, {205, "puffs"},
    {215, "beside -85 0 90"}, {220, "HoldKey 57"}, {223, "ReleaseKey 57"}, {228, "HoldKey 29"}, {232, "ReleaseKey 29"},
    {270, "actors 600"},
};
const int kAttackShots[] = {200};
// a pound from a jump, then a run down the hall
const ScriptLine kParticlesScript[] = {
    {40, "player.SetAngle Z 90"}, {42, "mario 2202 1603.6 7360 90"}, {55, "puffs"},
    {60, "HoldKey 57"}, {63, "ReleaseKey 57"}, {68, "HoldKey 29"}, {72, "ReleaseKey 29"}, {100, "puffs"},
    {103, "mario 2120 1603.6 7360 90"}, {105, "HoldKey 17"}, {118, "puffs"}, {125, "ReleaseKey 17"},
};
const int kParticlesShots[] = {56, 90, 120};
const ControlScript kParticles{kParticlesScript, {}, kParticlesShots, 135};
const ControlScript kAttack{kAttackScript, {}, kAttackShots, 285};

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
    fnv::TESObjectCELL *cell = nullptr;
    int32_t id = -1;
    int ticks = 0, swing_until = -1, arrive_until = -1;
    // references with a shape when the world was last gathered
    int refs = 0;
    SM64MarioState state{};
    Vec3 run_start{}, run_mid{};
    float floor_z = 0, min_z = 0;
};
Sim g_sim;
SurfaceWindow g_window{{}, 0, 0};
Regather g_regather{kRegatherMove};
std::vector<uint32_t> g_owners;
uint32_t g_solid;
uint32_t g_window_loads;
StallWatch g_stall;
ActorBoxes g_boxes;
std::vector<LiveActor> g_near;
ActorStats g_actor_stats;
Swing g_swing;
Thrown g_thrown;
LastFit g_fit;
Trail g_trail;
FixedStep g_step;
MeshOut g_mesh, g_decal, g_puff_mesh;
Particles g_particles;

// how often mario has raised each effect since the last take
struct PuffCounts {
    int dust = 0, ring = 0, wall_stars = 0, ground_stars = 0, hits = 0;
};
PuffCounts g_puffs;

struct Smooth {
    int frames = 0, hitches = 0;
    float max_step = 0;
};

struct Control {
    Pad pad{};
    Smooth smooth;
    Vec3 last{}, move_from{};
    float jump_floor = 0, jump_peak = 0, max_gap = 0;
    // how far under his placing the game has let the player sink
    float settle = 0;
    int frames = 0, tick = 0, restore_check = 0;
    uint32_t action = 0;
    bool started = false, placed = false, blocked = false;
    // frames spent with the player still mario's and no mario, 0 when he has one or is let go
    int carry = 0;
    Press toggle, activate;
    ControlState saved{};
    fnv::TESObjectCELL *cell = nullptr;
    uintptr_t place = 0;
    const ControlScript *script = nullptr;
};
Control g_ctl;
ArrivalWatch g_arrival(kSettleFrames);

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

double seconds_now() {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq), QueryPerformanceCounter(&now);
    return double(now.QuadPart) / freq.QuadPart;
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
    std::vector<uint8_t> puffs = puff_atlas(g_rom);
    if (puffs.empty()) return logf("refused: rom holds no dust pictures where they should be");
    path = g_dir + kTextureDirs[1] + "\\puffs.dds";
    dds = rgba_dds(puffs.data(), kPuffAtlasWidth, kPuffCell, kPuffAtlasWidth);
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

// outdoors every cell of a worldspace is the same place
uintptr_t place(const fnv::TESObjectCELL *c) {
    return c && !c->interior() ? reinterpret_cast<uintptr_t>(c->worldSpace) : reinterpret_cast<uintptr_t>(c);
}

int count_refs(const fnv::TESObjectCELL *c) {
    int n = 0;
    for (const fnv::ListNode<fnv::TESObjectREFR> *it = &c->objectList; it; it = it->next) n += it->data != nullptr;
    return n;
}

fnv::TESObjectCELL *settle_cell() {
    if (g_frames == kMenuFrames && !g_config.cell.empty()) run_console("coc " + g_config.cell);
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
    logf("collision refs=%d bodies=%d tris=%u floors=%u walls=%u ceilings=%u degenerate=%u stood_up=%u still_steep=%u steps=%u",
         st.refs, st.bodies, (unsigned)tris, ss.floors, ss.walls, ss.ceilings, ss.degenerate, ss.stood_up, ss.still_steep, ss.steps);
    logf("havok scale samples=%d off=%d max_err=%.3f owner=%08X node=%.3f,%.3f,%.3f body=%.3f,%.3f,%.3f", st.scale.samples,
         st.scale.off, st.scale.worst, st.scale_worst_owner, st.scale_worst_node.x, st.scale_worst_node.y, st.scale_worst_node.z,
         st.scale_worst_body.x, st.scale_worst_body.y, st.scale_worst_body.z);
    logf("havok turn samples=%d off=%d max_err=%.4f owner=%08X keyframed=%d", st.turn.samples, st.turn.off, st.turn.worst,
         st.turn_worst_owner, st.keyframed);
    if (st.land_quads) logf("collision land cells=%d quads=%d max_err=%.3f", st.cells, st.land_quads, st.land_max_err);
    for (const auto &[name, n] : st.skipped_types) logf("collision skipped type=%s count=%d", name.c_str(), n);
    for (const auto &[layer, n] : st.skipped_layers) logf("collision skipped layer=%d count=%d", layer, n);
}

void sync_window(Vec3 feet) {
    if (!g_window.update(feet)) return;
    load_window(g_window);
    g_window_loads++;
}

void log_window(const char *when) {
    logf("collision window when=%s loaded=%u solid=%u gathers=%u flips=%u settled=%u loads=%u", when,
         (unsigned)g_window.loaded().size(), g_solid, g_window.stats.gathers, g_window.stats.flips, g_window.stats.settled, g_window_loads);
}

// why what was gathered cannot be trusted, empty when it can
std::string distrust(const CollisionStats &st) {
    char buf[160] = "";
    if (!st.scale.holds())
        snprintf(buf, sizeof buf, "havok scale samples=%d off=%d max_err=%.3f owner=%08X limit=%.1f", st.scale.samples, st.scale.off,
                 st.scale.worst, st.scale_worst_owner, st.scale.limit);
    else if (!st.turn.holds())
        snprintf(buf, sizeof buf, "havok turn samples=%d off=%d max_err=%.4f owner=%08X limit=%.2f", st.turn.samples, st.turn.off,
                 st.turn.worst, st.turn_worst_owner, st.turn.limit);
    else if (st.land_max_err > kMaxLandErr)
        snprintf(buf, sizeof buf, "land quads=%d max_err=%.3f limit=%.1f", st.land_quads, st.land_max_err, kMaxLandErr);
    return buf;
}

struct Gathered {
    size_t tris;
    // bodies found away from their nodes and turned away from them
    int off, turned;
};

// collision around center in mario's frame, a refusal keeps the set that was there
// the first gather in a place logs what it found, a take by hand also writes it out
Gathered gather_world(Vec3 center, bool first, bool dump = false) {
    CollisionStats st;
    std::vector<Tri> tris = gather_collision(g_sim.cell, center, kCollisionRadius, st);
    SurfaceStats ss;
    std::vector<uint32_t> kept;
    std::vector<SM64Surface> surfaces = build_surfaces(g_sim.frame, tris, ss, &kept);
    if (dump) write_obj((g_dir + "sm64nv_collision.obj").c_str(), tris);
    if (first) log_collision(st, tris.size(), ss);
    std::string why = distrust(st);
    if (!why.empty()) {
        logf("refused: %s", why.c_str());
        g_regather.refused(g_sim.ticks);
        return {0, st.scale.off, st.turn.off};
    }
    std::vector<bool> fixed;
    g_owners.clear(), g_solid = 0;
    for (uint32_t i : kept) g_owners.push_back(tris[i].owner), fixed.push_back(tris[i].solid), g_solid += tris[i].solid;
    g_window = SurfaceWindow(std::move(surfaces), kWindowRadius * g_config.scale, kFaceReach * g_config.scale, fixed);
    g_regather.loaded(center);
    g_sim.refs = st.refs;
    return {tris.size(), st.scale.off, st.turn.off};
}

void regather_when_far() {
    Vec3 m = to_game(g_sim.frame, g_ticks.cur_pos);
    // what was gathered still has a door he just used where it was
    bool swinging = g_sim.ticks <= g_sim.swing_until && (g_sim.swing_until - g_sim.ticks) % kDoorLookTicks == 0;
    // a place just arrived in is gathered again when more of it has loaded and once at the end
    int left = g_sim.arrive_until - g_sim.ticks;
    bool arriving = left >= 0 && left % kDoorLookTicks == 0 && (!left || loaded_refs(g_sim.cell) != g_sim.refs);
    if (!g_regather.due(m, g_sim.ticks) && !swinging && !arriving) return;
    double t0 = seconds_now();
    float moved = g_regather.moved(m);
    Gathered g = gather_world(m, false);
    logf("collision regather tick=%d moved=%.1f ms=%.1f ok=%d tris=%u off=%d turned=%d", g_sim.ticks, moved,
         (seconds_now() - t0) * 1000, g.tris != 0, (unsigned)g.tris, g.off, g.turned);
}

bool start_mario(fnv::TESObjectCELL *cell, float ahead, bool dump = false) {
    fnv::TESObjectREFR *p = fnv::player();
    float h = p->rot[2];
    Vec3 at{p->pos[0] + std::sin(h) * ahead, p->pos[1] + std::cos(h) * ahead, p->pos[2]};
    g_sim.frame = {at, g_config.scale};
    g_sim.cell = cell;
    if (!gather_world(at, true, dump).tris) return false;
    Vec3 s = to_sm64(g_sim.frame, {at.x, at.y, at.z + 60});
    g_window_loads = 0, g_stall = {}, g_fit = {}, g_trail.clear();
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

// the dust and stars of this tick, counted by kind
void raise_puffs(uint32_t flags) {
    g_puffs.dust += (flags & kPuffDust) != 0, g_puffs.ring += (flags & kPuffRing) != 0;
    g_puffs.wall_stars += (flags & kPuffWallStars) != 0, g_puffs.ground_stars += (flags & kPuffGroundStars) != 0;
    g_puffs.hits += (flags & kPuffHit) != 0;
    if (g_config.particles) g_particles.step(flags, g_ticks.cur_pos, g_sim.state.faceAngle);
}

void log_puffs() {
    auto shards = std::ranges::count_if(g_particles.alive(), [](const Particle &p) { return p.model == Puff::shard; });
    logf("particles tick=%d dust=%d ring=%d wall_stars=%d ground_stars=%d hits=%d alive=%u shards=%d drawn=%u", g_ctl.tick,
         g_puffs.dust, g_puffs.ring, g_puffs.wall_stars, g_puffs.ground_stars, g_puffs.hits, (unsigned)g_particles.alive().size(),
         (int)shards, g_puff_mesh.tris);
}

// whoever stands near mario this tick, as shapes he cannot walk through
void sync_actors() {
    g_actor_stats = {};
    g_near = nearby_actors(g_sim.cell, to_game(g_sim.frame, g_ticks.cur_pos), kActorReach, g_actor_stats);
    std::vector<ActorBody> bodies;
    for (const LiveActor &a : g_near) bodies.push_back(a.body);
    g_boxes.sync(g_sim.frame, bodies);
}

void tick_mario(const SM64MarioInputs &in) {
    regather_when_far();
    sync_window(g_ticks.cur_pos);
    sync_actors();
    g_ticks.tick(g_sim.id, in, g_sim.state);
    g_sim.ticks++;
    raise_puffs(g_sim.state.particleFlags);
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
    if (mario_mesh_follow(body_parent())) logf("mesh hung %s", mario_mesh_chain().c_str());
    convert_mesh(g_sim.frame, g_drawn.view(), m, kLight, g_mesh);
    convert_decal(g_sim.frame, g_drawn.view(), m, kLight, g_decal);
    Vec3 eye;
    static const std::vector<Particle> kNone;
    // with no camera to face there is nothing to draw them toward
    build_puffs(g_sim.frame, camera_pos(eye) ? g_particles.alive() : kNone, alpha, eye, m, g_puff_mesh);
    mario_mesh_update(g_mesh, g_decal, g_puff_mesh, m);
    return m;
}

bool taken() { return g_sim.id >= 0; }

// the player is mario's, also between two places while there is no mario
bool held() { return taken() || g_ctl.carry; }

void drop_mario() {
    if (taken()) sm64_mario_delete(g_sim.id);
    g_sim.id = -1;
    g_boxes.clear(), g_near.clear(), g_thrown.clear(), g_particles.clear(), g_puffs = {};
}

bool spawn_drawn_mario(fnv::TESObjectCELL *c, float ahead, std::string &why, bool dump = false) {
    if (!start_mario(c, ahead, dump)) why = "mario_create";
    else if (!mario_mesh_create(kTexturePath, kPuffTexturePath, why)) why = "mesh " + why;
    if (!why.empty()) drop_mario();
    frame_seconds();
    return why.empty();
}

void tick_render_scenario() {
    if (g_sim.id < 0) {
        std::string why;
        fnv::TESObjectCELL *c = settle_cell();
        if (c && !spawn_drawn_mario(c, kRenderSpawnAhead, why)) finish(false, why.c_str());
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

Vec3 player_pos() { return {fnv::player()->pos[0], fnv::player()->pos[1], fnv::player()->pos[2]}; }

void refuse_take(const std::string &why) {
    drop_mario();
    mario_mesh_hide();
    logf("refused: take reason=%s tick=%d", why.c_str(), g_ctl.tick);
}

void take_control() {
    fnv::TESObjectCELL *c = loaded_cell();
    if (!c) return logf("control take skipped tick=%d reason=no_cell", g_ctl.tick);
    g_ctl.cell = c, g_ctl.place = place(c);
    logf("control cell tick=%d id=%08X", g_ctl.tick, c->form.refID);
    g_ctl.saved = control_state();
    logf("control saved tick=%d %s", g_ctl.tick, describe(g_ctl.saved).c_str());
    g_ctl.restore_check = 0;
    // mario comes first so a place he cannot stand in leaves the courier as he was
    std::string why;
    if (!spawn_drawn_mario(c, 0, why, true) || !take_player(g_ctl.saved, why)) return refuse_take(why);
    focus_game();
    if (!sound_ready() && sound_open(why)) logf("sound open rate=%d", kAudioRate);
    else if (!sound_ready()) logf("sound unavailable reason=%s", why.c_str());
    // the camera aims at courier eye height so tilt it down onto mario
    run_console("player.SetAngle X 20");
    ControlState cs = control_state();
    logf("control take controls=%02X noclip=%d hidden=%d", cs.controls, cs.noclip, cs.hidden);
}

// the last few seconds of what mario did, oldest first
void log_trail() {
    for (const std::string &line : g_trail.lines()) logf("trail %s", line.c_str());
}

void release_control(const char *reason) {
    // letting go by hand is often the way out of a bad spot, so say how he got there
    if (!strcmp(reason, "key")) log_trail();
    drop_mario();
    g_ctl.placed = false, g_ctl.carry = 0, g_ctl.settle = 0;
    mario_mesh_hide();
    release_player(g_ctl.saved);
    logf("control release tick=%d reason=%s", g_ctl.tick, reason);
    // a save load replaces what was put back so there is nothing to read back
    g_ctl.restore_check = strcmp(reason, "load") ? g_ctl.tick + kRestoreTicks : 0;
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
    logf("stall tick=%d pos=%s action=%08X floor=%.1f ceil=%.1f faces=%u from_gather=%.1f", t, xyz(mario_pos()).c_str(),
         g_sim.state.action, g_sim.frame.origin.z + floor / g_sim.frame.scale, g_sim.frame.origin.z + ceil / g_sim.frame.scale,
         (unsigned)faces.size(), g_regather.moved(mario_pos()));
    for (const LiveActor &a : g_near)
        if (std::hypot(a.body.feet.x - mario_pos().x, a.body.feet.y - mario_pos().y) < kStallActor)
            logf("stall actor ref=%08X at=%s half=%.1f,%.1f", a.body.id, xyz(a.body.feet).c_str(), a.body.half_width, a.body.half_length);
    for (size_t k = 0; k < faces.size() && k < kStallFaces; k++) {
        const SM64Surface &s = g_window.loaded()[faces[k]];
        Vec3 n = dir_to_game(surface_normal(s)), c{0, 0, 0};
        for (const auto &v : s.vertices) c = {c.x + v[0] / 3.0f, c.y + v[1] / 3.0f, c.z + v[2] / 3.0f};
        logf("stall face kind=%s owner=%08X normal=%.2f,%.2f,%.2f at=%s", surface_kind(surface_normal(s)),
             g_owners[g_window.source(faces[k])], n.x, n.y, n.z, xyz(to_game(g_sim.frame, c)).c_str());
    }
}

// nothing lets mario in where he does not fit, so there he was pushed and he goes back
void keep_fit(int t) {
    Vec3 at = mario_pos();
    float room = g_window.headroom(g_ticks.cur_pos);
    int was = g_fit.low(), low = g_fit.feed(g_ticks.cur_pos, room);
    if (!low && was) logf("unstuck tick=%d ticks=%d pos=%s", t, was, xyz(at).c_str());
    if (!low) return;
    g_ticks.put_back(g_sim.id, g_fit.pos(), g_sim.state);
    if (low > 1) return;
    logf("stuck tick=%d pos=%s action=%08X room=%.1f needs=%.1f back=%s", t, xyz(at).c_str(), g_sim.state.action,
         room / g_sim.frame.scale, LastFit::kHeight / g_sim.frame.scale, xyz(mario_pos()).c_str());
    log_trail();
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
    g_ticks.reset(s), g_fit = {};
    logf("control teleport tick=%d to=%s heading=%.1f", g_ctl.tick, xyz(g).c_str(), deg);
}

// stands mario east and north of the nearest actor by so much, facing a heading
void teleport_beside(const char *args) {
    float dx, dy, deg;
    if (g_near.empty() || sscanf(args, "%f %f %f", &dx, &dy, &deg) != 3) return finish(false, "beside");
    Vec3 a = g_near[0].body.feet;
    char to[96];
    snprintf(to, sizeof to, "%.1f %.1f %.1f %.1f", a.x + dx, a.y + dy, a.z + 1, deg);
    teleport_mario(to);
}

// wall clock so a recording of the stream can be lined up with the windows
uint64_t unix_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000 - 11644473600000ull;
}

uint64_t g_sound_from;

void log_sound_status() {
    SoundStats st = sound_take_stats();
    logf("sound status written=%u done=%u queued=%u peak=%d errors=%u resets=%u", st.written, st.done, st.queued, st.peak,
         st.errors, st.resets);
}

void log_sound(const char *name) {
    SoundStats st = sound_take_stats();
    uint64_t now = unix_ms();
    logf("control sound name=%s peak=%d written=%u done=%u start_ms=%llu end_ms=%llu", name, st.peak, st.written, st.done,
         (unsigned long long)g_sound_from, (unsigned long long)now);
    g_sound_from = now;
}

const char *kAttackNames[] = {"none", "punch", "kick", "dive", "pound"};

// the blow mario is throwing lands on whoever it reaches, once each
void land_blows(int t) {
    Attack now = attack_now(g_sim.state.action, g_sim.state.flags);
    g_swing.tick(now);
    if (now == Attack::none) return;
    AttackProfile p = attack_profile(now);
    Vec3 feet = mario_pos();
    float heading = heading_from_sm64_yaw(g_sim.state.faceAngle), scale = g_sim.frame.scale;
    for (const LiveActor &a : g_near) {
        if (!attack_reaches(p, feet, LastFit::kHeight / scale, heading, a.body) || !g_swing.lands(a.body.id)) continue;
        float before = actor_health(a.ref), damage = strike(a.ref, p.damage * g_config.punch);
        logf("attack hit tick=%d kind=%s ref=%08X damage=%.1f health=%.1f>%.1f", t, kAttackNames[(int)now], a.body.id, damage, before,
             actor_health(a.ref));
        if (p.push > 0 && g_thrown.allow(a.body.id, t))
            logf("attack push tick=%d ref=%08X force=%.1f ok=%d", t, a.body.id, p.push, shove(a.ref, feet, p.push));
        // libsm64 gives mario his own recoil and the sound of the hit
        Vec3 n = nearest_on(a.body, feet);
        Vec3 s = to_sm64(g_sim.frame, {n.x, n.y, a.body.feet.z});
        bool met = sm64_mario_attack(g_sim.id, s.x, s.y, s.z, a.body.height * scale);
        // sm64 bursts shards off what a fist or a foot meets, the flag comes too late for this tick
        if (met && (now == Attack::punch || now == Attack::kick)) {
            g_puffs.hits++;
            if (g_config.particles) g_particles.emit(kPuffHit, g_ticks.cur_pos, g_sim.state.faceAngle);
        }
    }
}

void log_actors(float reach) {
    ActorStats st;
    Vec3 m = mario_pos();
    std::vector<LiveActor> found = nearby_actors(g_ctl.cell, m, reach, st);
    logf("control actors tick=%d near=%u seen=%d far=%d down=%d unsized=%d boxes=%u", g_ctl.tick, (unsigned)found.size(), st.seen,
         st.away, st.down, st.unsized, (unsigned)g_boxes.size());
    for (const LiveActor &a : found)
        logf("actor ref=%08X base=%08X type=%02X pos=%s heading=%.0f half=%.1f,%.1f height=%.1f dist=%.1f health=%.1f knocked=%d", a.body.id,
             a.ref->baseForm->refID, a.ref->baseForm->typeID, xyz(a.body.feet).c_str(), a.body.heading * 180 / 3.14159265f,
             a.body.half_width, a.body.half_length, a.body.height,
             std::hypot(a.body.feet.x - m.x, a.body.feet.y - m.y, a.body.feet.z - m.z), actor_health(a.ref), knocked(a.ref));
}

// a console line run on the actor nearest mario
void run_on_nearest(const char *line) {
    if (g_near.empty()) return finish(false, "no_actor");
    unsigned ret = g_console->runScriptLine(line, g_near[0].ref);
    logf("console actor=%08X line=%s ok=%d", g_near[0].body.id, line, ret != 0);
}

void log_doors() {
    std::vector<Door> doors = cell_doors(g_ctl.cell);
    logf("control doors tick=%d cell=%08X count=%u", g_ctl.tick, g_ctl.cell->form.refID, (unsigned)doors.size());
    for (const Door &d : doors)
        logf("door ref=%08X base=%08X pos=%s heading=%.0f teleports=%d", d.ref->form.refID, d.ref->baseForm->refID, xyz(d.pos).c_str(),
             d.heading * 180 / 3.14159265f, d.teleports);
}

// the game refuses its own activate key while the player's movement is off
void use_door() {
    std::vector<Door> doors = cell_doors(g_ctl.cell);
    std::vector<Vec3> at;
    for (const Door &d : doors) at.push_back(d.pos);
    Vec3 m = mario_pos();
    int i = nearest_within(at, m, kDoorReach);
    if (i < 0) return logf("control door tick=%d ref=none doors=%u", g_ctl.tick, (unsigned)doors.size());
    const Door &d = doors[i];
    logf("control door tick=%d ref=%08X teleports=%d dist=%.1f", g_ctl.tick, d.ref->form.refID, d.teleports,
         std::hypot(d.pos.x - m.x, d.pos.y - m.y, d.pos.z - m.z));
    logf("control activated ref=%08X ok=%d", d.ref->form.refID, activate(d.ref));
    if (!d.teleports) g_sim.swing_until = g_sim.ticks + kDoorSwingTicks;
}

void control_tick() {
    const ControlScript &s = *g_ctl.script;
    float cam = fnv::player()->rot[2];
    int t = ++g_ctl.tick;
    if (taken()) {
        // menus and the console still see the keys, mario must not
        Pad pad = g_ctl.blocked ? Pad{} : g_ctl.pad;
        tick_mario(make_inputs(cam, pad.right, pad.forward, pad.buttons));
        land_blows(t);
        g_trail.add(t, mario_pos(), g_sim.state.action, pad.forward, pad.right, pad.buttons);
        keep_fit(t);
        if (g_stall.feed(g_ticks.cur_pos, std::hypot(pad.right, pad.forward))) log_stall(t), log_trail();
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
        else if (!strncmp(line.line, "beside ", 7)) teleport_beside(line.line + 7);
        else if (!strcmp(line.line, "sound pause")) sound_pause();
        else if (!strcmp(line.line, "sound status")) log_sound_status();
        else if (!strcmp(line.line, "doors")) log_doors();
        else if (!strcmp(line.line, "puffs")) log_puffs();
        else if (!strncmp(line.line, "actors ", 7)) log_actors((float)atof(line.line + 7));
        else if (!strncmp(line.line, "actor ", 6)) run_on_nearest(line.line + 6);
        else if (!strcmp(line.line, "state")) {
            logf("control state tick=%d %s", t, describe(control_state()).c_str());
            logf("mesh chain tick=%d %s", t, mario_mesh_chain().c_str());
            logf("body chain tick=%d %s", t, node_chain(fnv::player()->renderState->niNode).c_str());
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
    if (t % kStatusTicks == 0 && taken()) log_window("status"), log_puffs();
    if (t % kStatusTicks == 0 && sound_ready()) log_sound_status();
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

// how far the player is from where mario put him last
float player_gap() {
    Vec3 p = player_pos();
    return g_ctl.placed ? std::hypot(p.x - g_ctl.last.x, p.y - g_ctl.last.y, p.z - g_ctl.last.z) : 0;
}

// a door or a jump by the game takes mario's world away, the player stays his on the way
void carry_over(const char *reason) {
    drop_mario();
    mario_mesh_hide();
    g_ctl.placed = false, g_ctl.carry = 1, g_ctl.settle = 0;
    logf("control carry tick=%d reason=%s from=%s", g_ctl.tick, reason, xyz(g_ctl.last).c_str());
}

// the cell under the player changes with doors, loads and plain walking outdoors
void follow_cell() {
    fnv::TESObjectCELL *c = loaded_cell();
    float gap = player_gap();
    if (place(c) != g_ctl.place) return carry_over("cell");
    if (gap > kMovedGap) return carry_over("moved");
    if (c == g_ctl.cell) return;
    g_ctl.cell = g_sim.cell = c;
    logf("control crossed tick=%d id=%08X", g_ctl.tick, c->form.refID);
}

// through a door he is simply there, on the floor under the player and facing his way
void stand_mario() {
    Vec3 feet = to_sm64(g_sim.frame, player_pos());
    float floor = sm64_surface_find_floor_height(feet.x, feet.y, feet.z);
    sm64_set_mario_faceangle(g_sim.id, sm64_yaw_from_heading(fnv::player()->rot[2]));
    // no floor within a step of the player's feet leaves him to drop in as on a take
    if (floor < feet.y - kStandDrop) return;
    sm64_set_mario_position(g_sim.id, feet.x, floor, feet.z);
    sm64_set_mario_action(g_sim.id, kActIdle);
    g_ticks.reset({feet.x, floor, feet.z});
    logf("control stood tick=%d at=%s under_player=%.1f", g_ctl.tick, xyz(to_game(g_sim.frame, g_ticks.cur_pos)).c_str(),
         (feet.y - floor) / g_sim.frame.scale);
}

// stands mario where the game put the player once that place is loaded
void seat_mario() {
    fnv::TESObjectCELL *c = loaded_cell();
    hide_body();
    if (!c || g_ctl.blocked) return;
    int frames = g_ctl.carry++;
    std::string why;
    if (frames % kSeatEvery == 1 && spawn_drawn_mario(c, 0, why)) {
        g_ctl.cell = c, g_ctl.place = place(c), g_ctl.carry = 0;
        g_sim.arrive_until = g_sim.ticks + kArriveLookTicks;
        stand_mario();
        run_console("player.SetAngle X 20");
        logf("control seated tick=%d id=%08X frames=%d", g_ctl.tick, c->form.refID, frames);
    } else if (frames >= kSeatFrames) {
        logf("refused: seat frames=%d cell=%08X", frames, c->form.refID);
        release_control("lost");
    }
}

void control_frame() {
    if (taken()) follow_cell();
    if (g_ctl.placed) g_ctl.max_gap = std::fmax(g_ctl.max_gap, player_gap()), g_ctl.frames++;
    // the game lowers a player without collision, slowly and never far
    // he is placed higher by what he was found low the frame before
    if (g_ctl.placed) g_ctl.settle = std::clamp(g_ctl.settle + g_ctl.last.z - player_pos().z, -kSettleMost, kSettleMost);
    Pad pad;
    bool toggle, activate;
    if (!read_game_pad(pad, toggle, activate)) return finish(false, "input_globals");
    if (pad != g_ctl.pad)
        logf("control input tick=%d forward=%.2f right=%.2f a=%d b=%d z=%d", g_ctl.tick, pad.forward, pad.right,
             pad.buttons.a, pad.buttons.b, pad.buttons.z);
    g_ctl.pad = pad;
    if (int n = take_hushed()) logf("control hush tick=%d count=%d", g_ctl.tick, n);
    bool blocked = menu_mode();
    // in a menu the view is the game's to change
    if (held() && !blocked && hold_player()) logf("control regrip tick=%d", g_ctl.tick);
    if (blocked != g_ctl.blocked) logf("control gate tick=%d blocked=%d menu=%d", g_ctl.tick, blocked, menu_mode());
    g_ctl.blocked = blocked;
    if (g_ctl.toggle.edge(toggle) && !blocked) held() ? release_control("key") : take_control();
    if (g_ctl.activate.edge(activate) && !blocked && taken()) use_door();
    if (g_done) return;
    run_ticks(control_tick);
    if (taken() && !blocked && sound_ready()) sound_pump();
    // a console line run this frame may have sent the player somewhere else already
    if (taken()) follow_cell();
    if (g_ctl.carry) seat_mario();
    if (g_config.autotake && g_arrival.frame(place(loaded_cell()), blocked, held())) {
        logf("control arrive tick=%d id=%08X", g_ctl.tick, loaded_cell()->form.refID);
        take_control();
    }
    if (!taken() || g_done) return;
    if (hide_body()) logf("control rehide tick=%d", g_ctl.tick);
    Vec3 drawn = draw_mario();
    if (in_move(g_ctl.tick)) track_smooth(std::hypot(drawn.x - g_ctl.last.x, drawn.y - g_ctl.last.y, drawn.z - g_ctl.last.z));
    g_ctl.last = drawn;
    move_player({drawn.x, drawn.y, drawn.z + g_ctl.settle});
    g_ctl.placed = true;
}

// scenarios take the player themselves once the cell settles unless arrivals do it
bool own_take() { return !g_config.scenario.empty() && !g_config.autotake; }

void tick_control_scenario(const ControlScript &script) {
    g_ctl.script = &script;
    if (g_ctl.started) return control_frame();
    fnv::TESObjectCELL *c = settle_cell();
    g_ctl.started = own_take() ? c != nullptr : loaded_cell() != nullptr;
    if (g_ctl.started && own_take()) take_control();
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
    else if (g_config.scenario == "autotake") tick_control_scenario(kAutotake);
    else if (g_config.scenario == "door") tick_control_scenario(kDoor);
    else if (g_config.scenario == "outside") tick_control_scenario(kOutside);
    else if (g_config.scenario == "windmill") tick_control_scenario(kWindmill);
    else if (g_config.scenario == "primm") tick_control_scenario(kPrimm);
    else if (g_config.scenario == "airport") tick_control_scenario(kAirport);
    else if (g_config.scenario == "kerb") tick_control_scenario(kKerb);
    else if (g_config.scenario == "terminal") tick_control_scenario(kTerminal);
    else if (g_config.scenario == "inout") tick_control_scenario(kInout);
    else if (g_config.scenario == "gamepad") tick_control_scenario(kGamepad);
    else if (g_config.scenario == "walls") tick_control_scenario(kWalls);
    else if (g_config.scenario == "steep") tick_control_scenario(kSteep);
    else if (g_config.scenario == "table") tick_control_scenario(kTable);
    else if (g_config.scenario == "solid") tick_control_scenario(kSolid);
    else if (g_config.scenario == "sound") tick_control_scenario(kSound);
    else if (g_config.scenario == "soundpause") tick_control_scenario(kSoundPause);
    else if (g_config.scenario == "pipboy") tick_control_scenario(kPipboy);
    else if (g_config.scenario == "actors") tick_control_scenario(kActors);
    else if (g_config.scenario == "attack") tick_control_scenario(kAttack);
    else if (g_config.scenario == "particles" || g_config.scenario == "noparticles") tick_control_scenario(kParticles);
    else if (g_config.scenario.empty()) tick_control_scenario(kPlay);
    // play runs until the game closes once it has the player
    bool endless = g_config.scenario == "play" && g_ctl.started;
    if (!g_done && !g_config.scenario.empty() && !endless && g_frames >= kScenarioTimeout) finish(false, "timeout");
}

void on_load_game(bool ok) {
    logf("control load tick=%d ok=%d", g_ctl.tick, ok);
    g_arrival.again();
}

void on_message(nvse::Message *m) {
    if (m->type == nvse::kMessagePostLoad) on_post_load();
    else if (m->type == nvse::kMessageMainGameLoop) on_frame();
    else if (m->type == nvse::kMessagePreLoadGame && held()) release_control("load");
    // the load result travels as the pointer value itself
    else if (m->type == nvse::kMessagePostLoadGame && g_ready) on_load_game(m->data != nullptr);
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
    std::string why;
    if (!hook_player_save(why)) return logf("refused: save hook %s", why.c_str()), false;
    // without it mario still plays, the game just clicks at his jumps
    if (!hook_activate_sound(why)) logf("refused: activate sound hook %s", why.c_str());
    g_console = static_cast<const nvse::ConsoleInterface *>(nvse->queryInterface(nvse::kInterfaceConsole));
    auto *msg = static_cast<const nvse::MessagingInterface *>(nvse->queryInterface(nvse::kInterfaceMessaging));
    if (!msg || !msg->registerListener(g_handle, "NVSE", on_message)) {
        logf("refused: messaging interface unavailable");
        return false;
    }
    return true;
}
