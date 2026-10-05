#pragma once
#include <cstddef>
#include <cstdint>

namespace fnv {

// addresses and layouts for game version 1.4.0.525 which always loads at its preferred base
inline constexpr uintptr_t kPlayerSingleton = 0x011DEA3C;

inline constexpr uintptr_t kVtblPlayerCharacter = 0x0108AA3C;
inline constexpr uintptr_t kVtblTESObjectCELL = 0x0102E9B4;
inline constexpr uintptr_t kVtblNiNode = 0x0109B5AC;

struct TESForm {
    void *vtbl;
    uint8_t typeID;
    uint8_t pad05[3];
    uint32_t flags;
    uint32_t refID;
};
static_assert(offsetof(TESForm, refID) == 0x0C);

template <typename T> struct ListNode {
    T *data;
    ListNode *next;
};

struct RenderState {
    uint8_t pad00[0x14];
    void *niNode;
};

struct TESObjectREFR {
    TESForm form;
    uint8_t pad10[0x10];
    TESForm *baseForm;
    float rot[3];
    float pos[3];
    float scale;
    struct TESObjectCELL *parentCell;
    uint8_t extraDataList[0x20];
    RenderState *renderState;
};
static_assert(offsetof(TESObjectREFR, baseForm) == 0x20);
static_assert(offsetof(TESObjectREFR, pos) == 0x30);
static_assert(offsetof(TESObjectREFR, parentCell) == 0x40);
static_assert(offsetof(TESObjectREFR, renderState) == 0x64);

struct TESObjectCELL {
    TESForm form;
    uint8_t pad10[0x14];
    uint8_t cellFlags;
    uint8_t cellGameFlags;
    uint8_t cellState;
    uint8_t pad27[0xAC - 0x27];
    ListNode<TESObjectREFR> objectList;
};
static_assert(offsetof(TESObjectCELL, cellFlags) == 0x24);
static_assert(offsetof(TESObjectCELL, objectList) == 0xAC);

inline uintptr_t vtbl_of(const void *obj) { return obj ? *reinterpret_cast<const uintptr_t *>(obj) : 0; }

inline TESObjectREFR *player() { return *reinterpret_cast<TESObjectREFR **>(kPlayerSingleton); }

}
