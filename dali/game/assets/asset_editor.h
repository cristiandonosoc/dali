#pragma once

#include <dali/game/assets/registry.h>

namespace kdk {

// The Assets-tab UI: a per-asset-type view (create form + two-pane list + inspector), switched by
// the secondary type bar. Holds only transient editor scratch (form field buffers, current
// selection); the real data is the registry.
struct AssetEditor {
    // Which asset type the tab is showing (set by the secondary type bar in game.cpp).
    EAssetType CurrentType = EAssetType::Texture;

    // Texture creation form scratch.
    char NewSource[256] = "raw/sprites/goblin/D_Walk.png";
    char NewId[128] = "textures/goblin/walk";
    bool NewFlip = false;
    ETextureFilter NewFilter = ETextureFilter::Nearest;

    // Spritesheet creation form scratch (just the concept id; textures + clips are added in the
    // inspector).
    char NewSheetId[128] = "spritesheets/goblin";
    // Inspector scratch: which ref to preview, and the new-clip form.
    AssetId SelectedRef = {};
    char NewClipName[64] = "";
    AssetId NewClipTexture = {};

    // Clip playback preview (editor-local; deliberately not stored on the clip).
    FixedString<64> SelectedClip = {};
    float ClipTime = 0.0f;
    float ClipFps = 12.0f;
    bool ClipLoop = true;
    bool ClipPlaying = true;

    // Inspector state (shared across tabs; interpreted against the current type's holder).
    AssetId Selected = {};
    float PreviewZoom = 4.0f;
    i32 PreviewFrame = 0;  // Spritesheet inspector: the frame highlighted in the grid overlay.

    void Draw(AssetRegistry* registry);
    void DrawTextureTab(AssetRegistry* registry);
    void DrawSpritesheetTab(AssetRegistry* registry);
};

}  // namespace kdk
