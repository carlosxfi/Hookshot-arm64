// (oot-remake-multiplayer) Co-op's drawing over the picture (vk_coop.cpp, hos_coop_draw.h).
#pragma once

#include <cstdint>

namespace mkwvk {
struct Texture;
}

namespace mkwvk::coop {

/// The co-op module has something to draw this present (false without the module, or an older one)
bool wanted();
/// Draws it into the current Dear ImGui frame (after ui::begin), over a w x h window image
void draw(uint32_t w, uint32_t h);
/// A draw into a target with depth (vk_draw.cpp, beside temporal_note_scene_draw): the scene's camera for the module's
/// name tags, when it has hos_coop_camera (nothing otherwise). camera: the vertex stage's uniform buffer 3
void note_scene_draw(const Texture *color0, uint32_t width, uint32_t height, uint64_t camera, uint64_t camera_size);

}  // namespace mkwvk::coop
