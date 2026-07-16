#include <dali/game/graphics/texture.h>

#include <dali/game/platform.h>

#include <glad/gl.h>
#include <stb/stb_image.h>

namespace kdk {

namespace texture_private {

GLint GLFilter(ETextureFilter filter) {
    switch (filter) {
        case ETextureFilter::Nearest: return GL_NEAREST;
        case ETextureFilter::Linear: return GL_LINEAR;
    }
    return GL_NEAREST;
}

GLenum GLFormat(i32 channels) {
    switch (channels) {
        case 1: return GL_RED;
        case 3: return GL_RGB;
        case 4: return GL_RGBA;
    }
    return GL_NONE;
}

}  // namespace texture_private

Texture Texture::FromPixels(std::span<const u8> pixels,
                            i32 width,
                            i32 height,
                            i32 channels,
                            ETextureFilter filter) {
    using namespace texture_private;

    GLenum format = GLFormat(channels);
    if (format == GL_NONE) {
        LogError("Texture: unsupported channel count %d", channels);
        return {};
    }

    GLuint handle = GL_NONE;
    glGenTextures(1, &handle);
    if (handle == GL_NONE) {
        return {};
    }

    GLint gl_filter = GLFilter(filter);
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, GL_NONE);

    Texture texture = {};
    texture.Handle = handle;
    texture.Width = width;
    texture.Height = height;
    texture.Channels = channels;
    texture.Filter = filter;
    return texture;
}

Texture Texture::Load(StringView path, bool flip_vertically) {
    stbi_set_flip_vertically_on_load(flip_vertically);

    i32 width = 0;
    i32 height = 0;
    i32 channels = 0;
    u8* data = stbi_load(path.Str(), &width, &height, &channels, 0);
    if (!data) {
        LogError("Texture: failed to load '%s'", path.Str());
        return {};
    }
    DEFER { stbi_image_free(data); };

    u64 byte_count = (u64)(width * height * channels);
    return FromPixels(std::span<const u8>(data, byte_count), width, height, channels, ETextureFilter::Nearest);
}

void Texture::Destroy() {
    if (Handle == GL_NONE) {
        return;
    }
    GLuint handle = Handle;
    glDeleteTextures(1, &handle);
    Handle = GL_NONE;
    Width = 0;
    Height = 0;
    Channels = 0;
    Filter = ETextureFilter::Nearest;
}

void Texture::SetFilter(ETextureFilter filter) {
    using namespace texture_private;

    if (Handle == GL_NONE) {
        return;
    }
    Filter = filter;
    GLint gl_filter = GLFilter(filter);
    glBindTexture(GL_TEXTURE_2D, Handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
    glBindTexture(GL_TEXTURE_2D, GL_NONE);
}

}  // namespace kdk
