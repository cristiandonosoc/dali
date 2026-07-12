#include <dali/game/assets/spritesheet_asset.h>

#include <dali/core/memory.h>
#include <dali/game/assets/registry.h>
#include <dali/game/file.h>
#include <dali/game/log.h>

#include <yaml-cpp/yaml.h>

namespace kdk {

namespace spritesheet_asset_private {

struct GridDims {
    i32 Cols = 0;
    i32 Rows = 0;
};

// Number of whole cells that fit across and down a |tex_w| x |tex_h| texture. {0,0} for a degenerate
// grid. n cells of side C separated by S span n*C + (n-1)*S, so n = (usable + S) / (C + S).
GridDims ComputeGridDims(const SpriteGrid& grid, i32 tex_w, i32 tex_h) {
    if (grid.CellW <= 0) {
        return {};
    }
    if (grid.CellH <= 0) {
        return {};
    }
    i32 usable_w = tex_w - 2 * grid.Margin;
    i32 usable_h = tex_h - 2 * grid.Margin;
    if (usable_w < grid.CellW) {
        return {};
    }
    if (usable_h < grid.CellH) {
        return {};
    }
    GridDims dims = {};
    dims.Cols = (usable_w + grid.Spacing) / (grid.CellW + grid.Spacing);
    dims.Rows = (usable_h + grid.Spacing) / (grid.CellH + grid.Spacing);
    return dims;
}

}  // namespace spritesheet_asset_private

i32 SpriteTextureRef::FrameCount() const {
    using namespace spritesheet_asset_private;

    if (!_Resolved) {
        return 0;
    }
    const kdk::Texture& res = _Resolved->Resource;
    GridDims dims = ComputeGridDims(Grid, res.Width, res.Height);
    return dims.Cols * dims.Rows;
}

SpriteTextureRef::FrameUV SpriteTextureRef::FrameRect(i32 frame) const {
    using namespace spritesheet_asset_private;

    FrameUV uv = {};
    if (!_Resolved) {
        return uv;
    }
    if (frame < 0) {
        return uv;
    }
    const kdk::Texture& res = _Resolved->Resource;
    GridDims dims = ComputeGridDims(Grid, res.Width, res.Height);
    if (dims.Cols <= 0) {
        return uv;
    }

    i32 col = frame % dims.Cols;
    i32 row = frame / dims.Cols;
    i32 x = Grid.Margin + col * (Grid.CellW + Grid.Spacing);
    i32 y = Grid.Margin + row * (Grid.CellH + Grid.Spacing);

    uv.Handle = res.Handle;
    uv.Width = Grid.CellW;
    uv.Height = Grid.CellH;
    uv.Uv0 = Vec2{(float)x / (float)res.Width, (float)y / (float)res.Height};
    uv.Uv1 = Vec2{(float)(x + Grid.CellW) / (float)res.Width, (float)(y + Grid.CellH) / (float)res.Height};
    return uv;
}

i32 SpriteClip::At(float time, float fps, bool loop) const {
    if (Frames.IsEmpty()) {
        return NONE;
    }
    i32 count = Frames.Size;
    i32 step = (i32)(time * fps);
    if (step < 0) {
        step = 0;
    }
    if (loop) {
        step = step % count;
    } else if (step >= count) {
        step = count - 1;
    }
    return Frames[step];
}

bool SpritesheetAsset::Create(AssetId id) {
    if (!id.IsValid()) {
        LogError("SpritesheetAsset::Create: non-canonical id '%s'", id.Value.Str());
        return false;
    }

    SpritesheetAsset sheet = {};
    sheet.Manifest.Type = kAssetType;
    sheet.Manifest.Version = kVersion;
    sheet.Manifest.Id = id;
    sheet.Manifest.HasPayload = false;
    return sheet.SaveManifest();
}

std::optional<SpritesheetAsset> SpritesheetAsset::LoadFromDisk(AssetId id) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    FileContents contents = ReadFile(arena, AssetYmlPath(arena, id));
    if (!contents.IsValid()) {
        return std::nullopt;
    }
    YAML::Node node = YAML::Load((const char*)contents.Data.data());

    EAssetType type = AssetTypeFromString(StringView(node["type"].as<std::string>("invalid").c_str()));
    if (type != kAssetType) {
        return std::nullopt;
    }

    i32 version = node["version"].as<int>(0);
    if (version != kVersion) {
        LogError("SpritesheetAsset: '%s' is version %d, expected %d - re-author needed",
                 id.Value.Str(),
                 version,
                 kVersion);
        return std::nullopt;
    }

    AssetId stored = AssetId::Normalize(StringView(node["id"].as<std::string>("").c_str()));
    if (!(stored == id)) {
        LogError("SpritesheetAsset: id mismatch - manifest says '%s' but lives at '%s'",
                 stored.Value.Str(),
                 id.Value.Str());
    }

    SpritesheetAsset sheet = {};
    sheet.Manifest.Type = kAssetType;
    sheet.Manifest.Version = version;
    sheet.Manifest.Id = id;
    sheet.Manifest.HasPayload = false;

    if (YAML::Node textures = node["textures"]) {
        for (const auto& tex_node : textures) {
            if (sheet.Textures.IsFull()) {
                break;
            }
            SpriteTextureRef ref = {};
            ref.Texture = AssetId::Normalize(StringView(tex_node["texture"].as<std::string>("").c_str()));
            if (YAML::Node grid = tex_node["grid"]) {
                ref.Grid.CellW = grid["cell_w"].as<int>(0);
                ref.Grid.CellH = grid["cell_h"].as<int>(0);
                ref.Grid.Margin = grid["margin"].as<int>(0);
                ref.Grid.Spacing = grid["spacing"].as<int>(0);
            }
            sheet.Textures.Push(ref);
        }
    }

    if (YAML::Node clips = node["clips"]) {
        for (const auto& clip_node : clips) {
            if (sheet.Clips.IsFull()) {
                break;
            }
            SpriteClip clip = {};
            clip.Name = StringView(clip_node["name"].as<std::string>("").c_str());
            clip.Texture = AssetId::Normalize(StringView(clip_node["texture"].as<std::string>("").c_str()));
            if (YAML::Node frames = clip_node["frames"]) {
                for (const auto& frame_node : frames) {
                    if (clip.Frames.IsFull()) {
                        break;
                    }
                    clip.Frames.Push(frame_node.as<int>(0));
                }
            }
            sheet.Clips.Push(clip);
        }
    }

    return sheet;
}

bool SpritesheetAsset::SaveManifest() const {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    StringView yml_path = AssetYmlPath(arena, Manifest.Id);

    YAML::Emitter emit;
    emit << YAML::BeginMap;
    emit << YAML::Key << "type" << YAML::Value << ToString(kAssetType).Str();
    emit << YAML::Key << "version" << YAML::Value << kVersion;
    emit << YAML::Key << "id" << YAML::Value << Manifest.Id.Value.Str();

    emit << YAML::Key << "textures" << YAML::Value << YAML::BeginSeq;
    for (const SpriteTextureRef& ref : Textures) {
        emit << YAML::Flow << YAML::BeginMap;
        emit << YAML::Key << "texture" << YAML::Value << ref.Texture.Value.Str();
        emit << YAML::Key << "grid" << YAML::Value << YAML::Flow << YAML::BeginMap;
        emit << YAML::Key << "cell_w" << YAML::Value << ref.Grid.CellW;
        emit << YAML::Key << "cell_h" << YAML::Value << ref.Grid.CellH;
        emit << YAML::Key << "margin" << YAML::Value << ref.Grid.Margin;
        emit << YAML::Key << "spacing" << YAML::Value << ref.Grid.Spacing;
        emit << YAML::EndMap;
        emit << YAML::EndMap;
    }
    emit << YAML::EndSeq;

    emit << YAML::Key << "clips" << YAML::Value << YAML::BeginSeq;
    for (const SpriteClip& clip : Clips) {
        emit << YAML::Flow << YAML::BeginMap;
        emit << YAML::Key << "name" << YAML::Value << clip.Name.Str();
        emit << YAML::Key << "texture" << YAML::Value << clip.Texture.Value.Str();
        emit << YAML::Key << "frames" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (i32 frame : clip.Frames) {
            emit << frame;
        }
        emit << YAML::EndSeq;
        emit << YAML::EndMap;
    }
    emit << YAML::EndSeq;
    emit << YAML::EndMap;

    if (!WriteFile(yml_path, std::span<const u8>((const u8*)emit.c_str(), emit.size()))) {
        LogError("SpritesheetAsset: cannot write manifest '%s'", yml_path.Str());
        return false;
    }
    return true;
}

bool SpritesheetAsset::ResolveReferences(AssetRegistry& registry) {
    bool all_resolved = true;
    for (SpriteTextureRef& ref : Textures) {
        ref._Resolved = registry.FindTexture(ref.Texture);
        if (!ref._Resolved) {
            LogError("Spritesheet '%s' references missing texture '%s'",
                     Manifest.Id.Value.Str(),
                     ref.Texture.Value.Str());
            all_resolved = false;
        }
    }
    return all_resolved;
}

const SpriteTextureRef* SpritesheetAsset::FindTextureRef(AssetId texture) const {
    for (const SpriteTextureRef& ref : Textures) {
        if (ref.Texture == texture) {
            return &ref;
        }
    }
    return nullptr;
}

const SpriteClip* SpritesheetAsset::FindClip(StringView name) const {
    for (const SpriteClip& clip : Clips) {
        if (clip.Name.Equals(name)) {
            return &clip;
        }
    }
    return nullptr;
}

}  // namespace kdk
