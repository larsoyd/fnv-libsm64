#pragma once
#include <cstdint>

namespace nvse {

using PluginHandle = uint32_t;

struct Interface {
    uint32_t nvseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void *registerCommand;
    void *setOpcodeBase;
    void *(*queryInterface)(uint32_t id);
    PluginHandle (*getPluginHandle)();
    void *registerTypedCommand;
    const char *(*getRuntimeDirectory)();
};

struct PluginInfo {
    uint32_t infoVersion;
    const char *name;
    uint32_t version;
};

struct Message {
    const char *sender;
    uint32_t type;
    uint32_t dataLen;
    void *data;
};

struct MessagingInterface {
    uint32_t version;
    bool (*registerListener)(PluginHandle listener, const char *sender, void (*handler)(Message *));
    bool (*dispatch)(PluginHandle sender, uint32_t type, void *data, uint32_t len, const char *receiver);
};

struct ConsoleInterface {
    uint32_t version;
    // success comes back as nonzero bytes other than 1 so read a byte not a bool
    uint8_t (*runScriptLine)(const char *line, void *refr);
    uint8_t (*runScriptLine2)(const char *line, void *refr, bool suppressOutput);
};

enum : uint32_t {
    kInterfaceConsole = 1,
    kInterfaceMessaging = 2,
};

enum : uint32_t {
    kMessagePostLoad = 0,
    kMessagePostLoadGame = 8,
    kMessageDeferredInit = 18,
    kMessageMainGameLoop = 20,
};

inline constexpr uint32_t kRuntime1_4_0_525 = 0x040020D0;

}
