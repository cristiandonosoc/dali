#pragma once

#include <dali/core/defines.h>
#include <dali/core/string.h>

#include <span>

namespace kdk {

enum class ETextureFilter : u8 {
    Nearest,  // Crisp texels — the pixel-art default.
    Linear,
};

// A GPU texture: a thin owner of a GL texture object. Handle lives in the platform's GL context, so
// it survives DLL hot-reloads untouched (the DLL only reloads GLAD's function table, not the GL
// objects). GLAD is kept out of this header so gameplay/UI can include it freely; Handle is a plain
// u32 (a GLuint under the hood).
struct Texture {
    u32 Handle = 0;  // GL texture object; 0 == invalid.
    i32 Width = 0;
    i32 Height = 0;
    i32 Channels = 0;                                 // 1/3/4 (-> R/RGB/RGBA).
    ETextureFilter Filter = ETextureFilter::Nearest;  // Sampling mode set on the GL object.

    // Decodes |path| with stb_image and uploads it (nearest filtering). Invalid on failure. Used by
    // standalone/debug paths; the asset pipeline bakes pixels and uses FromPixels instead.
    static Texture Load(StringView path, bool flip_vertically = false);
    // Uploads a tightly-packed, in-memory pixel buffer. |channels| is 1/3/4 (-> R/RGB/RGBA). No
    // mipmaps, clamp-to-edge wrap. Invalid on an unsupported channel count.
    static Texture FromPixels(std::span<const u8> pixels,
                              i32 width,
                              i32 height,
                              i32 channels,
                              ETextureFilter filter);

    bool IsValid() const { return Handle != 0; }
    // Deletes the GL object and resets to invalid. Safe to call on an invalid Texture.
    void Destroy();
    // Re-applies the sampling mode to the live GL object (no re-upload) and updates Filter. This is
    // why filter is not an "import setting": it can be changed on an existing texture.
    void SetFilter(ETextureFilter filter);
    // The GL handle widened to ImGui's texture id (ImTextureID == ImU64). Cast to ImTextureID at the
    // ImGui::Image call site; kept as u64 here so this header stays ImGui-free.
    u64 ImGuiId() const { return (u64)Handle; }
};

}  // namespace kdk
