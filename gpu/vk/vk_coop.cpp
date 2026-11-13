// (oot-remake-multiplayer) Co-op's drawing over the picture: hookshot_coop.dll's hos_coop_draw_wanted / hos_coop_draw
// (hos_coop_draw.h), found once host/coop_bridge.c has loaded it, drawn in the menu's Dear ImGui frame (vk_menu.cpp
// menu_record). Nothing happens without the module: no ImGui frame is made for it. A module with hos_coop_draw2 also gets
// fonts and pictures of its own (the HUD pack's game font and heart pictures): loaded once, kept for the process's life.
#include "vk_coop.h"

#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"   // (RegisterUserTexture: the module's pictures, as vk_imgui.cpp's logo)
#include "vk_imgui.h"
#include "vk_internal.h"   // (logf, memory_read_as_submitted)
#include "vk_texture.h"    // (Texture, texture_frame_no)
#include "hos_coop_draw.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mkwvk::coop {
namespace {

std::atomic<hos_coop_draw_wanted_fn> wanted_fn{nullptr};
std::atomic<hos_coop_draw_fn> draw_fn{nullptr};
std::atomic<hos_coop_draw2_fn> draw2_fn{nullptr};   // (a module with the game look: the whole table)
std::atomic<hos_coop_camera_wanted_fn> camera_wanted_fn{nullptr};   // (a module with name tags on Hookshot)
std::atomic<hos_coop_camera_fn> camera_fn{nullptr};

// (the bridge loads the module at the game's first frame hook: looked for again every 60 presents until found)
void find() {
    static int wait = 0;
    if (draw_fn.load(std::memory_order_acquire) || wait-- > 0) return;
    wait = 60;
#ifdef _WIN32
    HMODULE m = GetModuleHandleA("hookshot_coop.dll");
    if (!m) return;
    auto w = (hos_coop_draw_wanted_fn)(void *)GetProcAddress(m, "hos_coop_draw_wanted");
    auto d = (hos_coop_draw_fn)(void *)GetProcAddress(m, "hos_coop_draw");
    auto d2 = (hos_coop_draw2_fn)(void *)GetProcAddress(m, "hos_coop_draw2");
    auto cw = (hos_coop_camera_wanted_fn)(void *)GetProcAddress(m, "hos_coop_camera_wanted");
    auto cf = (hos_coop_camera_fn)(void *)GetProcAddress(m, "hos_coop_camera");
#else
    void *m = dlopen("hookshot_coop.so", RTLD_NOW | RTLD_NOLOAD);
    auto w = (hos_coop_draw_wanted_fn)(m ? dlsym(m, "hos_coop_draw_wanted") : dlsym(RTLD_DEFAULT, "hos_coop_draw_wanted"));
    auto d = (hos_coop_draw_fn)(m ? dlsym(m, "hos_coop_draw") : dlsym(RTLD_DEFAULT, "hos_coop_draw"));
    auto d2 = (hos_coop_draw2_fn)(m ? dlsym(m, "hos_coop_draw2") : dlsym(RTLD_DEFAULT, "hos_coop_draw2"));
    auto cw = (hos_coop_camera_wanted_fn)(m ? dlsym(m, "hos_coop_camera_wanted") : dlsym(RTLD_DEFAULT, "hos_coop_camera_wanted"));
    auto cf = (hos_coop_camera_fn)(m ? dlsym(m, "hos_coop_camera") : dlsym(RTLD_DEFAULT, "hos_coop_camera"));
#endif
    if (!w || !d) return;   // (an older module: nothing to draw)
    wanted_fn.store(w, std::memory_order_release);
    draw2_fn.store(d2, std::memory_order_release);
    if (cw && cf) {
        camera_fn.store(cf, std::memory_order_release);
        camera_wanted_fn.store(cw, std::memory_order_release);
    }
    draw_fn.store(d, std::memory_order_release);
}

// (the module's fonts and pictures: used on the thread that records the present only)
struct Loaded {
    std::string path;
    ImFont *font;
};
std::vector<Loaded> fonts_;
std::vector<ImTextureData *> images_;
ImFont *chosen = nullptr;   // (use_font's; nullptr: Hookshot's own)

ImFont *font() {
    if (chosen) return chosen;
    ImFont *f = ui::fonts().strong;
    return f ? f : ImGui::GetFont();
}
int font_load(void *, const char *path) {
    if (!path || !*path) return 0;
    for (size_t i = 0; i < fonts_.size(); i++)
        if (fonts_[i].path == path) return fonts_[i].font ? (int)i + 1 : 0;
    ImFontConfig cfg;
    cfg.Flags |= ImFontFlags_NoLoadError;   // (a missing file: 0, the module keeps its plain look)
    const char *base = path + strlen(path);
    while (base > path && base[-1] != '/' && base[-1] != '\\') base--;
    snprintf(cfg.Name, sizeof cfg.Name, "co-op %s", base);
    ImFont *f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 18.0f, &cfg);   // (ImGui 1.92: any time, glyphs baked as drawn)
    fonts_.push_back({path, f});
    logf("vk: co-op: font %s %s", base, f ? "loaded" : "could not be loaded");
    return f ? (int)fonts_.size() : 0;
}
void use_font(void *, int id) {
    chosen = id > 0 && id <= (int)fonts_.size() ? fonts_[id - 1].font : nullptr;
}
int image(void *, int w, int h, const uint8_t *rgba) {
    if (!rgba || w <= 0 || h <= 0 || w > 4096 || h > 4096 || images_.size() >= 64) return 0;
    ImTextureData *t = IM_NEW(ImTextureData)();
    t->Create(ImTextureFormat_RGBA32, w, h);   // (copied in on the present's command buffer, as the logo: vk_imgui.cpp)
    memcpy(t->GetPixels(), rgba, (size_t)w * h * 4);
    t->UseColors = true;
    ImGui::RegisterUserTexture(t);
    images_.push_back(t);
    return (int)images_.size();
}
void draw_image(void *, int id, float x0, float y0, float x1, float y1, uint32_t tint) {
    if (id <= 0 || id > (int)images_.size()) return;
    ImGui::GetForegroundDrawList()->AddImage(images_[id - 1]->GetTexRef(), ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(0, 0),
                                             ImVec2(1, 1), tint);
}
void rect(void *, float x0, float y0, float x1, float y1, uint32_t rgba, float rounding) {
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), rgba, rounding);
}
void circle(void *, float x, float y, float r, uint32_t rgba, float thickness) {
    if (thickness > 0) ImGui::GetForegroundDrawList()->AddCircle(ImVec2(x, y), r, rgba, 0, thickness);
    else ImGui::GetForegroundDrawList()->AddCircleFilled(ImVec2(x, y), r, rgba);
}
void triangle(void *, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba) {
    ImGui::GetForegroundDrawList()->AddTriangleFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x2, y2), rgba);
}
void text(void *, float x, float y, float px, uint32_t rgba, const char *s) {
    if (s) ImGui::GetForegroundDrawList()->AddText(font(), px, ImVec2(x, y), rgba, s);
}
float text_width(void *, float px, const char *s) {
    return s ? font()->CalcTextSizeA(px, FLT_MAX, 0.0f, s).x : 0.0f;
}

}  // namespace

void note_scene_draw(const Texture *color0, uint32_t width, uint32_t height, uint64_t camera, uint64_t camera_size) {
    const auto cw = camera_wanted_fn.load(std::memory_order_acquire);
    if (!cw || !color0 || !camera || camera_size < 0x130 || width < 640 || height < 360 ||
        color0->format != VK_FORMAT_B10G11R11_UFLOAT_PACK32)
        return;
    const uint64_t frame = texture_frame_no();
    if (!cw(frame, (float)width, (float)height)) return;
    float block[0x130 / 4];
    if (!memory_read_as_submitted(camera, block, sizeof block)) return;
    if (const auto cf = camera_fn.load(std::memory_order_acquire)) cf(frame, block, (float)width, (float)height);
}

bool wanted() {
    find();
    const auto w = wanted_fn.load(std::memory_order_acquire);
    return w && w() != 0;
}

void draw(uint32_t w, uint32_t h) {
    const auto d = draw_fn.load(std::memory_order_acquire);
    if (!d) return;
    HosCoopDraw c{(float)w, (float)h, nullptr, rect, circle, triangle, text, text_width, font_load, use_font, image, draw_image};
    chosen = nullptr;
    if (const auto d2 = draw2_fn.load(std::memory_order_acquire)) d2(&c);
    else d(&c);
    chosen = nullptr;   // (the menu's text after it: Hookshot's own font)
}

}  // namespace mkwvk::coop
