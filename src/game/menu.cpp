#include "game/menu.h"
#include "core/options.h"

#include <string>
#include <utility>
#include <vector>

namespace sm64nv {

namespace {

using Callback = void(__cdecl *)();
using ShowBox = bool(__cdecl *)(const char *, uint32_t, const char *, Callback, uint32_t, uint32_t, float, float, const char *, ...);
using PressedButton = uint8_t(__cdecl *)();

int g_pick = -1;

void __cdecl options_pressed() { g_pick = reinterpret_cast<PressedButton>(0x00703FA0)(); }

}

bool show_options(const Config &c) {
    std::vector<std::string> b;
    for (const Option &o : game_options()) b.push_back(option_button(o, c));
    b.push_back("Done");
    if (b.size() != 8) return false;
    return reinterpret_cast<ShowBox>(0x00703E80)("Mario", 0, nullptr, &options_pressed, 0, 0x17, 0, 0, b[0].c_str(), b[1].c_str(),
                                                 b[2].c_str(), b[3].c_str(), b[4].c_str(), b[5].c_str(), b[6].c_str(), b[7].c_str(),
                                                 nullptr);
}

int take_options_pick() { return std::exchange(g_pick, -1); }

}
