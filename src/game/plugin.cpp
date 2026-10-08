#include "core/arrival.h"
#include "core/attack.h"
#include "core/audio.h"
#include "core/builder.h"
#include "core/config.h"
#include "core/dds.h"
#include "core/frame.h"
#include "core/geo.h"
#include "core/health.h"
#include "core/hud.h"
#include "core/options.h"
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
#include "game/menu.h"
#include "game/overlay.h"
#include "game/render.h"
#include "game/pipboy.h"
#include "game/sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
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
// how often the doors are looked at for one that swung since the world was gathered
const int kDoorLookTicks = 15;
// what a frame may spend walking the engine for a regather
const double kWalkBudget = 0.003;
// a place just arrived in may still be loading, so it is looked at again for this long
const int kArriveLookTicks = 120;
const uint32_t kActIdle = 0x0C400201, kActFreefall = 0x0100088C;
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
// the hotel's corridor, run along it and back and along it again
const ScriptLine kHotelScript[] = {
    {60, "player.SetAngle Z 90"}, {62, "mario -380 -3660 256 90"}, {80, "HoldKey 17"}, {140, "ReleaseKey 17"}, {145, "spot east"},
    {150, "player.SetAngle Z 270"}, {160, "HoldKey 17"}, {220, "ReleaseKey 17"}, {225, "spot west"},
    {230, "player.SetAngle Z 90"}, {240, "HoldKey 17"}, {300, "ReleaseKey 17"}, {305, "spot again"},
};
const Move kHotelMoves[] = {{"east", 80, 140}, {"west", 160, 220}, {"again", 240, 300}};
const ControlScript kHotel{kHotelScript, kHotelMoves, {}, 315};

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
// into the shut bathroom door, then through it once the game has swung it open on its own
const ScriptLine kSwingScript[] = {
    {40, "player.SetAngle Z 90"}, {45, "mario 1542.5 832.7 7360 90"}, {55, "HoldKey 17"}, {95, "ReleaseKey 17"},
    {105, "spot door_shut"}, {107, "doors"}, {110, "open 001062AC"}, {130, "doors"}, {185, "doors"},
    {190, "HoldKey 17"}, {230, "ReleaseKey 17"}, {240, "spot door_open"},
};
const ControlScript kSwing{kSwingScript, {}, {}, 250};
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
const int kPipboyShots[] = {40, 52, 65, 95, 120, 140, 155, 200};
const ControlScript kPipboy{kPipboyScript, {}, kPipboyShots, 215};

const ScriptLine kPipboyArmsScript[] = {
    {40, "HoldKey 15"}, {43, "ReleaseKey 15"}, {95, "state"},
    {100, "HoldKey 205"}, {103, "ReleaseKey 205"}, {135, "state"},
    {150, "HoldKey 15"}, {153, "ReleaseKey 15"},
    {200, "HoldKey 50"}, {203, "ReleaseKey 50"}, {235, "state"},
    {245, "HoldKey 15"}, {248, "ReleaseKey 15"}, {295, "state"},
    {315, "HoldKey 15"}, {318, "ReleaseKey 15"},
    {355, "HoldKey 50"}, {358, "ReleaseKey 50"},
    {400, "HoldKey 15"}, {403, "ReleaseKey 15"}, {445, "state"},
    {470, "HoldKey 15"}, {473, "ReleaseKey 15"}, {520, "state"},
};
const int kPipboyArmsShots[] = {65, 95, 105, 112, 135, 295, 445, 520};
const ControlScript kPipboyArms{kPipboyArmsScript, {}, kPipboyArmsShots, 535};

const ScriptLine kPipboyViewScript[] = {
    {30, "player.SetAngle X 80"}, {40, "state"},
    {45, "HoldKey 15"}, {48, "ReleaseKey 15"}, {55, "state"}, {95, "state"},
    {130, "HoldKey 15"}, {133, "ReleaseKey 15"}, {140, "state"}, {200, "state"},
    {210, "player.SetAngle X -70"},
    {230, "HoldKey 15"}, {233, "ReleaseKey 15"}, {240, "state"}, {280, "state"},
    {310, "HoldKey 15"}, {313, "ReleaseKey 15"}, {320, "state"}, {380, "state"},
};
const int kPipboyViewShots[] = {40, 55, 95, 140, 200, 240, 280, 320, 380};
const ControlScript kPipboyView{kPipboyViewScript, {}, kPipboyViewShots, 395};

const ScriptLine kPipboyTabsScript[] = {
    {40, "HoldKey 15"}, {43, "ReleaseKey 15"}, {95, "state"},
    {100, "HoldKey 60"}, {103, "ReleaseKey 60"}, {135, "state"},
    {150, "HoldKey 61"}, {153, "ReleaseKey 61"}, {190, "state"},
};
const int kPipboyTabsShots[] = {95, 105, 112, 135, 160, 190};
const ControlScript kPipboyTabs{kPipboyTabsScript, {}, kPipboyTabsShots, 210};

const ScriptLine kPipboyEquipScript[] = {
    {34, "SetPCCanUsePowerArmor 1"},
    {35, "player.AddItem 00020423 1"}, {36, "player.AddItem 00014E13 1"},
    {40, "HoldKey 15"}, {43, "ReleaseKey 15"}, {90, "state"},
    {100, "player.EquipItem 00020423"}, {120, "state"},
    {140, "player.EquipItem 00014E13"}, {160, "state"},
    {180, "HoldKey 15"}, {183, "ReleaseKey 15"},
    {220, "player.SexChange"}, {250, "HoldKey 15"}, {253, "ReleaseKey 15"}, {295, "state"},
    {320, "HoldKey 15"}, {323, "ReleaseKey 15"}, {370, "state"},
};
const int kPipboyEquipShots[] = {90, 120, 160, 295, 370};
const ControlScript kPipboyEquip{kPipboyEquipScript, {}, kPipboyEquipShots, 385};

const ScriptLine kPipboyReloadScript[] = {
    {40, "SaveGame sm64nv_pipboy_test"}, {60, "HoldKey 15"}, {63, "ReleaseKey 15"}, {100, "state"},
    {120, "LoadGame sm64nv_pipboy_test"}, {220, "HoldKey 15"}, {223, "ReleaseKey 15"}, {260, "state"},
    {290, "HoldKey 15"}, {293, "ReleaseKey 15"}, {340, "state"},
};
const int kPipboyReloadShots[] = {100, 260, 340};
const ControlScript kPipboyReload{kPipboyReloadScript, {}, kPipboyReloadShots, 355};


// the doctor is stood on open floor, then a run east at him and a drop onto his head
const ScriptLine kActorsScript[] = {
    {42, "mario 2285 2065 7360 0"}, {50, "actor SetRestrained 1"}, {51, "actor SetPos X 2352"}, {51, "actor SetPos Y 1603.6"},
    {51, "actor SetPos Z 7360"}, {53, "player.SetAngle Z 90"}, {55, "mario 2202 1603.6 7360 90"}, {70, "actors 600"},
    {75, "spot west"}, {77, "HoldKey 17"}, {137, "ReleaseKey 17"}, {145, "spot blocked"},
    {150, "mario 2352 1603.6 7560 90"}, {215, "spot dropped"},
};
const ControlScript kActors{kActorsScript, {}, {}, 230};
// a settler made next to mario: a punch facing away, one at him, a jump kick and a pound
// the courier is a ghost so the settler cannot hit back and stays where the blows expect him
const ScriptLine kAttackScript[] = {
    {40, "player.SetAngle Z 90"}, {41, "player.SetGhost 1"}, {42, "player.PlaceAtMe 00104F02 1"}, {55, "actor SetRestrained 1"},
    {60, "beside -85 0 270"}, {70, "actors 600"}, {75, "HoldKey 42"}, {77, "ReleaseKey 42"},
    {95, "player.SetAV Unarmed 100"}, {100, "beside -85 0 90"}, {105, "HoldKey 42"}, {107, "ReleaseKey 42"},
    {112, "puffs"}, {120, "actors 600"}, {125, "puffs"}, {130, "player.SetAV Unarmed 0"}, {135, "actor SetRestrained 0"},
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
// a coyote made beside mario is set on the courier, whose health is mario's
const ScriptLine kHurtScript[] = {
    {40, "player.SetAngle Z 90"}, {42, "player.PlaceAtMe 00168D08 1"}, {55, "actor SetRestrained 1"},
    {60, "beside -85 0 90"}, {70, "actors 600"}, {75, "health"}, {76, "overlay"}, {78, "hudshot"}, {80, "actor SetRestrained 0"},
    {85, "actor StartCombat player"}, {200, "overlay"}, {202, "hudshot"},
    {300, "HoldKey 1"}, {303, "ReleaseKey 1"}, {310, "overlay"}, {330, "overlay"}, {332, "HoldKey 1"}, {335, "ReleaseKey 1"},
    {150, "health"}, {150, "actors 600"}, {250, "health"}, {250, "actors 600"}, {260, "HoldKey 31"}, {290, "ReleaseKey 31"},
    {350, "health"}, {355, "overlay"},
};
// he walks away while the coyote is at him, a bite never stops him
const Move kHurtMoves[] = {{"bitten", 260, 290}};
const ControlScript kHurt{kHurtScript, kHurtMoves, {}, 360};
// the courier dies while mario has him
const ScriptLine kDeathScript[] = {
    {40, "health"}, {60, "player.Kill"}, {100, "health"}, {120, "state"}, {130, "overlay"}, {182, "hudshot"}, {216, "hudshot"},
    {200, "overlay"},
};
const ControlScript kDeath{kDeathScript, {}, {}, 600};
// three settlers in a row up the street and a slide kick through them
const ScriptLine kBowlingScript[] = {
    {40, "player.SetAngle Z 90"}, {41, "player.SetGhost 1"},
    {42, "place 00104F02"}, {52, "set 00104F02 860 0"}, {53, "of 00104F02 SetRestrained 1"},
    {55, "place 00104F07"}, {65, "set 00104F07 940 30"}, {66, "of 00104F07 SetRestrained 1"},
    {68, "place 00104F09"}, {78, "set 00104F09 940 -30"}, {79, "of 00104F09 SetRestrained 1"},
    {86, "body 00104F02"}, {86, "body 00104F07"}, {86, "body 00104F09"}, {87, "spot start"}, {88, "HoldKey 17"},
    {130, "of 00104F02 SetRestrained 0"}, {130, "of 00104F07 SetRestrained 0"}, {130, "of 00104F09 SetRestrained 0"},
    {128, "HoldKey 29"}, {128, "spot slide"}, {132, "HoldKey 42"}, {134, "ReleaseKey 42"}, {138, "ReleaseKey 29"}, {150, "ReleaseKey 17"},
    {170, "body 00104F02"}, {170, "body 00104F07"}, {170, "body 00104F09"},
};
const ControlScript kBowling{kBowlingScript, {}, {}, 180};
// the game's own trooper in primm, two punches and nothing forced, then he fights
const ScriptLine kTrooperScript[] = {
    {20, "player.moveto 00156FF5"}, {80, "beside -120 0 90 00156FF5"}, {140, "aware 3000"}, {142, "health"}, {148, "face 00156FF5 70"},
    {150, "HoldKey 42"}, {152, "ReleaseKey 42"}, {154, "face 00156FF5 70"}, {156, "HoldKey 42"}, {158, "ReleaseKey 42"},
    {170, "aware 3000"},
    {200, "aware 3000"}, {230, "aware 3000"}, {260, "aware 3000"}, {290, "aware 3000"}, {230, "health"},
    {310, "health"},
};
const ControlScript kTrooper{kTrooperScript, {}, {}, 320};
// a creature of the game's where it lives, mario stands 150 off where it came from, idle
const ScriptLine kWildScript[] = {
    {20, "player.moveto %08X"}, {80, "face %08X 150"}, {82, "actors 1500"}, {88, "health"}, {90, "aware 1500"},
    {120, "aware 1500"}, {150, "aware 1500"}, {180, "aware 1500"}, {210, "aware 1500"}, {240, "aware 1500"},
    {270, "aware 1500"}, {300, "aware 1500"}, {330, "aware 1500"}, {360, "aware 1500"}, {390, "aware 1500"},
    {150, "actors 1500"}, {300, "actors 1500"}, {395, "health"},
};
const ControlScript kWild{kWildScript, {}, {}, 400};
// the same before its face, for one that only notices what it looks at
const ScriptLine kWildFrontScript[] = {
    {20, "player.moveto %08X"}, {80, "front %08X 150"}, {82, "actors 1500"}, {88, "health"}, {90, "aware 1500"},
    {120, "aware 1500"}, {150, "aware 1500"}, {180, "aware 1500"}, {210, "aware 1500"}, {240, "aware 1500"},
    {270, "aware 1500"}, {300, "aware 1500"}, {330, "aware 1500"}, {360, "aware 1500"}, {390, "aware 1500"},
    {150, "actors 1500"}, {300, "actors 1500"}, {395, "health"},
};
const ControlScript kWildFront{kWildFrontScript, {}, {}, 400};
// the same creature dropped on while it bites the courier, again in case it moved off
const ScriptLine kWildStompScript[] = {
    {20, "player.moveto %08X"}, {80, "face %08X 250"}, {82, "actors 1500"}, {140, "actors 1500"}, {162, "over 190 90 %08X"},
    {182, "over 190 90 %08X"}, {202, "over 190 90 %08X"}, {240, "actors 1500"},
};
const ControlScript kWildStomp{kWildStompScript, {}, {}, 260};
struct Wild {
    const char *scenario;
    uint32_t ref;
    const ControlScript &script = kWild;
};
// one of each kind that goes for the courier, none of them left to spawn chance
const Wild kWildlife[] = {
    {"gecko", 0x0016518D}, {"usergecko", 0x0016518D}, {"wild_firegecko", 0x00168CC8}, {"wild_fireant", 0x0015E959},
    {"wild_bloatfly", 0x0015C6F8, kWildFront}, {"wild_coyote", 0x001728AA}, {"wild_nightstalker", 0x00164A73},
    {"wild_cazador", 0x00168CC2}, {"wild_deathclaw", 0x000E62E5}, {"wild_radscorpion", 0x00174BDD}, {"wild_ghoul", 0x00168B36},
    {"coyotestomp", 0x001728AA, kWildStomp},
};

const Wild *wildlife(const std::string &scenario) {
    auto it = std::ranges::find(kWildlife, scenario, &Wild::scenario);
    return it == std::end(kWildlife) ? nullptr : it;
}
// a pound on a friend, then on an essential foe, then with bloody mess owned
const ScriptLine kPoundScript[] = {
    {40, "player.SetAngle Z 90"}, {41, "player.SetGhost 1"}, {42, "place 00104F02"}, {55, "of 00104F02 SetRestrained 1"},
    {58, "body 00104F02"}, {60, "over 200 90 00104F02"}, {63, "HoldKey 29"}, {66, "ReleaseKey 29"}, {100, "body 00104F02"},
    {170, "place 00104F02"}, {185, "of 00104F02 SetRestrained 1"}, {186, "of 00104F02 SetAV Aggression 3"},
    {187, "of 00104F02 SetActorRefEssential 1"}, {190, "over 200 90 00104F02"}, {193, "HoldKey 29"}, {196, "ReleaseKey 29"},
    {220, "body 00104F02"},
    {230, "player.ModAV BloodyMess 1"}, {232, "place 00104F02"}, {245, "of 00104F02 SetRestrained 1"},
    {246, "of 00104F02 SetAV Aggression 3"}, {250, "over 200 90 00104F02"}, {253, "HoldKey 29"}, {256, "ReleaseKey 29"},
    {280, "body 00104F02"},
};
const int kPoundShots[] = {72, 104};
const ControlScript kPound{kPoundScript, {}, kPoundShots, 290};
// two punches and the closing kick on a healthy settler, then on one at death's door
const ScriptLine kFinisherScript[] = {
    {40, "player.SetAngle Z 90"}, {41, "player.SetGhost 1"}, {42, "place 00104F02"}, {55, "of 00104F02 SetRestrained 1"},
    {60, "beside -70 0 90 00104F02"}, {64, "of 00104F02 SetRestrained 0"}, {66, "body 00104F02"},
    {70, "HoldKey 42"}, {72, "ReleaseKey 42"}, {76, "HoldKey 42"}, {78, "ReleaseKey 42"}, {82, "HoldKey 42"}, {84, "ReleaseKey 42"},
    {90, "body 00104F02"}, {95, "body 00104F02"}, {100, "body 00104F02"}, {105, "body 00104F02"}, {130, "body 00104F02"},
    {140, "place 00104F02"}, {150, "of 00104F02 SetRestrained 1"}, {155, "beside -70 0 90 00104F02"}, {158, "of 00104F02 SetRestrained 0"},
    {160, "body 00104F02"},
    {165, "HoldKey 42"}, {167, "ReleaseKey 42"}, {171, "HoldKey 42"}, {173, "ReleaseKey 42"}, {177, "HoldKey 42"}, {179, "ReleaseKey 42"},
    {177, "of 00104F02 ForceAV Health 1"}, {185, "body 00104F02"}, {190, "body 00104F02"}, {195, "body 00104F02"}, {205, "body 00104F02"},
};
const int kFinisherShots[] = {98};
const ControlScript kFinisher{kFinisherScript, {}, kFinisherShots, 240};
// the n key opens mario's options, a pick steps one and saves it and the box comes back
// the cursor rests on the middle of the box, on dust and stars
const ScriptLine kOptionsScript[] = {
    {40, "HoldKey 49"}, {42, "ReleaseKey 49"}, {60, "HoldKey 256"}, {62, "ReleaseKey 256"}, {80, "HoldKey 256"},
    {82, "ReleaseKey 256"},
};
const int kOptionsShots[] = {55, 70, 95};
const ControlScript kOptions{kOptionsScript, {}, kOptionsShots, 110};
// three points of damage, less than a wedge of the courier's health
const ScriptLine kScratchScript[] = {{56, "overlay"}, {60, "player.DamageAV Health 3"}, {75, "overlay"}};
const ControlScript kScratch{kScratchScript, {}, {}, 85};
// the punches and the closing kick twice on one settler, once he is up again
const ScriptLine kStunOnceScript[] = {
    {40, "player.SetAngle Z 90"}, {41, "player.SetGhost 1"}, {42, "place 00104F02"}, {55, "of 00104F02 SetRestrained 1"},
    {60, "beside -70 0 90 00104F02"}, {64, "of 00104F02 SetRestrained 0"}, {68, "face 00104F02 70"}, {70, "HoldKey 42"},
    {72, "ReleaseKey 42"}, {74, "face 00104F02 70"}, {76, "HoldKey 42"}, {78, "ReleaseKey 42"}, {80, "face 00104F02 70"},
    {82, "HoldKey 42"}, {84, "ReleaseKey 42"}, {100, "body 00104F02"},
    {260, "body 00104F02"}, {262, "of 00104F02 SetRestrained 1"}, {265, "beside -70 0 90 00104F02"}, {268, "of 00104F02 SetRestrained 0"},
    {269, "face 00104F02 70"}, {270, "HoldKey 42"}, {272, "ReleaseKey 42"}, {274, "face 00104F02 70"}, {276, "HoldKey 42"},
    {278, "ReleaseKey 42"}, {280, "face 00104F02 70"}, {282, "HoldKey 42"}, {284, "ReleaseKey 42"}, {300, "body 00104F02"},
};
const ControlScript kStunOnce{kStunOnceScript, {}, {}, 310};
// the punches and the closing kick on the quarry's deathclaw, facing it before each
const ScriptLine kBigStunScript[] = {
    {20, "player.moveto 000E62E5"}, {22, "tgm"}, {80, "face 000E62E5 70"}, {82, "HoldKey 42"}, {84, "ReleaseKey 42"},
    {86, "face 000E62E5 70"}, {88, "HoldKey 42"}, {90, "ReleaseKey 42"}, {92, "face 000E62E5 70"}, {94, "HoldKey 42"},
    {96, "ReleaseKey 42"}, {120, "actors 600"},
};
const ControlScript kBigStun{kBigStunScript, {}, {}, 130};
// a courier past the intro's question and the dlc messages, for scenarios to start from
// down twice and a picks travel onward, on any other box a is its only button
const ScriptLine kBaseScript[] = {
    {20, "player.RestoreAV Health 100000"}, {40, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {43, PAD_IDLE},
    {46, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {49, PAD_IDLE}, {52, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {55, PAD_IDLE}, {70, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {73, PAD_IDLE},
    {76, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {79, PAD_IDLE}, {82, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {85, PAD_IDLE}, {100, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {103, PAD_IDLE},
    {106, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {109, PAD_IDLE}, {112, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {115, PAD_IDLE}, {130, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {133, PAD_IDLE},
    {136, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {139, PAD_IDLE}, {142, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {145, PAD_IDLE}, {160, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {163, PAD_IDLE},
    {166, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {169, PAD_IDLE}, {172, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {175, PAD_IDLE}, {190, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {193, PAD_IDLE},
    {196, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {199, PAD_IDLE}, {202, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {205, PAD_IDLE}, {220, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {223, PAD_IDLE},
    {226, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {229, PAD_IDLE}, {232, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {235, PAD_IDLE}, {250, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {253, PAD_IDLE},
    {256, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {259, PAD_IDLE}, {262, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {265, PAD_IDLE}, {280, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {283, PAD_IDLE},
    {286, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {289, PAD_IDLE}, {292, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {295, PAD_IDLE}, {310, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {313, PAD_IDLE},
    {316, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {319, PAD_IDLE}, {322, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {325, PAD_IDLE}, {340, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {343, PAD_IDLE},
    {346, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {349, PAD_IDLE}, {352, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {355, PAD_IDLE}, {370, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {373, PAD_IDLE},
    {376, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {379, PAD_IDLE}, {382, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {385, PAD_IDLE}, {400, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {403, PAD_IDLE},
    {406, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {409, PAD_IDLE}, {412, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {415, PAD_IDLE}, {430, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {433, PAD_IDLE},
    {436, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {439, PAD_IDLE}, {442, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {445, PAD_IDLE}, {460, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {463, PAD_IDLE},
    {466, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {469, PAD_IDLE}, {472, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {475, PAD_IDLE}, {490, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {493, PAD_IDLE},
    {496, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {499, PAD_IDLE}, {502, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {505, PAD_IDLE}, {520, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {523, PAD_IDLE},
    {526, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {529, PAD_IDLE}, {532, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {535, PAD_IDLE}, {550, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {553, PAD_IDLE},
    {556, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {559, PAD_IDLE}, {562, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {565, PAD_IDLE}, {580, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {583, PAD_IDLE},
    {586, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {589, PAD_IDLE}, {592, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {595, PAD_IDLE}, {610, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {613, PAD_IDLE},
    {616, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {619, PAD_IDLE}, {622, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {625, PAD_IDLE}, {640, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {643, PAD_IDLE},
    {646, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {649, PAD_IDLE}, {652, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {655, PAD_IDLE}, {670, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {673, PAD_IDLE},
    {676, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {679, PAD_IDLE}, {682, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {685, PAD_IDLE}, {700, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {703, PAD_IDLE},
    {706, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {709, PAD_IDLE}, {712, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {715, PAD_IDLE}, {730, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {733, PAD_IDLE},
    {736, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {739, PAD_IDLE}, {742, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {745, PAD_IDLE}, {760, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {763, PAD_IDLE},
    {766, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {769, PAD_IDLE}, {772, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {775, PAD_IDLE}, {790, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {793, PAD_IDLE},
    {796, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {799, PAD_IDLE}, {802, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {805, PAD_IDLE}, {820, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {823, PAD_IDLE},
    {826, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {829, PAD_IDLE}, {832, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {835, PAD_IDLE}, {850, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {853, PAD_IDLE},
    {856, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {859, PAD_IDLE}, {862, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {865, PAD_IDLE}, {880, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {883, PAD_IDLE},
    {886, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {889, PAD_IDLE}, {892, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {895, PAD_IDLE}, {910, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {913, PAD_IDLE},
    {916, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=0002"}, {919, PAD_IDLE}, {922, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {925, PAD_IDLE}, {960, "coc TestTraps"}, {1000, "player.RestoreAV Health 100000"},
    {1040, "SaveGame sm64nvbase"},
};
const int kBaseShots[] = {38, 300, 1030, 1075};
const ControlScript kBase{kBaseScript, {}, kBaseShots, 1080};
// the same for the courier of the quicksave the other scenarios load
const ScriptLine kBaseQuickScript[] = {
    {40, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {44, PAD_IDLE}, {70, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {74, PAD_IDLE}, {100, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {104, PAD_IDLE},
    {130, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {134, PAD_IDLE}, {160, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {164, PAD_IDLE}, {190, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {194, PAD_IDLE},
    {220, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {224, PAD_IDLE}, {250, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {254, PAD_IDLE}, {280, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {284, PAD_IDLE},
    {310, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {314, PAD_IDLE}, {340, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {344, PAD_IDLE}, {370, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {374, PAD_IDLE},
    {400, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {404, PAD_IDLE}, {430, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {434, PAD_IDLE}, {460, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {464, PAD_IDLE},
    {490, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {494, PAD_IDLE}, {520, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {524, PAD_IDLE}, {550, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {554, PAD_IDLE},
    {580, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {584, PAD_IDLE}, {610, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {614, PAD_IDLE}, {640, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {644, PAD_IDLE},
    {670, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {674, PAD_IDLE}, {700, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {704, PAD_IDLE}, {730, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {734, PAD_IDLE},
    {760, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {764, PAD_IDLE}, {790, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {794, PAD_IDLE}, {820, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {824, PAD_IDLE},
    {850, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {854, PAD_IDLE}, {880, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {884, PAD_IDLE}, {910, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {914, PAD_IDLE},
    {940, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {944, PAD_IDLE}, {970, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"},
    {974, PAD_IDLE}, {1000, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {1004, PAD_IDLE},
    {1040, "SaveGame quicksave"},
};
const ControlScript kBaseQuick{kBaseQuickScript, {}, kBaseShots, 1080};
// doc mitchell squashed and blown apart, then the save from before brings him back whole
const ScriptLine kReloadScript[] = {
    {40, "SaveGame sm64nvsquash"}, {60, "of 00104C0F SetRestrained 1"}, {62, "body 00104C0F"}, {64, "over 200 90 00104C0F"},
    {80, "body 00104C0F"}, {140, "LoadGame sm64nvsquash"}, {300, "body 00104C0F"},
};
const ControlScript kReload{kReloadScript, {}, {}, 310};
// mario dropped on a friendly settler's head
const ScriptLine kStompScript[] = {
    {40, "player.SetAngle Z 90"}, {41, "player.SetGhost 1"}, {42, "place 00104F02"}, {55, "of 00104F02 SetRestrained 1"},
    {60, "actors 600"}, {65, "over 200 90 00104F02"}, {80, "flat_beside -140 0 90"}, {150, "actors 600"},
};
const int kStompShots[] = {64, 86};
const ControlScript kStomp{kStompScript, {}, kStompShots, 160};
// with friends spared, a stomp on the settler's head
const ScriptLine kSpareScript[] = {
    {40, "player.SetAngle Z 90"}, {42, "place 00104F02"}, {55, "of 00104F02 SetRestrained 1"}, {60, "body 00104F02"},
    {65, "over 200 90 00104F02"}, {108, "body 00104F02"}, {150, "body 00104F02"},
};
const ControlScript kSpare{kSpareScript, {}, {}, 160};
// the saloon: who is there, a doorway with a bare strip, a coyote without bounds
const ScriptLine kSaloonScript[] = {
    {40, "actors 3000"}, {45, "player.SetAngle Z 0"}, {47, "mario -385 40 3456 0"}, {60, "spot south"},
    {62, "HoldKey 17"}, {122, "ReleaseKey 17"}, {130, "spot north"},
    {132, "player.PlaceAtMe 00168D08 1"}, {150, "actors 600"},
};
const ControlScript kSaloon{kSaloonScript, {}, {}, 160};
// the camera stick in the house and again outside after the game has switched looking off
const ScriptLine kPadoutScript[] = {
    {60, "pad lx=0 ly=0 rx=32767 ry=0 lt=0 rt=0 buttons=0000"}, {70, PAD_IDLE},
    {80, "mario 2380 1560 7360 90"}, {90, "HoldKey 18"}, {93, "ReleaseKey 18"},
    {150, "DisablePlayerControls 0 0 0 0 1 0 0"},
    {190, "pad lx=0 ly=32767 rx=32767 ry=0 lt=0 rt=0 buttons=0000"}, {200, PAD_IDLE},
    {215, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=0 buttons=1000"}, {218, PAD_IDLE},
    {235, "pad lx=0 ly=0 rx=0 ry=0 lt=0 rt=255 buttons=4000"}, {238, PAD_IDLE},
    {242, "player.AddScriptPackage 001055BE"},
    {250, "pad lx=0 ly=0 rx=32767 ry=0 lt=0 rt=0 buttons=0000"}, {260, PAD_IDLE},
    {270, PAD_DPAD_DOWN}, {273, PAD_IDLE},
    {276, "pad lx=0 ly=0 rx=32767 ry=0 lt=0 rt=0 buttons=0000"}, {283, PAD_IDLE}, {287, "state"},
};
const Move kPadoutMoves[] = {{"in_before", 50, 55}, {"in_after", 72, 76}, {"out_before", 180, 185}, {"out_after", 202, 206}};
const ControlScript kPadout{kPadoutScript, kPadoutMoves, {}, 295};
// at a prison yard, a run at a wall pillar whose collision is on a layer of no name
const ScriptLine kPrisonScript[] = {
    {45, "cow WastelandNV -8 -8"},
    {200, "player.SetAngle Z 135.8"}, {202, "mario -31880 -29940 6000 135.8"}, {240, "spot before"},
    {242, "HoldKey 17"}, {282, "ReleaseKey 17"}, {292, "spot pillar"},
};
const ControlScript kPrison{kPrisonScript, {}, {}, 300};
// in front of a city gate 480 wide, which is placed by one end 233 away
const ScriptLine kGateScript[] = {
    {45, "cow WastelandNV -3 25"},
    {200, "mario -9668.5 102910.7 4340 220"}, {230, "doors"}, {235, "HoldKey 18"}, {238, "ReleaseKey 18"},
};
const ControlScript kGate{kGateScript, {}, {}, 320};

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
    // counts the places mario has been stood in, a build from an earlier one is dropped
    int gen = 0;
    int ticks = 0, arrive_until = -1;
    // references with a shape when the world was last gathered
    int refs = 0;
    SM64MarioState state{};
    Vec3 run_start{}, run_mid{};
    float floor_z = 0, min_z = 0;
};
Sim g_sim;
SurfaceWindow g_window{{}, 0, 0};
Regather g_regather{kRegatherMove};
DoorPoses g_doors;
Builder g_builder;
Gatherer g_gatherer;
// the regather under way, walked in slices and then built, with the place as it was walked
struct Pending {
    int gen = -1, tick = 0, refs = 0, off = 0, turned = 0;
    Vec3 center{};
    // the longest slice a frame paid and all of them together
    float moved = 0, slice_ms = 0, walk_ms = 0;
    size_t tris = 0, decoded = 0, culled = 0;
    const char *why = "";
    std::vector<DoorPoses::Pose> doors;
};
Pending g_pending;
std::vector<uint32_t> g_owners;
uint32_t g_solid;
uint32_t g_window_loads;
StallWatch g_stall;
ActorBoxes g_boxes;
std::vector<LiveActor> g_near;
ActorStats g_actor_stats;
Swing g_swing;
Thrown g_thrown;
HealthWatch g_health;
uint16_t g_meter;
PowerMeter g_hud;
DeathScene g_death;
// the pictures the overlay holds for the power meter and bowser's head, and whether it draws
int g_meter_picture = -1, g_bowser_picture = -1;
bool g_overlay;
// a blow on an actor that was thrown, dealt once the throw has taken
struct LateBlow {
    uint32_t id;
    int due;
    Attack kind;
    float asked, unarmed, skill;
};
std::vector<LateBlow> g_late;
// a person mario came down on, squashed flat over a second and then killed
struct Squashed {
    uint32_t id;
    int start;
    Skeleton skeleton;
    float head;
};
std::vector<Squashed> g_squashed;
// how high mario got since he last stood on something
float g_fall_peak;
// what a scenario placed of each base form, known once the new one has a body
struct Placing {
    std::vector<uint32_t> before;
    uint32_t ref = 0;
};
std::map<uint32_t, Placing> g_placed;

// a form the scenario never placed is taken as the game's own reference
fnv::TESObjectREFR *placed(uint32_t base) {
    auto it = g_placed.find(base);
    if (it == g_placed.end()) return find_actor(g_sim.cell, base);
    Placing &p = it->second;
    for (fnv::TESObjectREFR *a : p.ref ? std::vector<fnv::TESObjectREFR *>{} : actors_of_base(g_sim.cell, base))
        if (std::ranges::find(p.before, a->form.refID) == p.before.end()) p.ref = a->form.refID, logf("placed base=%08X ref=%08X", base, p.ref);
    return p.ref ? find_actor(g_sim.cell, p.ref) : nullptr;
}
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
    // the longest the frame hook took while he was meant to be moving, and every frame's time
    float max_step = 0, max_ms = 0;
    FrameTimes ms;
};
// a frame hook slower than this costs the game a frame at 60
const float kSlowFrameMs = 12;

struct Control {
    Pad pad{};
    Smooth smooth;
    Vec3 last{}, move_from{};
    float jump_floor = 0, jump_peak = 0, max_gap = 0;
    // how far under his placing the game has let the player sink
    float settle = 0;
    int frames = 0, tick = 0, restore_check = 0;
    double frame_t0 = 0;
    uint32_t action = 0;
    bool started = false, placed = false, blocked = false;
    // frames spent with the player still mario's and no mario, 0 when he has one or is let go
    int carry = 0;
    Press toggle, activate, options;
    // when the camera stick was pushed over and which way the player faced then
    int look_from = 0;
    float look_cam = 0;
    ControlState saved{};
    fnv::TESObjectCELL *cell = nullptr;
    uintptr_t place = 0;
    const ControlScript *script = nullptr;
    // the options box comes back once the one a pick was made in has closed
    bool reopen = false;
    // the creature a shared script is about, its form id fills each %08X
    uint32_t who = 0;
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

std::string ini_path() { return g_dir + "Data\\NVSE\\Plugins\\sm64nv.ini"; }

void open_options() {
    if (show_options(g_config)) logf("options shown tick=%d", g_ctl.tick);
    else logf("refused: options box tick=%d", g_ctl.tick);
}

// a pick steps that option and saves it, the box comes back until done is picked
void choose_option(int pick) {
    const std::vector<Option> &all = game_options();
    if (pick >= (int)all.size()) return logf("options closed tick=%d", g_ctl.tick);
    step_option(all[pick], g_config);
    std::string text = write_config(read_text(ini_path()), g_config);
    FILE *f = fopen(ini_path().c_str(), "wb");
    bool saved = f && fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f) fclose(f);
    logf("options set tick=%d key=%s value=%.2f saved=%d", g_ctl.tick, all[pick].key, all[pick].get(g_config), saved);
    g_ctl.reopen = true;
}

// libsm64's audio code prints on every note
MessageKinds g_lib_messages(64);

void libsm64_print(const char *msg) {
    if (g_lib_messages.first(msg)) logf("libsm64 %s", msg);
}

void on_post_load() {
    g_config = parse_config(read_text(ini_path()));
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
    std::vector<uint8_t> meter = power_meter_art(g_rom), bowser = bowser_wipe_art(g_rom);
    if (meter.empty() || bowser.empty()) return logf("refused: rom holds no power meter or bowser where they should be");
    g_meter_picture = overlay_picture(std::move(meter), kHudPicture, kHudPicture * kMeterPictures);
    g_bowser_picture = overlay_picture(std::move(bowser), kHudPicture, kHudPicture);
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

// a save to start from and a cell to go to: the cell once the save has loaded
bool g_cell_after_load, g_game_loaded;
// the save's own cell, which does not count as arrived in
fnv::TESObjectCELL *g_save_cell;

fnv::TESObjectCELL *settle_cell() {
    if (g_frames == kMenuFrames && !g_config.load.empty()) run_console("LoadGame " + g_config.load), g_cell_after_load = !g_config.cell.empty();
    else if (g_frames == kMenuFrames && !g_config.cell.empty()) run_console("coc " + g_config.cell);
    if (g_frames <= kMenuFrames) return nullptr;
    if (g_cell_after_load) {
        if (g_game_loaded) g_save_cell = loaded_cell(), run_console("coc " + g_config.cell), g_cell_after_load = false;
        return nullptr;
    }
    if (g_save_cell && loaded_cell() == g_save_cell) return g_settled = 0, nullptr;
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
    double t0 = seconds_now();
    load_window(g_window);
    g_window_loads++;
    if (float ms = (seconds_now() - t0) * 1000; ms >= 1) logf("collision window load tick=%d ms=%.1f loaded=%u", g_sim.ticks, ms, (unsigned)g_window.loaded().size());
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

Gather to_build(std::vector<Tri> tris) {
    return {g_sim.frame, std::move(tris), kWindowRadius * g_config.scale, kFaceReach * g_config.scale};
}

// the built surfaces become the world, with the place as it was when they were walked
void install(Built &b, Vec3 center, int refs, std::vector<DoorPoses::Pose> doors) {
    g_owners = std::move(b.owners), g_solid = std::ranges::count(b.fixed, true);
    g_window = std::move(b.window);
    g_regather.loaded(center);
    g_sim.refs = refs;
    g_doors.loaded(std::move(doors));
}

// collision around center, walked and built this frame, a refusal keeps what was there
// the first gather in a place logs what it found, a take by hand also writes it out
bool gather_world(Vec3 center, bool first, bool dump = false) {
    CollisionStats st;
    std::vector<Tri> tris = gather_collision(g_sim.cell, center, kCollisionRadius, st);
    if (dump) write_obj((g_dir + "sm64nv_collision.obj").c_str(), tris);
    std::string why = distrust(st);
    if (!why.empty()) {
        logf("refused: %s", why.c_str());
        g_regather.refused(g_sim.ticks);
        return false;
    }
    size_t count = tris.size();
    Built b = build_world(to_build(std::move(tris)));
    if (first) log_collision(st, count, b.stats);
    g_sim.gen++;
    install(b, center, st.refs, door_poses(g_sim.cell));
    return true;
}

// ms is the most one frame paid for this regather
void log_regather(bool ok, float ms, const Built *b) {
    const Pending &p = g_pending;
    logf("collision regather tick=%d moved=%.1f ms=%.1f ok=%d tris=%u off=%d turned=%d why=%s walk=%.1f build=%.1f window=%.1f "
         "decoded=%u culled=%u wait=%d steps=%d",
         g_sim.ticks, p.moved, ms, ok, (unsigned)p.tris, p.off, p.turned, p.why, p.walk_ms, b ? b->build_ms : 0.0f,
         b ? b->window_ms : 0.0f, (unsigned)p.decoded, (unsigned)p.culled, g_sim.ticks - p.tick, g_gatherer.steps);
}

void start_regather(Vec3 center, const char *why) {
    g_pending = {g_sim.gen, g_sim.ticks, 0, 0, 0, center, g_regather.moved(center), 0, 0, 0, 0, 0, why, {}};
    g_gatherer.begin(g_sim.cell, center, kCollisionRadius);
}

// one slice of the walk, the surfaces go to the builder's thread once it is whole
void walk_regather() {
    double t0 = seconds_now();
    bool whole = g_gatherer.step(kWalkBudget);
    float ms = (seconds_now() - t0) * 1000;
    g_pending.walk_ms += ms, g_pending.slice_ms = std::fmax(g_pending.slice_ms, ms);
    if (!whole) return;
    const CollisionStats &st = g_gatherer.stats;
    std::vector<Tri> tris = g_gatherer.take();
    g_pending.refs = st.refs, g_pending.off = st.scale.off, g_pending.turned = st.turn.off;
    g_pending.tris = tris.size(), g_pending.decoded = st.decoded, g_pending.culled = st.culled;
    g_pending.doors = door_poses(g_sim.cell);
    std::string bad = distrust(st);
    if (!bad.empty()) {
        logf("refused: %s", bad.c_str());
        g_regather.refused(g_sim.ticks);
        return log_regather(false, g_pending.slice_ms, nullptr);
    }
    g_builder.start(to_build(std::move(tris)));
}

void finish_regather(Built &b) {
    if (g_pending.gen != g_sim.gen) return logf("collision dropped tick=%d gen=%d now=%d", g_sim.ticks, g_pending.gen, g_sim.gen);
    double t0 = seconds_now();
    install(b, g_pending.center, g_pending.refs, std::move(g_pending.doors));
    log_regather(true, std::fmax(g_pending.slice_ms, float((seconds_now() - t0) * 1000)), &b);
}

void regather_when_far() {
    if (g_gatherer.walking() && g_pending.gen != g_sim.gen) g_gatherer = {};
    if (g_gatherer.walking()) walk_regather();
    if (std::optional<Built> b = g_builder.take()) finish_regather(*b);
    if (g_gatherer.walking() || g_builder.busy()) return;
    Vec3 m = to_game(g_sim.frame, g_ticks.cur_pos);
    // a place just arrived in is gathered again when more of it has loaded and once at the end
    int left = g_sim.arrive_until - g_sim.ticks;
    bool arriving = left >= 0 && left % kDoorLookTicks == 0 && (!left || loaded_refs(g_sim.cell) != g_sim.refs);
    // what was gathered has a door where it stood then, however it was opened
    bool swung = g_sim.ticks % kDoorLookTicks == 0 && g_doors.changed(door_poses(g_sim.cell));
    const char *why = g_regather.due(m, g_sim.ticks) ? "far" : arriving ? "arrive" : swung ? "door" : nullptr;
    if (why) start_regather(m, why);
}

bool start_mario(fnv::TESObjectCELL *cell, float ahead, bool dump = false) {
    fnv::TESObjectREFR *p = fnv::player();
    float h = p->rot[2];
    Vec3 at{p->pos[0] + std::sin(h) * ahead, p->pos[1] + std::cos(h) * ahead, p->pos[2]};
    g_sim.frame = {at, g_config.scale};
    g_sim.cell = cell;
    if (!gather_world(at, true, dump)) return false;
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
// mario passes through the downed and through those fighting him, so bites reach him
bool solid_to_mario(const LiveActor &a) {
    return !knocked(a.ref) && !g_thrown.resting(a.body.id, g_ctl.tick) && combat_target(a.ref) != fnv::player();
}

void sync_actors() {
    g_actor_stats = {};
    g_near = nearby_actors(g_sim.cell, to_game(g_sim.frame, g_ticks.cur_pos), kActorReach, g_actor_stats);
    // one being squashed is flat, so he is not in mario's way nor hit again
    std::erase_if(g_near, [](const LiveActor &a) { return std::ranges::any_of(g_squashed, [&](const Squashed &q) { return q.id == a.body.id; }); });
    std::vector<ActorBody> bodies;
    for (const LiveActor &a : g_near)
        if (solid_to_mario(a)) bodies.push_back(a.body);
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
    g_health = {}, g_meter = 0, g_hud = {}, g_death = {};
    for (const Squashed &q : g_squashed) logf("squash undone ref=%08X ok=%d", q.id, end_squash(find_actor(g_sim.cell, q.id), q.skeleton));
    g_squashed.clear(), g_fall_peak = 0, g_late.clear(), g_placed.clear();
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
    pipboy_arms_reset();
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

// drops mario from so high over the actor a base form was last placed as, facing a heading
void teleport_over(const char *args) {
    float dz, deg;
    unsigned base;
    if (sscanf(args, "%f %f %x", &dz, &deg, &base) != 3) return finish(false, "over");
    fnv::TESObjectREFR *of = placed(base);
    if (!of) return finish(false, "over_none");
    Vec3 a{of->pos[0], of->pos[1], of->pos[2]};
    char to[96];
    snprintf(to, sizeof to, "%.1f %.1f %.1f %.1f", a.x, a.y, a.z + dz, deg);
    teleport_mario(to);
    sm64_set_mario_action(g_sim.id, kActFreefall);
}

// stands mario beside the first one he squashed, to look at him
void teleport_beside_squashed(const char *args) {
    float dx, dy, deg;
    ActorStats st;
    std::vector<LiveActor> all = nearby_actors(g_sim.cell, mario_pos(), kActorReach, st);
    auto it = g_squashed.empty() ? all.end()
                                 : std::ranges::find_if(all, [](const LiveActor &a) { return a.body.id == g_squashed[0].id; });
    if (it == all.end() || sscanf(args, "%f %f %f", &dx, &dy, &deg) != 3) return finish(false, "flat_beside");
    char to[96];
    snprintf(to, sizeof to, "%.1f %.1f %.1f %.1f", it->body.feet.x + dx, it->body.feet.y + dy, it->body.feet.z + 1, deg);
    teleport_mario(to);
}

// stands mario beside the nearest actor or the one last placed of a base form
void teleport_beside(const char *args) {
    float dx, dy, deg;
    unsigned base = 0;
    int n = sscanf(args, "%f %f %f %x", &dx, &dy, &deg, &base);
    fnv::TESObjectREFR *of = n == 4 ? placed(base) : g_near.empty() ? nullptr : g_near[0].ref;
    if (n < 3 || !of) return finish(false, "beside");
    char to[96];
    snprintf(to, sizeof to, "%.1f %.1f %.1f %.1f", of->pos[0] + dx, of->pos[1] + dy, of->pos[2] + 1, deg);
    teleport_mario(to);
}

// mario this far from an actor that walks about, on his side of it and facing it
void teleport_facing(const char *args) {
    unsigned id;
    float apart;
    fnv::TESObjectREFR *of = sscanf(args, "%x %f", &id, &apart) == 2 ? placed(id) : nullptr;
    if (!of) return finish(false, "face");
    Vec3 m = mario_pos();
    float dx = of->pos[0] - m.x, dy = of->pos[1] - m.y, len = std::fmax(std::hypot(dx, dy), 1.0f);
    char to[96];
    snprintf(to, sizeof to, "%.1f %.1f %.1f %.1f", of->pos[0] - dx / len * apart, of->pos[1] - dy / len * apart, of->pos[2] + 1,
             std::atan2(dx, dy) * 180 / 3.14159265f);
    teleport_mario(to);
}

// mario this far ahead of an actor's face, looking at it, on ground it stands on
void teleport_front(const char *args) {
    unsigned id;
    float apart;
    fnv::TESObjectREFR *of = sscanf(args, "%x %f", &id, &apart) == 2 ? placed(id) : nullptr;
    if (!of) return finish(false, "front");
    float h = of->rot[2];
    char to[96];
    snprintf(to, sizeof to, "%.1f %.1f %.1f %.1f", of->pos[0] + std::sin(h) * apart, of->pos[1] + std::cos(h) * apart, of->pos[2] + 1,
             h * 180 / 3.14159265f + 180);
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

const char *kAttackNames[] = {"none", "punch", "kick", "dive", "pound", "finisher", "slide"};

// the blow mario is throwing lands on whoever it reaches, once each
// a thrown actor is struck this many ticks later, a death in the same frame eats the throw
const int kStrikeAfterThrow = 3;


void deal(int t, const LiveActor &a, Attack kind, float asked, float unarmed, float skill) {
    float before = actor_health(a.ref), damage = strike(a.ref, asked);
    logf("attack hit tick=%d kind=%s ref=%08X damage=%.1f health=%.1f>%.1f unarmed=%.0f scale=%.2f asked=%.2f", t, kAttackNames[(int)kind],
         a.body.id, damage, before, actor_health(a.ref), unarmed, skill, asked);
}

void deal_late(int t) {
    std::erase_if(g_late, [&](const LateBlow &b) {
        if (t < b.due) return false;
        auto it = std::ranges::find_if(g_near, [&](const LiveActor &a) { return a.body.id == b.id; });
        if (it == g_near.end()) logf("attack lost tick=%d ref=%08X kind=%s", t, b.id, kAttackNames[(int)b.kind]);
        else deal(t, *it, b.kind, b.asked, b.unarmed, b.skill);
        return true;
    });
}

void land_blows(int t) {
    deal_late(t);
    Attack now = attack_now(g_sim.state.action, g_sim.state.flags);
    g_swing.tick(now);
    if (now == Attack::none) return;
    AttackProfile p = attack_profile(now);
    Vec3 feet = mario_pos();
    float heading = heading_from_sm64_yaw(g_sim.state.faceAngle), scale = g_sim.frame.scale;
    float unarmed = actor_unarmed(fnv::player()), skill = unarmed_scale(unarmed), asked = p.damage * g_config.punch * skill;
    for (const LiveActor &a : g_near) {
        if (!attack_reaches(p, feet, LastFit::kHeight / scale, heading, a.body) || !g_swing.lands(a.body.id)) continue;
        // the game throws only the living, so the blow lands after the throw and a corpse flies on
        ThrowTarget meets{a.body.id, a.body.height, 2 * std::fmax(a.body.half_width, a.body.half_length), asked >= actor_health(a.ref)};
        bool thrown = p.push > 0 && g_thrown.allow(meets, t);
        if (thrown) logf("attack push tick=%d ref=%08X force=%.1f ok=%d", t, a.body.id, p.push, shove(a.ref, {feet.x, feet.y, feet.z - p.lift}, p.push));
        if (thrown) g_late.push_back({a.body.id, t + kStrikeAfterThrow, now, asked, unarmed, skill});
        else deal(t, a, now, asked, unarmed, skill);
        // libsm64 gives mario his own recoil and the sound of the hit
        Vec3 n = nearest_on(a.body, feet);
        Vec3 s = to_sm64(g_sim.frame, {n.x, n.y, a.body.feet.z});
        bool met = sm64_mario_attack(g_sim.id, s.x, s.y, s.z, a.body.height * scale);
        // sm64 bursts shards off what a fist or a foot meets, the flag comes too late for this tick
        if (met && (now == Attack::punch || now == Attack::kick || now == Attack::finisher || now == Attack::slide)) {
            g_puffs.hits++;
            if (g_config.particles) g_particles.emit(kPuffHit, g_ticks.cur_pos, g_sim.state.faceAngle);
        }
    }
}

const int32_t kSoundAttacked = 0x240AFF81, kSoundPowerMeter = 0x700D0081, kSoundBowserLaugh = 0x70188081;
const uint32_t kActStandingDeath = 0x00021311, kActDeathOnBack = 0x00021316;
// a blow this far past death throws him on his back
const float kOverkill = -20;

void log_health(const char *what) {
    fnv::TESObjectREFR *p = fnv::player();
    // the meter is read back from libsm64 as it stood after the last tick
    logf("hurt %s tick=%d health=%.1f max=%.1f meter=%03X standing=%d", what, g_ctl.tick, actor_health(p), actor_max_health(p),
         g_sim.state.health, actor_standing(p));
}

// the courier's health is mario's meter, a wound is heard and his death lets the courier go
void watch_health(int t) {
    fnv::TESObjectREFR *p = fnv::player();
    float health = actor_health(p), max = actor_max_health(p);
    bool first = !g_health.primed();
    HealthChange c = g_health.tick(health, actor_standing(p));
    uint16_t meter = health_meter(health, max);
    if (meter != g_meter) sm64_set_mario_health(g_sim.id, meter), g_meter = meter;
    if (first) log_health("baseline");
    if (c.lost > 0) {
        logf("hurt tick=%d lost=%.1f health=%.1f max=%.1f meter=%03X", t, c.lost, health, max, meter);
        sm64_play_sound_global(kSoundAttacked);
    }
    if (c.died) log_health("dead");
    // mario dies as in sm64, the game's own reload of the last save lets the courier go
    DeathCue cue = g_death.tick(!g_health.dead());
    if (cue == DeathCue::fall) {
        uint32_t act = health <= kOverkill ? kActDeathOnBack : kActStandingDeath;
        sm64_set_mario_action(g_sim.id, act);
        g_death.warp_at(death_warp_tick(act));
        logf("death tick=%d cue=fall action=%08X health=%.1f", t, act, health);
    } else if (cue == DeathCue::laugh) {
        sm64_play_sound_global(kSoundBowserLaugh);
        logf("death tick=%d cue=laugh", t);
    }
    // sm64 sounds its meter when wedges come back, a dead man's is empty
    if (g_hud.tick(g_death.dying() ? 0 : g_meter >> 8, c.lost > 0)) sm64_play_sound_global(kSoundPowerMeter);
}

// what the overlay draws: the power meter while it shows and not in a menu, then the wipe
void show_hud() {
    int w, h;
    overlay_size(w, h);
    std::vector<OverlayQuad> quads;
    bool wiping = taken() && g_death.wipe_frame() >= 0 && h > 0;
    if (g_config.meter && taken() && !g_ctl.blocked && !wiping && g_hud.shown() && h > 0) quads.push_back({meter_quad(g_hud.y(), g_hud.wedges(), w, h), g_meter_picture});
    // bowser's wipe takes the place of the meter and covers every menu
    if (wiping)
        for (const HudQuad &q : wipe_quads(g_death.wipe_frame(), w, h)) quads.push_back({q, q.textured ? g_bowser_picture : -1});
    overlay_set(std::move(quads));
}

const int32_t kSoundStomped = 0x50308081, kSoundSquashed = 0x5060B081;
const uint32_t kBloodyMess = 0x37;
const uint32_t kActFlagAir = 0x800;
// ticks a squashed person lies flat before he dies
const int kSquashTicks = 30;

void restrain(fnv::TESObjectREFR *actor) { g_console->runScriptLine("SetRestrained 1", actor); }

// the highest mario gets in the ticks after a stomp, a low ceiling can cut the bounce short
const int kBounceLook = 3;
struct BounceLook {
    int at = -1;
    float from = 0, peak = 0;
} g_bounce;

void look_at_bounce(int t) {
    if (g_bounce.at < 0) return;
    g_bounce.peak = std::fmax(g_bounce.peak, mario_pos().z);
    if (t != g_bounce.at) return;
    logf("stomp rise tick=%d from=%.1f peak=%.1f", t, g_bounce.from, g_bounce.peak);
    g_bounce = {};
}

// only an attacker with bloody mess blows a body apart, so the player gets it briefly
void blow_apart(int t, const LiveActor &a) {
    fnv::TESObjectREFR *p = fnv::player();
    float mess = current_value(p, kBloodyMess), before = actor_health(a.ref);
    if (mess < 1) run_console("player.ModAV BloodyMess 1");
    float during = current_value(p, kBloodyMess), dealt = strike_explode(a.ref);
    if (mess < 1) run_console("player.ModAV BloodyMess -1");
    logf("gib tick=%d ref=%08X damage=%.1f health=%.1f>%.1f mess=%.0f>%.0f>%.0f gone=%04X", t, a.body.id, dealt, before, actor_health(a.ref),
         mess, during, current_value(p, kBloodyMess), limbs_gone(a.ref));
    sm64_play_sound_global(kSoundSquashed);
}

// mario bounces off a head he falls on, a person is squashed and a creature takes a blow
void land_stomps(int t, Stomp move, Vec3 was) {
    Vec3 now = mario_pos();
    float peak = std::fmax(g_fall_peak, was.z);
    g_fall_peak = g_sim.state.action & kActFlagAir ? std::fmax(peak, now.z) : now.z;
    if (move == Stomp::none) return;
    auto hit = std::ranges::find_if(g_near, [&](const LiveActor &a) { return stomps(peak, was, now, a.body, g_sim.frame.scale); });
    if (hit == g_near.end()) return;
    fnv::TESObjectREFR *a = hit->ref;
    int disposition;
    // a creature biting the courier may still not count as one that should attack him
    bool hostile = hostile_to_player(a, disposition) || combat_target(a) == fnv::player();
    float head = head_height(a);
    // the landing is all this blow does to its target, the pound's shock skips him
    g_swing.lands(hit->body.id);
    bool essential = actor_essential(a);
    StompOutcome out = stomp_outcome({hostile, skeleton_of(a).root && head > 0, hit->body.height, essential}, move, g_config.stomp);
    static const char *kOutcomes[] = {"stand", "hit", "squash", "gib"};
    fnv::TESObjectREFR *target = combat_target(a);
    logf("stomp tick=%d ref=%08X kind=%s peak=%.1f at=%s hostile=%d disposition=%d essential=%d target=%08X", t, hit->body.id,
         kOutcomes[(int)out], peak - hit->body.feet.z, xyz(now).c_str(), hostile, disposition, essential, target ? target->form.refID : 0);
    // one he spares he lands on and stands on
    if (out == StompOutcome::stand) return;
    g_bounce = {t + kBounceLook, now.z, now.z};
    // a pound that blows its target apart carries on down, anything else bounces him off
    if (out == StompOutcome::gib) return blow_apart(t, *hit);
    stomp_bounce(g_sim.id, g_sim.state);
    sm64_play_sound_global(kSoundStomped);
    if (out == StompOutcome::hit) {
        float before = actor_health(a), dealt = strike(a, attack_profile(Attack::pound).damage * g_config.punch * unarmed_scale(actor_unarmed(fnv::player())));
        logf("attack hit tick=%d kind=stomp ref=%08X damage=%.1f health=%.1f>%.1f", t, hit->body.id, dealt, before, actor_health(a));
    }
    if (out != StompOutcome::squash) return;
    restrain(a);
    Skeleton bones = skeleton_of(a);
    bool hung = begin_squash(bones);
    g_squashed.push_back({hit->body.id, t, bones, head});
    logf("squash start tick=%d ref=%08X skeleton=%d head=%.1f", t, hit->body.id, hung, head);
}

// the squashed flatten like sm64's goombas, then burst apart as the player's kill
void tick_squashed(int t) {
    if (g_squashed.empty()) return;
    ActorStats st;
    std::vector<LiveActor> all = nearby_actors(g_sim.cell, mario_pos(), kActorReach, st);
    std::erase_if(g_squashed, [&](const Squashed &q) {
        auto it = std::ranges::find_if(all, [&](const LiveActor &a) { return a.body.id == q.id; });
        if (it == all.end()) return logf("squash lost tick=%d ref=%08X undone=%d", t, q.id, end_squash(find_actor(g_sim.cell, q.id), q.skeleton)), true;
        Squash flat = squash_at(t - q.start);
        squash_skeleton(q.skeleton, flat.height, flat.width);
        if (t - q.start < kSquashTicks) return false;
        logf("squash done tick=%d ref=%08X head=%.1f>%.1f", t, q.id, q.head, head_height(it->ref));
        end_squash(it->ref, q.skeleton);
        blow_apart(t, *it);
        return true;
    });
}

void log_overlay() {
    OverlayStats st = overlay_take_stats();
    logf("overlay tick=%d hooked=%d presents=%u drawn=%u captured=%u size=%dx%d meter_shown=%d meter_y=%d wedges=%d", g_ctl.tick, g_overlay,
         st.presents, st.drawn, st.captured, st.width, st.height, g_hud.shown(), g_hud.y(), g_hud.wedges());
}

void log_actors(float reach) {
    ActorStats st;
    Vec3 m = mario_pos();
    std::vector<LiveActor> found = nearby_actors(g_ctl.cell, m, reach, st);
    logf("control actors tick=%d near=%u seen=%d far=%d down=%d unsized=%d boxes=%u", g_ctl.tick, (unsigned)found.size(), st.seen,
         st.away, st.down, st.unsized, (unsigned)g_boxes.size());
    for (const LiveActor &a : found)
        logf("actor ref=%08X base=%08X type=%02X pos=%s heading=%.0f half=%.1f,%.1f height=%.1f dist=%.1f health=%.1f knocked=%d sized=%s solid=%d target=%08X", a.body.id,
             a.ref->baseForm->refID, a.ref->baseForm->typeID, xyz(a.body.feet).c_str(), a.body.heading * 180 / 3.14159265f,
             a.body.half_width, a.body.half_length, a.body.height,
             std::hypot(a.body.feet.x - m.x, a.body.feet.y - m.y, a.body.feet.z - m.z), actor_health(a.ref), knocked(a.ref), a.sized,
             solid_to_mario(a), combat_target(a.ref) ? combat_target(a.ref)->form.refID : 0);
}

void log_awareness(float reach) {
    ActorStats st;
    Vec3 m = mario_pos();
    for (const LiveActor &a : nearby_actors(g_ctl.cell, m, reach, st)) {
        Awareness w = awareness_of_player(a.ref);
        int disposition = 0;
        bool hostile = hostile_to_player(a.ref, disposition);
        logf("aware tick=%d ref=%08X base=%08X dist=%.0f hostile=%d disposition=%d sees=%d ray=%d detect=%d seen=%d lost=%d target=%08X",
             g_ctl.tick, a.body.id, a.ref->baseForm->refID, std::hypot(a.body.feet.x - m.x, a.body.feet.y - m.y), hostile, disposition,
             w.sees, w.ray, w.detect, w.seen, w.lost, w.target ? w.target->form.refID : 0);
    }
}

// where the body last placed of a base form is drawn, not where its reference is
void log_body(const char *args) {
    fnv::TESObjectREFR *a = placed(strtoul(args, nullptr, 16));
    Vec3 p{};
    if (!a || !pelvis_at(a, p)) return finish(false, "body");
    logf("actor body tick=%d ref=%08X pelvis=%s health=%.1f knocked=%d standing=%d gone=%04X head=%.1f", g_ctl.tick, a->form.refID,
         xyz(p).c_str(), actor_health(a), knocked(a), actor_standing(a), limbs_gone(a), head_height(a));
}

// places an actor of a base form at the player, which one it became shows once it has a body
void place_actor(const char *args) {
    uint32_t base = strtoul(args, nullptr, 16);
    Placing p;
    for (fnv::TESObjectREFR *a : actors_of_base(g_sim.cell, base)) p.before.push_back(a->form.refID);
    g_placed[base] = p;
    char line[64];
    snprintf(line, sizeof line, "player.PlaceAtMe %08X 1", base);
    run_console(line);
}

// stands the actor a base form was last placed as so far east and north of mario
void set_placed(const char *args) {
    unsigned base;
    float dx, dy;
    fnv::TESObjectREFR *a = sscanf(args, "%x %f %f", &base, &dx, &dy) == 3 ? placed(base) : nullptr;
    if (!a) return finish(false, "set");
    Vec3 m = mario_pos();
    char line[64];
    snprintf(line, sizeof line, "SetPos X %.1f", m.x + dx);
    g_console->runScriptLine(line, a);
    snprintf(line, sizeof line, "SetPos Y %.1f", m.y + dy);
    g_console->runScriptLine(line, a);
    snprintf(line, sizeof line, "SetPos Z %.1f", m.z);
    g_console->runScriptLine(line, a);
    logf("set ref=%08X at=%.1f,%.1f,%.1f", a->form.refID, m.x + dx, m.y + dy, m.z);
}

// a console line run on the actor a base form was last placed as
void run_on_placed(const char *args) {
    char *line = nullptr;
    fnv::TESObjectREFR *a = placed(strtoul(args, &line, 16));
    if (!a || !line || *line != ' ') return finish(false, "no_actor_of_base");
    unsigned ret = g_console->runScriptLine(line + 1, a);
    logf("console actor=%08X line=%s ok=%d", a->form.refID, line + 1, ret != 0);
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
        logf("door ref=%08X base=%08X pos=%s heading=%.0f teleports=%d pose=%016llX", d.ref->form.refID, d.ref->baseForm->refID, xyz(d.pos).c_str(),
             d.heading * 180 / 3.14159265f, d.teleports, (unsigned long long)node_pose(d.ref->renderState->niNode));
}

// the game refuses its own activate key while the player's movement is off
// the game opens a door of the cell by form id, the way a menu or a script does
void open_door(const char *id) {
    uint32_t want = strtoul(id, nullptr, 16);
    for (const Door &d : cell_doors(g_ctl.cell))
        if (d.ref->form.refID == want) logf("control opened ref=%08X ok=%d", want, activate(d.ref));
}

void use_door() {
    std::vector<Door> doors = cell_doors(g_ctl.cell);
    std::vector<Vec3> at;
    Vec3 m = mario_pos();
    for (const Door &d : doors) at.push_back(nearest_in_box(m, d.pos, d.heading, d.lo, d.hi));
    int i = nearest_within(at, m, kDoorReach);
    if (i < 0) return logf("control door tick=%d ref=none doors=%u", g_ctl.tick, (unsigned)doors.size());
    const Door &d = doors[i];
    logf("control door tick=%d ref=%08X teleports=%d dist=%.1f", g_ctl.tick, d.ref->form.refID, d.teleports,
         std::hypot(at[i].x - m.x, at[i].y - m.y, at[i].z - m.z));
    logf("control activated ref=%08X ok=%d", d.ref->form.refID, activate(d.ref));
}

// each push of the camera stick with how far the view turned
void watch_look(int t, float cam) {
    bool pushed = std::fabs(look_stick()) > 0.5f;
    if (pushed && !g_ctl.look_from) g_ctl.look_from = t, g_ctl.look_cam = cam;
    if (pushed || !g_ctl.look_from) return;
    ControlState cs = control_state();
    logf("control look tick=%d ticks=%d turned=%.3f taken=%d blocked=%d controls=%02X third=%d %s", t, t - g_ctl.look_from,
         std::remainder(cam - g_ctl.look_cam, 6.2831853f), taken(), g_ctl.blocked, cs.controls, cs.third, pad_mode().c_str());
    g_ctl.look_from = 0;
}

// after a save and a cell the arrival waits on frames, so the script counts from the take
bool g_took;

void control_tick() {
    const ControlScript &s = *g_ctl.script;
    float cam = fnv::player()->rot[2];
    g_took = g_took || taken();
    if (g_save_cell && g_config.autotake && !g_took) return;
    int t = ++g_ctl.tick;
    if (taken()) {
        // menus and the console still see the keys, mario must not
        Pad pad = g_ctl.blocked || g_death.dying() ? Pad{} : g_ctl.pad;
        // the tick he lands on has him on his feet already, so the fall is judged before it
        Stomp move = stomp_move(g_sim.state.action, g_sim.state.velocity[1]);
        Vec3 was = mario_pos();
        tick_mario(make_inputs(cam, pad.right, pad.forward, pad.buttons));
        land_stomps(t, move, was);
        look_at_bounce(t);
        tick_squashed(t);
        land_blows(t);
        g_trail.add(t, mario_pos(), g_sim.state.action, pad.forward, pad.right, pad.buttons);
        keep_fit(t);
        if (g_stall.feed(g_ticks.cur_pos, std::hypot(pad.right, pad.forward))) log_stall(t), log_trail();
        watch_health(t);
    }
    watch_look(t, cam);
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
        logf("control smooth name=%s frames=%d hitches=%d max_step=%.2f max_ms=%.1f p95_ms=%.1f slow=%d", mv.name, sm.frames, sm.hitches,
             sm.max_step, sm.max_ms, sm.ms.quantile(0.95f), sm.ms.over(kSlowFrameMs));
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
        char filled[128];
        const char *text = line.line;
        if (strchr(text, '%')) snprintf(filled, sizeof filled, text, g_ctl.who), text = filled;
        ControlState cs = control_state();
        logf("control focus tick=%d foreground=%d active=%d", t, cs.foreground, cs.active);
        if (!cs.foreground) return finish(false, "no_focus");
        // pad lines are for the virtual gamepad that follows this log
        if (!strncmp(text, "pad ", 4)) logf("control pad tick=%d %s", t, text + 4);
        else if (!strncmp(text, "mario ", 6)) teleport_mario(text + 6);
        else if (!strncmp(text, "beside ", 7)) teleport_beside(text + 7);
        else if (!strncmp(text, "over ", 5)) teleport_over(text + 5);
        else if (!strncmp(text, "face ", 5)) teleport_facing(text + 5);
        else if (!strncmp(text, "front ", 6)) teleport_front(text + 6);
        else if (!strncmp(text, "flat_beside ", 12)) teleport_beside_squashed(text + 12);
        else if (!strcmp(text, "sound pause")) sound_pause();
        else if (!strcmp(text, "sound status")) log_sound_status();
        else if (!strcmp(text, "doors")) log_doors();
        else if (!strncmp(text, "open ", 5)) open_door(text + 5);
        else if (!strcmp(text, "puffs")) log_puffs();
        else if (!strcmp(text, "health")) log_health("status");
        else if (!strcmp(text, "overlay")) log_overlay();
        else if (!strcmp(text, "hudshot")) {
            char name[64];
            snprintf(name, sizeof name, "ScreenShotSM64_%03d.bmp", t);
            overlay_capture(g_dir + name);
        }
        else if (!strncmp(text, "body ", 5)) log_body(text + 5);
        else if (!strncmp(text, "aware ", 6)) log_awareness((float)atof(text + 6));
        else if (!strncmp(text, "actors ", 7)) log_actors((float)atof(text + 7));
        else if (!strncmp(text, "actor ", 6)) run_on_nearest(text + 6);
        else if (!strncmp(text, "of ", 3)) run_on_placed(text + 3);
        else if (!strncmp(text, "place ", 6)) place_actor(text + 6);
        else if (!strncmp(text, "set ", 4)) set_placed(text + 4);
        else if (!strcmp(text, "state")) {
            pipboy_arms_log();
            logf("control state tick=%d %s", t, describe(control_state()).c_str());
            logf("mesh chain tick=%d %s", t, mario_mesh_chain().c_str());
            logf("body chain tick=%d %s", t, node_chain(fnv::player()->renderState->niNode).c_str());
            if (!log_camera(m, cam)) return finish(false, "camera");
        }
        else if (!strncmp(text, "spot ", 5))
            logf("control spot name=%s pos=%s action=%08X", text + 5, xyz(m).c_str(), g_sim.state.action);
        else run_console(text);
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
    float ms = float((seconds_now() - g_ctl.frame_t0) * 1000);
    sm.max_ms = std::fmax(sm.max_ms, ms), sm.ms.add(ms);
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
    g_ctl.frame_t0 = seconds_now();
    if (taken()) follow_cell();
    if (g_ctl.placed) g_ctl.max_gap = std::fmax(g_ctl.max_gap, player_gap()), g_ctl.frames++;
    // the game lowers a player without collision, slowly and never far
    // he is placed higher by what he was found low the frame before
    if (g_ctl.placed) g_ctl.settle = std::clamp(g_ctl.settle + g_ctl.last.z - player_pos().z, -kSettleMost, kSettleMost);
    pipboy_arms_update(held() && g_config.pipboy_arms);
    keep_pad_hooked();
    keep_calls_hooked();
    Pad pad;
    bool toggle, activate, options;
    if (!read_game_pad(pad, toggle, activate, options)) return finish(false, "input_globals");
    if (pad != g_ctl.pad)
        logf("control input tick=%d forward=%.2f right=%.2f a=%d b=%d z=%d", g_ctl.tick, pad.forward, pad.right,
             pad.buttons.a, pad.buttons.b, pad.buttons.z);
    g_ctl.pad = pad;
    if (int n = take_hushed()) logf("control hush tick=%d count=%d", g_ctl.tick, n);
    if (int n = take_quiet_voices()) logf("control quiet voice tick=%d count=%d", g_ctl.tick, n);
    bool blocked = menu_mode();
    // in a menu the view is the game's to change
    if (int had = held() && !blocked ? hold_player() : -1; had >= 0) logf("control regrip tick=%d had=%02X", g_ctl.tick, had);
    if (blocked != g_ctl.blocked) logf("control gate tick=%d blocked=%d menu=%d", g_ctl.tick, blocked, menu_mode());
    g_ctl.blocked = blocked;
    if (g_ctl.toggle.edge(toggle) && !blocked) held() ? release_control("key") : take_control();
    if (g_ctl.activate.edge(activate) && !blocked && taken()) use_door();
    // a box shown while the last one is still closing never comes up
    bool asked = g_ctl.options.edge(options);
    if (!blocked && (asked || std::exchange(g_ctl.reopen, false))) open_options();
    if (int pick = take_options_pick(); pick >= 0) choose_option(pick);
    if (g_done) return;
    run_ticks(control_tick);
    if (taken() && !blocked && sound_ready()) sound_pump();
    // a console line run this frame may have sent the player somewhere else already
    if (taken()) follow_cell();
    if (g_ctl.carry) seat_mario();
    show_hud();
    if (g_config.autotake && g_arrival.frame(place(loaded_cell()), blocked, held())) {
        logf("control arrive tick=%d id=%08X", g_ctl.tick, loaded_cell()->form.refID);
        take_control();
    }
    if (!taken() || g_done) return;
    if (hide_body()) logf("control rehide tick=%d", g_ctl.tick);
    Vec3 drawn = draw_mario();
    mario_mesh_hide(pipboy_active());
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
    bool there = !g_cell_after_load && loaded_cell() && loaded_cell() != g_save_cell;
    g_ctl.started = own_take() ? c != nullptr : there;
    if (g_ctl.started && own_take()) take_control();
}

void on_frame() {
    if (!g_ready || g_done) return;
    g_frames++;
    if (g_frames == 1) {
        std::string why;
        g_overlay = overlay_hook(why);
        if (g_overlay) logf("overlay hook ok=1");
        else logf("refused: overlay hook %s", why.c_str());
    }
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
    else if (g_config.scenario == "hotel") tick_control_scenario(kHotel);
    else if (g_config.scenario == "airport") tick_control_scenario(kAirport);
    else if (g_config.scenario == "kerb") tick_control_scenario(kKerb);
    else if (g_config.scenario == "terminal") tick_control_scenario(kTerminal);
    else if (g_config.scenario == "inout") tick_control_scenario(kInout);
    else if (g_config.scenario == "gamepad") tick_control_scenario(kGamepad);
    else if (g_config.scenario == "walls") tick_control_scenario(kWalls);
    else if (g_config.scenario == "steep") tick_control_scenario(kSteep);
    else if (g_config.scenario == "table") tick_control_scenario(kTable);
    else if (g_config.scenario == "solid") tick_control_scenario(kSolid);
    else if (g_config.scenario == "swing") tick_control_scenario(kSwing);
    else if (g_config.scenario == "sound") tick_control_scenario(kSound);
    else if (g_config.scenario == "soundpause") tick_control_scenario(kSoundPause);
    else if (g_config.scenario == "pipboy") tick_control_scenario(kPipboy);
    else if (g_config.scenario == "pipboyview") tick_control_scenario(kPipboyView);
    else if (g_config.scenario == "pipboytabs") tick_control_scenario(kPipboyTabs);
    else if (g_config.scenario == "pipboyarms") tick_control_scenario(kPipboyArms);
    else if (g_config.scenario == "pipboyequip") tick_control_scenario(kPipboyEquip);
    else if (g_config.scenario == "pipboyreload") tick_control_scenario(kPipboyReload);
    else if (g_config.scenario == "actors") tick_control_scenario(kActors);
    else if (g_config.scenario == "attack") tick_control_scenario(kAttack);
    else if (g_config.scenario == "hurt" || g_config.scenario == "nometer") tick_control_scenario(kHurt);
    else if (g_config.scenario == "death") tick_control_scenario(kDeath);
    else if (g_config.scenario == "bowling") tick_control_scenario(kBowling);
    else if (g_config.scenario == "trooper" || g_config.scenario == "usertrooper") tick_control_scenario(kTrooper);
    else if (const Wild *w = wildlife(g_config.scenario)) g_ctl.who = w->ref, tick_control_scenario(w->script);
    else if (g_config.scenario == "pound") tick_control_scenario(kPound);
    else if (g_config.scenario == "finisher") tick_control_scenario(kFinisher);
    else if (g_config.scenario == "stomp") tick_control_scenario(kStomp);
    else if (g_config.scenario == "spare") tick_control_scenario(kSpare);
    else if (g_config.scenario == "reload") tick_control_scenario(kReload);
    else if (g_config.scenario == "scratch") tick_control_scenario(kScratch);
    else if (g_config.scenario == "base") tick_control_scenario(kBase);
    else if (g_config.scenario == "basequick") tick_control_scenario(kBaseQuick);
    else if (g_config.scenario == "stunonce") tick_control_scenario(kStunOnce);
    else if (g_config.scenario == "bigstun") tick_control_scenario(kBigStun);
    else if (g_config.scenario == "options") tick_control_scenario(kOptions);
    else if (g_config.scenario == "saloon") tick_control_scenario(kSaloon);
    else if (g_config.scenario == "padout") tick_control_scenario(kPadout);
    else if (g_config.scenario == "prison") tick_control_scenario(kPrison);
    else if (g_config.scenario == "gate") tick_control_scenario(kGate);
    else if (g_config.scenario == "particles" || g_config.scenario == "noparticles") tick_control_scenario(kParticles);
    else if (g_config.scenario.empty()) tick_control_scenario(kPlay);
    // play runs until the game closes once it has the player
    bool endless = g_config.scenario == "play" && g_ctl.started;
    if (!g_done && !g_config.scenario.empty() && !endless && g_frames >= kScenarioTimeout) finish(false, "timeout");
}

void on_load_game(bool ok) {
    logf("control load tick=%d ok=%d", g_ctl.tick, ok);
    g_game_loaded = g_game_loaded || ok;
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
    if (!hook_pad(why)) return logf("refused: pad hook %s", why.c_str()), false;
    // without it mario still plays, the game just clicks at the activate key
    if (!hook_activate_sound(why)) logf("refused: activate sound hook %s", why.c_str());
    if (!hook_combat_check(why)) logf("refused: combat hook %s", why.c_str());
    if (!hook_courier_voice(why)) logf("refused: courier voice hook %s", why.c_str());
    g_console = static_cast<const nvse::ConsoleInterface *>(nvse->queryInterface(nvse::kInterfaceConsole));
    auto *msg = static_cast<const nvse::MessagingInterface *>(nvse->queryInterface(nvse::kInterfaceMessaging));
    if (!msg || !msg->registerListener(g_handle, "NVSE", on_message)) {
        logf("refused: messaging interface unavailable");
        return false;
    }
    return true;
}
