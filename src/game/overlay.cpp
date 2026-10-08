#include "game/overlay.h"
#include "game/log.h"
#include "game/rtti.h"

#include <d3d9.h>
#include <psapi.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace sm64nv {

namespace {

// the game's renderer and where it keeps its device, and the device's present slot
const uintptr_t kRenderer = 0x011C73B4;
const size_t kDevice = 0x288, kPresentSlot = 17;

using Present = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
Present g_present;

struct Picture {
    std::vector<uint8_t> rgba;
    int width, height;
    IDirect3DTexture9 *texture = nullptr;
};

std::mutex g_lock;
std::vector<Picture> g_pictures;
std::vector<OverlayQuad> g_quads;
std::string g_capture;
OverlayStats g_stats;
std::atomic<int> g_width{0}, g_height{0};

struct Vertex {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};

IDirect3DTexture9 *texture_of(IDirect3DDevice9 *dev, Picture &p) {
    if (p.texture) return p.texture;
    if (FAILED(dev->CreateTexture(p.width, p.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &p.texture, nullptr))) return nullptr;
    D3DLOCKED_RECT r;
    if (FAILED(p.texture->LockRect(0, &r, nullptr, 0))) return nullptr;
    for (int y = 0; y < p.height; y++)
        for (int x = 0; x < p.width; x++) {
            const uint8_t *s = &p.rgba[4 * (y * p.width + x)];
            static_cast<uint32_t *>(r.pBits)[y * r.Pitch / 4 + x] = (uint32_t)s[3] << 24 | s[0] << 16 | s[1] << 8 | s[2];
        }
    p.texture->UnlockRect(0);
    return p.texture;
}

void draw(IDirect3DDevice9 *dev, const std::vector<OverlayQuad> &quads) {
    IDirect3DStateBlock9 *saved = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved))) return;
    IDirect3DSurface9 *target = nullptr, *back = nullptr;
    dev->GetRenderTarget(0, &target);
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    dev->SetRenderTarget(0, back);
    dev->SetVertexShader(nullptr), dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    for (auto [state, value] : {std::pair{D3DRS_ZENABLE, (DWORD)FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, TRUE},
                                {D3DRS_SRCBLEND, D3DBLEND_SRCALPHA}, {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {D3DRS_CULLMODE, D3DCULL_NONE},
                                {D3DRS_LIGHTING, FALSE}, {D3DRS_FOGENABLE, FALSE}, {D3DRS_ALPHATESTENABLE, FALSE},
                                {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, 0xF}})
        dev->SetRenderState(state, value);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT), dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP), dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE), dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    for (const OverlayQuad &oq : quads) {
        IDirect3DTexture9 *tex = oq.picture >= 0 && oq.picture < (int)g_pictures.size() ? texture_of(dev, g_pictures[oq.picture]) : nullptr;
        // a picture's colour times the quad's, a bare quad is its colour alone
        D3DTEXTUREOP op = tex ? D3DTOP_MODULATE : D3DTOP_SELECTARG2;
        dev->SetTexture(0, tex);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, op), dev->SetTextureStageState(0, D3DTSS_ALPHAOP, op);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE), dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE), dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        const HudQuad &q = oq.q;
        // pixel centres sit half a pixel in
        Vertex v[4] = {{q.x0 - 0.5f, q.y0 - 0.5f, 0, 1, q.argb, q.u0, q.v0}, {q.x1 - 0.5f, q.y0 - 0.5f, 0, 1, q.argb, q.u1, q.v0},
                       {q.x0 - 0.5f, q.y1 - 0.5f, 0, 1, q.argb, q.u0, q.v1}, {q.x1 - 0.5f, q.y1 - 0.5f, 0, 1, q.argb, q.u1, q.v1}};
        if (SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex)))) g_stats.drawn++;
    }
    dev->SetRenderTarget(0, target);
    saved->Apply();
    saved->Release();
    if (target) target->Release();
    if (back) back->Release();
}

// the back buffer as a bottom up 24 bit bmp
void capture(IDirect3DDevice9 *dev, const std::string &path) {
    IDirect3DSurface9 *back = nullptr, *copy = nullptr;
    D3DSURFACE_DESC d;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return logf("overlay capture failed step=backbuffer");
    back->GetDesc(&d);
    bool ok = (d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8) && d.MultiSampleType == D3DMULTISAMPLE_NONE &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr)) &&
              SUCCEEDED(dev->GetRenderTargetData(back, copy));
    D3DLOCKED_RECT r;
    if (ok && SUCCEEDED(copy->LockRect(&r, nullptr, D3DLOCK_READONLY))) {
        uint32_t row = (d.Width * 3 + 3) & ~3u, size = 54 + row * d.Height;
        std::vector<uint8_t> bmp(size, 0);
        uint8_t *h = bmp.data();
        h[0] = 'B', h[1] = 'M';
        memcpy(h + 2, &size, 4);
        uint32_t at = 54, info = 40, w = d.Width, ht = d.Height;
        uint16_t planes = 1, bits = 24;
        memcpy(h + 10, &at, 4), memcpy(h + 14, &info, 4), memcpy(h + 18, &w, 4), memcpy(h + 22, &ht, 4);
        memcpy(h + 26, &planes, 2), memcpy(h + 28, &bits, 2);
        for (uint32_t y = 0; y < d.Height; y++)
            for (uint32_t x = 0; x < d.Width; x++)
                memcpy(&bmp[54 + (d.Height - 1 - y) * row + 3 * x], static_cast<uint8_t *>(r.pBits) + y * r.Pitch + 4 * x, 3);
        copy->UnlockRect();
        if (FILE *f = fopen(path.c_str(), "wb")) {
            fwrite(bmp.data(), 1, bmp.size(), f), fclose(f);
            g_stats.captured++;
        }
    } else {
        logf("overlay capture failed format=%u samples=%u", (unsigned)d.Format, (unsigned)d.MultiSampleType);
    }
    if (copy) copy->Release();
    back->Release();
}

HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty) {
    {
        std::lock_guard hold(g_lock);
        g_stats.presents++;
        IDirect3DSurface9 *back = nullptr;
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) {
            D3DSURFACE_DESC d;
            back->GetDesc(&d);
            g_width = (int)d.Width, g_height = (int)d.Height;
            back->Release();
        }
        if (!g_quads.empty()) draw(dev, g_quads);
        if (!g_capture.empty()) capture(dev, g_capture), g_capture.clear();
    }
    return g_present(dev, src, dst, wnd, dirty);
}

bool in_module(const char *name, uintptr_t at) {
    MODULEINFO mi{};
    HMODULE m = GetModuleHandleA(name);
    if (!m || !GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof mi)) return false;
    return at >= (uintptr_t)mi.lpBaseOfDll && at < (uintptr_t)mi.lpBaseOfDll + mi.SizeOfImage;
}

}

bool overlay_hook(std::string &why) {
    void *renderer = *reinterpret_cast<void **>(kRenderer);
    if (!renderer || strcmp(rtti_name(renderer), ".?AVNiDX9Renderer@@")) return why = "renderer rtti", false;
    auto *dev = *reinterpret_cast<IDirect3DDevice9 **>(static_cast<uint8_t *>(renderer) + kDevice);
    uintptr_t *vtbl = dev ? *reinterpret_cast<uintptr_t **>(dev) : nullptr;
    if (!vtbl || !in_module("d3d9.dll", (uintptr_t)vtbl) || !in_module("d3d9.dll", vtbl[kPresentSlot])) return why = "device not d3d9", false;
    if (vtbl[kPresentSlot] == reinterpret_cast<uintptr_t>(&present)) return true;
    DWORD old;
    if (!VirtualProtect(&vtbl[kPresentSlot], 4, PAGE_READWRITE, &old)) return why = "protect error=" + std::to_string(GetLastError()), false;
    g_present = reinterpret_cast<Present>(vtbl[kPresentSlot]);
    vtbl[kPresentSlot] = reinterpret_cast<uintptr_t>(&present);
    VirtualProtect(&vtbl[kPresentSlot], 4, old, &old);
    return true;
}

int overlay_picture(std::vector<uint8_t> rgba, int width, int height) {
    std::lock_guard hold(g_lock);
    g_pictures.push_back({std::move(rgba), width, height});
    return (int)g_pictures.size() - 1;
}

void overlay_set(std::vector<OverlayQuad> quads) {
    std::lock_guard hold(g_lock);
    g_quads = std::move(quads);
}

void overlay_size(int &width, int &height) { width = g_width, height = g_height; }

void overlay_capture(const std::string &path) {
    std::lock_guard hold(g_lock);
    g_capture = path;
}

OverlayStats overlay_take_stats() {
    std::lock_guard hold(g_lock);
    OverlayStats s = g_stats;
    s.width = g_width, s.height = g_height;
    g_stats = {};
    return s;
}

}
