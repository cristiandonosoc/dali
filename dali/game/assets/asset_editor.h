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

    // Left-list search filters (case-insensitive substring; empty matches everything). One per list
    // so a filter typed in one tab doesn't carry into another. Every list is also shown sorted
    // alphabetically by short id.
    FixedString<64> DatabaseFilter = {};
    FixedString<64> TextureFilter = {};
    FixedString<64> SheetFilter = {};
    FixedString<64> EnemyFilter = {};
    FixedString<64> TowerFilter = {};

    // Texture creation form scratch. The id is the short, root-relative form (the "textures/" root
    // is prepended on create); Source stays a full raw path.
    FixedString<256> NewSource = "raw/sprites/goblin/D_Walk.png";
    FixedString<128> NewId = "goblin/walk";
    bool NewFlip = false;
    ETextureFilter NewFilter = ETextureFilter::Nearest;

    // SpriteSheet creation form scratch: the short concept id (root prepended on create); textures +
    // clips are added in the inspector.
    FixedString<128> NewSheetId = "goblin";

    // Texture "Batch Import" mode: import every .png in a folder in one shot. The folder is scanned
    // once (on Load/Browse) into BatchFiles and kept in memory; the prefix is proposed as the
    // project-relative folder path and is editable (final id = textures/<prefix>/<stem>).
    static constexpr i32 kMaxBatch = 128;
    FixedString<256> BatchFolder = {};  // source dir, working-dir-relative
    FixedString<128> BatchPrefix = {};  // asset-path prefix
    bool BatchLoaded = false;    // a folder has been scanned (gates the second stage of the view)
    FixedVector<FixedString<128>, kMaxBatch> BatchFiles = {};  // cached .png basenames from the scan
    bool BatchFlip = false;
    ETextureFilter BatchFilter = ETextureFilter::Nearest;
    i32 BatchResultOk = NONE;  // last import tally (NONE == none run since the last Load)
    i32 BatchResultSkip = 0;
    i32 BatchResultFail = 0;

    // Enemy creation form scratch: the short id (root prepended on create); stats edited in the
    // inspector.
    FixedString<128> NewEnemyId = "goblin";
    // Tower creation form scratch: the short id (root prepended on create); stats edited in the
    // inspector.
    FixedString<128> NewTowerId = "arrow";
    // New-clip form scratch (a clip carries its own texture + grid; both edited in the inspector).
    FixedString<64> NewClipName = {};
    AssetId NewClipTexture = {};

    // Spritesheet "Batch Create" mode: turn several textures into clips in one shot. Each selected
    // texture becomes a clip named <prefix><texture-basename>, all sharing one ClipEditFields (grid +
    // fps + pivot). Names that collide with an existing clip are skipped — the batch never overwrites
    // hand-authored frames.
    static constexpr i32 kMaxBatchClips = SpriteSheetAsset::kMaxClips;
    ClipEditFields ClipBatchFields = {};   // grid/fps/pivot applied to every created clip
    FixedString<64> ClipBatchPrefix = {};  // prepended to each clip name (empty by default)
    bool ClipBatchFillFrames = true;       // fill every cell as a frame after creating each clip
    FixedString<64> ClipBatchFilter = {};  // texture multi-select filter
    FixedVector<AssetId, kMaxBatchClips> ClipBatchSelection = {};  // chosen texture ids
    i32 ClipBatchResultOk = NONE;                                  // last create tally (NONE == none run)
    i32 ClipBatchResultSkip = 0;

    // Spritesheet "Batch Edit" mode: multi-select existing clips of the sheet and overwrite their
    // grid/fps/pivot with one shared ClipEditFields. Selection is by clip name (clips have no id).
    ClipEditFields ClipEditTemplate = {};
    FixedString<64> ClipEditFilter = {};                              // clip multi-select filter
    FixedVector<FixedString<64>, kMaxBatchClips> ClipEditSelection = {};  // chosen clip names
    i32 ClipEditResultOk = NONE;                                      // last apply tally (NONE == none run)

    // Clip playback preview (editor-local clock/toggles; playback rate is the clip's own FPS).
    FixedString<64> SelectedClip = {};
    float ClipTime = 0.0f;
    bool ClipLoop = true;
    bool ClipPlaying = true;

    // Inspector state (shared across tabs; interpreted against the current type's holder).
    AssetId Selected = {};
    float PreviewZoom = 4.0f;

    // Enemy inspector's animated preview: which facing to show (steered by WASD while the preview
    // pane holds focus) and its own playback clock.
    EFacing PreviewFacing = EFacing::Down;
    float PreviewTime = 0.0f;

    void Draw(AssetRegistry* registry);
    void DrawDatabaseTab(AssetRegistry* registry);
    void DrawTextureTab(AssetRegistry* registry);
    void DrawTextureListView(AssetRegistry* registry);
    void DrawTextureBatchView(AssetRegistry* registry);
    void DrawSpriteSheetTab(AssetRegistry* registry);
    void DrawEnemyTab(AssetRegistry* registry);
    void DrawTowerTab(AssetRegistry* registry);
};

}  // namespace kdk
