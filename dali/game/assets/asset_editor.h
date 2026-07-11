#pragma once

#include <dali/game/assets/registry.h>

namespace kdk {

// The Assets-tab UI: a per-type creation form on top, then a two-pane list + inspector. Holds only
// transient editor scratch (form field buffers, current selection); the real data is the registry.
struct AssetEditor {
    // Texture creation form scratch.
    char NewSource[256] = "raw/sprites/goblin/D_Walk.png";
    char NewId[128] = "textures/goblin/walk";
    bool NewFlip = false;
    ETextureFilter NewFilter = ETextureFilter::Nearest;

    // Inspector state.
    AssetId Selected = {};
    float PreviewZoom = 4.0f;

    void Draw(AssetRegistry* registry);
};

}  // namespace kdk
