#include "core/config.h"
#include "core/rom.h"
#include "game/log.h"
#include "game/nvse.h"

#include <string>

using namespace sm64nv;

namespace {

const char *kVersion = "0.1.0";

nvse::PluginHandle g_handle;
const nvse::ConsoleInterface *g_console;
std::string g_dir;
Config g_config;
std::vector<uint8_t> g_rom;
uint32_t g_frames;
bool g_ready, g_done;

std::string read_text(const std::string &path) {
    std::vector<uint8_t> b = read_file(path);
    return {b.begin(), b.end()};
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
    g_ready = true;
}

void on_frame() {
    if (!g_ready || g_done) return;
    g_frames++;
    if (!g_config.scenario.empty() && g_frames == (uint32_t)g_config.frames) {
        logf("scenario done name=%s frames=%u", g_config.scenario.c_str(), g_frames);
        g_done = true;
    }
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
