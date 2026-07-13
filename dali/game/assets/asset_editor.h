#pragma once

#include <dali/game/assets/registry.h>

namespace kdk {

// The Assets-tab UI: a per-asset-type view (create form + two-pane list + inspector), switched by
// the secondary type bar. Holds only transient editor scratch (form field buffers, current
// selection); the real data is the registry.
struct AssetEditor {
    // The Database pane is a type-agnostic, read-only overview of every loaded asset; it is the
    // landing pane. When false, a per-type authoring pane (CurrentType) is shown instead. Both are
    // driven by the secondary type bar in game.cpp.
    bool ShowDatabase = true;
    // Which asset type the per-type pane is showing (set by the secondary type bar in game.cpp).
    EAssetType CurrentType = EAssetType::Texture;
    // The asset selected in the Database pane (kept separate from Selected, which the per-type
    // inspectors use, so switching panes doesn't cross-contaminate).
    AssetId DatabaseSelected = {};

    // Git "Verify all" cache (Database pane): the set of assets whose .yml or .asset is dirty per the
    // last global `git status`. Run on demand (a subprocess spawn is slow); one call covers every
    // asset. Both files of an asset collapse to its id, so this is an asset-level set.
    static constexpr i32 kMaxDirtyAssets = 256;
    bool GitChecked = false;    // a global verify has run (GitLaunched/ExitCode/DirtyIds are current)
    bool GitLaunched = false;   // did the git process start
    i32 GitExitCode = 0;
    FixedVector<AssetId, kMaxDirtyAssets> GitDirtyIds = {};

    // Texture creation form scratch. The id is the short, root-relative form (the "textures/" root
    // is prepended on create); Source stays a full raw path.
    char NewSource[256] = "raw/sprites/goblin/D_Walk.png";
    char NewId[128] = "goblin/walk";
    bool NewFlip = false;
    ETextureFilter NewFilter = ETextureFilter::Nearest;

    // Spritesheet creation form scratch: the short concept id (root prepended on create); textures +
    // clips are added in the inspector.
    char NewSheetId[128] = "goblin";

    // Enemy creation form scratch: the short id (root prepended on create); stats edited in the
    // inspector.
    char NewEnemyId[128] = "goblin";
    // New-clip form scratch (a clip carries its own texture + grid; both edited in the inspector).
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

    void Draw(AssetRegistry* registry);
    void DrawDatabaseTab(AssetRegistry* registry);
    void DrawTextureTab(AssetRegistry* registry);
    void DrawSpritesheetTab(AssetRegistry* registry);
    void DrawEnemyTab(AssetRegistry* registry);
};

}  // namespace kdk
