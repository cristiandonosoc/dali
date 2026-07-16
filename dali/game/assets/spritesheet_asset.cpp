#include <dali/game/assets/spritesheet_asset.h>

#include <dali/core/memory.h>
#include <dali/game/assets/registry.h>
#include <dali/game/platform.h>
#include <dali/game/serde.h>

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

bool SpriteSheetClip::Resolve(AssetRegistry& registry) {
    _Resolved = registry.FindTextureAsset(Texture);
    _Handle = 0;
    _CellSize = {};
    _Frames.Clear();
    if (!_Resolved) {
        return false;
    }

    _Handle = _Resolved->Resource.Handle;
    _CellSize = Vec2{(float)Grid.CellW, (float)Grid.CellH};
    for (i32 cell : Frames) {
        if (_Frames.IsFull()) {
            break;
        }
        _Frames.Push(CellRect(cell));
    }
    return true;
}

i32 SpriteSheetClip::CellCount() const {
    using namespace spritesheet_asset_private;

    if (!_Resolved) {
        return 0;
    }
    const kdk::Texture& res = _Resolved->Resource;
    GridDims dims = ComputeGridDims(Grid, res.Width, res.Height);
    return dims.Cols * dims.Rows;
}

FrameUv SpriteSheetClip::CellRect(i32 cell) const {
    using namespace spritesheet_asset_private;

    FrameUv uv = {};
    if (!_Resolved) {
        return uv;
    }
    if (cell < 0) {
        return uv;
    }
    const kdk::Texture& res = _Resolved->Resource;
    GridDims dims = ComputeGridDims(Grid, res.Width, res.Height);
    if (dims.Cols <= 0) {
        return uv;
    }

    i32 col = cell % dims.Cols;
    i32 row = cell / dims.Cols;
    i32 x = Grid.Margin + col * (Grid.CellW + Grid.Spacing);
    i32 y = Grid.Margin + row * (Grid.CellH + Grid.Spacing);

    uv.Uv0 = Vec2{(float)x / (float)res.Width, (float)y / (float)res.Height};
    uv.Uv1 = Vec2{(float)(x + Grid.CellW) / (float)res.Width, (float)(y + Grid.CellH) / (float)res.Height};
    return uv;
}

i32 SpriteSheetClip::At(float time, float fps, bool loop) const {
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
    return step;
}

Vec2 SpriteSheetClip::PivotOffset(Vec2 draw_size) const {
    // Pivot in [-1,1] measures from the center in half-cells; shifting the quad by -half*pivot moves
    // that point onto the anchor. Component-wise (glm::vec2 * is Hadamard).
    return draw_size * Pivot * -0.5f;
}

ClipEditFields SpriteSheetClip::EditFields() const { return {Grid, FPS, Pivot}; }

void SpriteSheetClip::ApplyEditFields(const ClipEditFields& fields) {
    Grid = fields.Grid;
    FPS = fields.FPS;
    Pivot = fields.Pivot;
}

void SpriteGrid::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, CellW);
    SERDE(sa, this, CellH);
    SERDE(sa, this, Margin);
    SERDE(sa, this, Spacing);
}

void SpriteSheetClip::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, Name);
    SERDE(sa, this, Texture);
    SERDE(sa, this, Grid);
    SERDE(sa, this, Pivot);
    SERDE(sa, this, FPS);
    SERDE(sa, this, Frames);
}

void SpriteSheetClipReference::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, SpriteSheetId);
    SERDE(sa, this, ClipName);
    SERDE(sa, this, FlipX);
}

void SpriteSheetAsset::Serialize(SerdeArchive* sa) {
    SERDE(sa, this, Manifest);
    SERDE(sa, this, Clips);
}

bool SpriteSheetAsset::Create(AssetId id) { return CreateAsset<SpriteSheetAsset>(id); }

std::optional<SpriteSheetAsset> SpriteSheetAsset::LoadFromDisk(AssetId id) {
    return LoadAssetFromDisk<SpriteSheetAsset>(id);
}

bool SpriteSheetAsset::SaveManifest() { return SaveAssetManifest(this); }

bool SpriteSheetAsset::ResolveReferences(AssetRegistry& registry) {
    bool all_resolved = true;
    for (SpriteSheetClip& clip : Clips) {
        if (!clip.Resolve(registry)) {
            LogError("SpriteSheet '%s' clip '%s' references missing texture '%s'",
                     Manifest.Id.Value.Str(),
                     clip.Name.Str(),
                     clip.Texture.Value.Str());
            all_resolved = false;
        }
    }
    return all_resolved;
}

const SpriteSheetClip* SpriteSheetClipReference::Resolve(AssetRegistry& registry) const {
    if (!SpriteSheetId.IsValid()) {
        return nullptr;
    }
    SpriteSheetAsset* sheet = registry.FindSpriteSheetAsset(SpriteSheetId);
    if (!sheet) {
        return nullptr;
    }
    return sheet->FindClip(ClipName);
}

const SpriteSheetClip* SpriteSheetAsset::FindClip(StringView name) const {
    for (const SpriteSheetClip& clip : Clips) {
        if (clip.Name.Equals(name)) {
            return &clip;
        }
    }
    return nullptr;
}

}  // namespace kdk
