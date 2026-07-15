#include <dali/game/assets/asset_editor.h>

#include <dali/core/algorithm.h>
#include <dali/core/api.h>
#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/core/string.h>
#include <dali/game/file.h>
#include <dali/game/platform_state.h>
#include <dali/game/process.h>

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <cstring>

namespace kdk {

namespace asset_editor_private {

StringView ForwardSlashes(Arena* arena, StringView s) {
    char* buffer = (char*)arena->Push(s.Size + 1).data();
    for (u64 i = 0; i < s.Size; ++i) {
        buffer[i] = (s[i] == '\\') ? '/' : s[i];
    }
    buffer[s.Size] = '\0';
    return StringView(buffer, s.Size);
}

// ImGui::InputText against a FixedString: it edits the backing buffer (kept null-terminated) but has
// no notion of FixedString::Size, so we resync it after an edit. Returns true when the text changed.
template <u64 N>
bool InputTextFixed(const char* label, FixedString<N>& str) {
    bool changed = ImGui::InputText(label, str.StrMutable(), N);
    if (changed) {
        str.Size = (u32)std::strlen(str.StrMutable());
    }
    return changed;
}

// Collects pointers to the ids in |assets| whose short id contains |filter| (case-insensitive),
// sorted alphabetically by short id. The pointers alias the registry (stable for the frame); the
// index array is pushed into |arena|, so the caller MUST keep that arena's scope alive while it
// iterates the result.
template <typename T, i32 N>
std::span<const AssetId*> SortedFilteredIds(Arena* arena,
                                            FixedVector<T, N>& assets,
                                            EAssetType type,
                                            StringView filter) {
    const AssetId** ids = arena->PushArray<const AssetId*>(assets.Size).data();
    i32 count = 0;
    for (T& asset : assets) {
        StringView short_id = ShortId(type, asset.Manifest.Id);
        if (!short_id.ContainsCi(filter)) {
            continue;
        }
        ids[count] = &asset.Manifest.Id;
        count++;
    }
    SortPred(ids, ids + count, [type](const AssetId* a, const AssetId* b) {
        return std::strcmp(ShortId(type, *a).Str(), ShortId(type, *b).Str()) < 0;
    });
    return {ids, (u64)count};
}

// A search box sized to the list column, drawn just above a left list. The hint shows in the empty
// box; |id| must be unique (a hidden "##..." label) so the four boxes don't collide.
template <u64 N>
void DrawFilterBox(const char* id, FixedString<N>& filter, float width) {
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint(id, "filter (substring)...", filter.StrMutable(), N)) {
        filter.Size = (u32)std::strlen(filter.StrMutable());
    }
}

// SortedFilteredIds but dispatched by |type| onto the matching registry holder, so a generic (type-
// erased) picker can list any asset type without knowing the concrete container.
std::span<const AssetId*>
    SortedFilteredIdsForType(Arena* arena, AssetRegistry* registry, EAssetType type, StringView filter) {
    switch (type) {
        case EAssetType::Texture:
            return SortedFilteredIds(arena, registry->TextureAssets, type, filter);
        case EAssetType::SpriteSheet:
            return SortedFilteredIds(arena, registry->SpriteSheetAssets, type, filter);
        case EAssetType::Enemy:
            return SortedFilteredIds(arena, registry->EnemyAssets, type, filter);
        case EAssetType::Tower:
            return SortedFilteredIds(arena, registry->TowerAssets, type, filter);
        case EAssetType::Invalid:
        case EAssetType::COUNT: break;
    }
    return {};
}

// A filterable, alphabetically-sorted picker for one asset type. Renders as a combo whose open popup
// carries a search box at the top — essential now that a type can hold dozens of assets. Writes the
// chosen id into |*selected| and returns true if it changed this frame. |label| both titles the combo
// and scopes its ImGui id (use "Name##uniq" when two pickers of the same type share a window).
bool AssetSelector(const char* label, EAssetType type, AssetRegistry* registry, AssetId* selected) {
    const char* preview = selected->IsValid() ? ShortId(type, *selected).Str() : "(none)";
    if (!ImGui::BeginCombo(label, preview)) {
        return false;
    }

    // One static filter buffer is safe: ImGui only ever has a single combo popup open at once. Reset
    // it (and focus it) when the popup appears, so a query left over from a previous open doesn't
    // silently hide everything the next time around.
    static FixedString<64> filter = {};
    if (ImGui::IsWindowAppearing()) {
        filter.Set("");
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##asset_filter", "filter...", filter.StrMutable(), 64)) {
        filter.Size = (u32)std::strlen(filter.StrMutable());
    }

    bool changed = false;
    auto scratch = Arena::GetScratch();
    for (const AssetId* id : SortedFilteredIdsForType(scratch, registry, type, filter.ToString())) {
        bool is_selected = (*id == *selected);
        if (ImGui::Selectable(ShortId(type, *id).Str(), is_selected)) {
            *selected = *id;
            changed = true;
        }
        if (is_selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
    return changed;
}

// A forward-slashed, lowercased copy for case- and separator-insensitive path comparison: Windows
// paths are case-insensitive and the dialog yields '\', while GetBaseDir may differ in both.
StringView PathCompareKey(Arena* arena, StringView s) {
    char* buffer = (char*)arena->Push(s.Size + 1).data();
    for (u64 i = 0; i < s.Size; ++i) {
        char c = s[i];
        if (c == '\\') {
            c = '/';
        }
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        buffer[i] = c;
    }
    buffer[s.Size] = '\0';
    return StringView(buffer, s.Size);
}

// Converts an absolute path (e.g. from the file dialog) into one relative to the working directory,
// so it can be stored as portable provenance ("raw/sprites/goblin/walk.png"). The compare is case-
// and separator-insensitive and tolerates a trailing slash on the working dir. A path that is
// already relative, or lies outside the working dir, is returned unchanged (forward-slashed).
StringView ToWorkingDirRelative(Arena* arena, StringView path) {
    StringView forward = ForwardSlashes(arena, path);
    if (!paths::IsAbsolute(path)) {
        return forward;
    }

    StringView base = PathCompareKey(arena, paths::GetBaseDir(arena));
    while (base.Size > 0 && base[base.Size - 1] == '/') {
        base = StringView(base.Str(), base.Size - 1);  // drop trailing slash(es)
    }
    StringView key = PathCompareKey(arena, path);

    // Require key == base + '/' + <rest>; anything else is outside the tree.
    if (key.Size <= base.Size) {
        return forward;
    }
    bool inside = (key[base.Size] == '/');
    for (u64 i = 0; i < base.Size; ++i) {
        inside &= (key[i] == base[i]);
    }
    if (!inside) {
        return forward;
    }

    u64 start = base.Size + 1;
    return StringView(forward.Str() + start, forward.Size - start);
}

// Opens the native file dialog for an image and writes the chosen (working-dir-relative) path into
// |dst|. Returns true if a file was chosen.
template <u64 N>
bool BrowseForSource(FixedString<N>& dst) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.OpenFileDialog) {
        return false;
    }

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView chosen = {};
    FileDialogFilter filters[] = {
        {"Images", "png,jpg,jpeg,bmp,tga"},
    };
    if (!ps->API.OpenFileDialog(arena, &chosen, filters)) {
        return false;
    }

    dst.Set(ToWorkingDirRelative(arena, chosen));
    return true;
}

// Suggests an asset id from a raw source path: drops the extension, strips the "raw/" source root,
// and replaces the first (category) directory with the asset type's id root. e.g.
// "raw/sprites/goblin/U_Walk.png" -> "textures/goblin/U_Walk". Case is preserved; the id is
// canonicalized (lowercased) at create time by AssetId::Normalize.
template <u64 N>
void SuggestTextureId(StringView source, FixedString<N>& dst) {
    if (source.IsEmpty()) {
        return;
    }
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    StringView path = ForwardSlashes(arena, source);
    path = paths::RemoveExtension(arena, path);
    path = RemovePrefix(arena, path, "raw/"sv);  // raw is the source root, never part of an id

    // Drop the first (category) directory; the create form prepends the type root itself, so the
    // suggestion is the short, root-relative form ("sprites/goblin/U_Walk" -> "goblin/U_Walk").
    StringView rest = path;
    for (u64 i = 0; i < path.Size; ++i) {
        if (path[i] == '/') {
            rest = StringView(path.Str() + i + 1, path.Size - i - 1);
            break;
        }
    }

    dst.Set(rest);
}

// Opens the folder containing |source| (a working-dir-relative path) in the OS file manager.
void OpenSourceFolder(StringView source) {
    if (source.IsEmpty()) {
        return;
    }
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.OpenContainingFolder) {
        return;
    }
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView absolute = paths::PathJoin(arena, paths::GetBaseDir(arena), source);
    ps->API.OpenContainingFolder(absolute);
}

// Case-insensitive suffix match, so ".png"/".PNG" both count.
bool HasExtensionCi(StringView path, StringView ext) {
    if (path.Size < ext.Size) {
        return false;
    }
    u64 offset = path.Size - ext.Size;
    for (u64 i = 0; i < ext.Size; ++i) {
        char a = path[offset + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

// Opens the native folder dialog and writes the chosen (working-dir-relative) directory into |dst|.
// Returns true if a folder was chosen.
template <u64 N>
bool BrowseForFolder(FixedString<N>& dst) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.OpenFolderDialog) {
        return false;
    }

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView chosen = {};
    if (!ps->API.OpenFolderDialog(arena, &chosen)) {
        return false;
    }

    dst.Set(ToWorkingDirRelative(arena, chosen));
    return true;
}

// (Re)reads editor->BatchFolder and caches the basenames of its top-level .png files (non-recursive)
// into editor->BatchFiles. Listing is done here (once, on Load), never per frame, so the live
// preview that iterates BatchFiles stays cheap.
void ScanBatchFolder(AssetEditor* editor) {
    editor->BatchFiles.Clear();
    if (editor->BatchFolder.IsEmpty()) {
        return;
    }

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView abs =
        paths::PathJoin(arena, paths::GetBaseDir(arena), editor->BatchFolder.ToString());
    std::span<paths::DirEntry> entries = paths::ListDir(arena, abs);
    for (const paths::DirEntry& entry : entries) {
        if (!entry.IsFile()) {
            continue;
        }
        StringView name = paths::GetBasename(arena, entry.Path);
        if (!HasExtensionCi(name, ".png"sv)) {
            continue;
        }
        if (editor->BatchFiles.IsFull()) {
            break;
        }
        editor->BatchFiles.Push(FixedString<128>(name));
    }
}

// Imports each cached .png as textures/<prefix>/<stem>, skipping any id that collides (after
// normalization) with an earlier file in the batch. Tallies onto editor->BatchResult*.
void ImportBatch(AssetEditor* editor, AssetRegistry* registry) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    TextureImportSettings settings = {};
    settings.FlipVertically = editor->BatchFlip;

    FixedVector<AssetId, AssetEditor::kMaxBatch> seen = {};
    i32 ok = 0;
    i32 skip = 0;
    i32 fail = 0;
    AssetId first = {};
    for (FixedString<128>& file : editor->BatchFiles) {
        StringView name = StringView(file.Str());
        StringView stem = paths::RemoveExtension(arena, name);
        StringView short_id = stem;
        if (!editor->BatchPrefix.IsEmpty()) {
            short_id = Printf(arena, "%s/%s", editor->BatchPrefix.Str(), stem.Str());
        }
        AssetId id = AssetIdFromShort(EAssetType::Texture, short_id);
        if (seen.Contains(id)) {
            skip++;
            continue;
        }
        seen.Push(id);

        StringView source =
            ForwardSlashes(arena, Printf(arena, "%s/%s", editor->BatchFolder.Str(), name.Str()));
        if (TextureAsset::Import(source, id, settings, editor->BatchFilter)) {
            registry->LoadTextureAsset(id);
            if (ok == 0) {
                first = id;
            }
            ok++;
        } else {
            fail++;
        }
    }

    editor->BatchResultOk = ok;
    editor->BatchResultSkip = skip;
    editor->BatchResultFail = fail;
    if (ok > 0) {
        editor->Selected = first;  // so a later switch to List lands on a freshly imported texture
    }
}

const char* FilterLabel(ETextureFilter filter) {
    switch (filter) {
        case ETextureFilter::Nearest: return "Nearest";
        case ETextureFilter::Linear: return "Linear";
    }
    return "Nearest";
}

// Returns true if the selection changed this frame.
bool DrawFilterCombo(const char* label, ETextureFilter* filter) {
    bool changed = false;
    if (!ImGui::BeginCombo(label, FilterLabel(*filter))) {
        return changed;
    }
    for (i32 i = 0; i <= (i32)ETextureFilter::Linear; ++i) {
        ETextureFilter option = (ETextureFilter)i;
        bool selected = (*filter == option);
        if (ImGui::Selectable(FilterLabel(option), selected)) {
            *filter = option;
            changed = true;
        }
    }
    ImGui::EndCombo();
    return changed;
}

void DrawInspector(AssetEditor* editor, TextureAsset* tex) {
    Texture& res = tex->Resource;

    ImGui::Text("Id:     %s", tex->Manifest.Id.Value.Str());
    ImGui::Text("Source: %s", tex->Manifest.Source.Str());
    ImGui::Text("Size:   %d x %d  (%d ch)", res.Width, res.Height, res.Channels);
    ImGui::Text("Handle: %u", res.Handle);
    ImGui::Separator();

    // Flip is an import transform (needs Re-import); filter is live GL state, applied immediately.
    ImGui::Checkbox("Flip vertically", &tex->Settings.FlipVertically);
    ETextureFilter filter = res.Filter;
    if (DrawFilterCombo("Filter", &filter)) {
        res.SetFilter(filter);
    }

    if (ImGui::Button("Save (yml)")) {
        tex->SaveManifest();
    }
    ImGui::SameLine();
    if (ImGui::Button("Re-import (rebake)")) {
        tex->Reimport();
    }

    ImGui::SliderFloat("Zoom", &editor->PreviewZoom, 0.5f, 16.0f, "%.1fx");
    ImGui::BeginChild("preview",
                      ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (res.IsValid()) {
        ImVec2 size(res.Width * editor->PreviewZoom, res.Height * editor->PreviewZoom);
        ImGui::Image((ImTextureID)res.ImGuiId(), size);
    }
    ImGui::EndChild();
}

// Grid-overlay frame picker for one clip: draws the clip's texture with cell boundaries on top
// (cells already in the clip highlighted), and appends the clicked cell to the sequence. Returns
// whether a cell was added this frame (the caller re-resolves so the baked frames stay current).
bool DrawClipFramePicker(AssetEditor* editor, SpriteSheetClip& clip) {
    if (!clip._Resolved) {
        ImGui::TextWrapped("Texture '%s' is not resolved.",
                           ShortId(EAssetType::Texture, clip.Texture).Str());
        return false;
    }
    const Texture& res = clip._Resolved->Resource;
    i32 cell_count = clip.CellCount();

    bool changed = false;
    // Fixed, modest height so the picker doesn't eat the whole inspector (and hide the playback
    // preview below it); it scrolls if the image is larger.
    ImGui::BeginChild("frame_picker",
                      ImVec2(0.0f, 300.0f),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (res.IsValid()) {
        float zoom = editor->PreviewZoom;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)res.ImGuiId(), ImVec2(res.Width * zoom, res.Height * zoom));

        bool image_hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImVec2 mouse = ImGui::GetIO().MousePos;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        i32 clicked_cell = NONE;
        for (i32 c = 0; c < cell_count; ++c) {
            FrameUv uv = clip.CellRect(c);
            ImVec2 tl(origin.x + uv.Uv0.x * res.Width * zoom,
                      origin.y + uv.Uv0.y * res.Height * zoom);
            ImVec2 br(origin.x + uv.Uv1.x * res.Width * zoom,
                      origin.y + uv.Uv1.y * res.Height * zoom);
            bool in_clip = clip.Frames.Contains(c);
            ImU32 color = in_clip ? IM_COL32(255, 220, 60, 255) : IM_COL32(255, 255, 255, 90);
            float thickness = in_clip ? 2.0f : 1.0f;
            draw->AddRect(tl, br, color, 0.0f, 0, thickness);

            bool hit = image_hovered;
            hit &= clicked;
            hit &= mouse.x >= tl.x && mouse.x < br.x;
            hit &= mouse.y >= tl.y && mouse.y < br.y;
            if (hit) {
                clicked_cell = c;
            }
        }
        if (clicked_cell != NONE) {
            if (!clip.Frames.IsFull()) {
                clip.Frames.Push(clicked_cell);
                changed = true;
            }
        }
    }
    ImGui::EndChild();
    return changed;
}

// Playback preview for one clip: editor-local fps/loop advance the clip and draw its current baked
// frame. Playback params live here, not on the clip.
void DrawClipPlayback(AssetEditor* editor, SpriteSheetClip& clip) {
    ImGui::SliderFloat("FPS", &editor->ClipFps, 1.0f, 30.0f, "%.0f");
    ImGui::Checkbox("Loop", &editor->ClipLoop);
    ImGui::SameLine();
    if (ImGui::Button(editor->ClipPlaying ? "Pause" : "Play")) {
        editor->ClipPlaying = !editor->ClipPlaying;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        editor->ClipTime = 0.0f;
    }

    if (editor->ClipPlaying) {
        editor->ClipTime += ImGui::GetIO().DeltaTime;
    }

    i32 pos = clip.At(editor->ClipTime, editor->ClipFps, editor->ClipLoop);
    if (pos == NONE) {
        ImGui::TextDisabled("(empty clip)");
        return;
    }
    if (pos >= clip._Frames.Size) {
        ImGui::TextDisabled("(clip not baked - unresolved texture)");
        return;
    }
    FrameUv uv = clip._Frames[pos];
    ImGui::Text("Frame %d (cell %d)", pos, clip.Frames[pos]);
    float zoom = editor->PreviewZoom;
    ImGui::Image((ImTextureID)clip._Handle,
                 ImVec2(clip._CellSize.x * zoom, clip._CellSize.y * zoom),
                 ImVec2(uv.Uv0.x, uv.Uv0.y),
                 ImVec2(uv.Uv1.x, uv.Uv1.y));
}

// The expanded editor for the selected clip: retarget its texture, edit its grid, build its frame
// sequence (grid picker + fill/clear), and preview playback. Any edit re-resolves the clip so the
// baked frames the preview samples stay current.
void DrawClipEditor(AssetEditor* editor, AssetRegistry* registry, SpriteSheetClip& clip) {
    bool dirty = false;

    if (AssetSelector("Texture##CreateClip", EAssetType::Texture, registry, &clip.Texture)) {
        dirty = true;
    }
    ImGui::SameLine();
    if (clip._Resolved) {
        const Texture& res = clip._Resolved->Resource;
        ImGui::TextDisabled("%d x %d", res.Width, res.Height);
    } else {
        ImGui::TextDisabled("(unresolved)");
    }

    dirty |= ImGui::InputInt("Cell W", &clip.Grid.CellW);
    dirty |= ImGui::InputInt("Cell H", &clip.Grid.CellH);
    dirty |= ImGui::InputInt("Margin", &clip.Grid.Margin);
    dirty |= ImGui::InputInt("Spacing", &clip.Grid.Spacing);

    if (ImGui::Button("Fill all frames")) {
        clip.Frames.Clear();
        i32 count = clip.CellCount();
        for (i32 c = 0; c < count; ++c) {
            if (clip.Frames.IsFull()) {
                break;
            }
            clip.Frames.Push(c);
        }
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear frames")) {
        clip.Frames.Clear();
        dirty = true;
    }

    ImGui::SliderFloat("Zoom", &editor->PreviewZoom, 0.5f, 16.0f, "%.1fx");
    ImGui::TextDisabled("Click a cell to append it to the sequence.");
    if (DrawClipFramePicker(editor, clip)) {
        dirty = true;
    }

    if (dirty) {
        clip.Resolve(*registry);
    }

    DrawClipPlayback(editor, clip);
}

// The single-clip authoring tab: add one clip at a time, then pick one to edit its grid/frames.
void DrawSpriteSheetClipTab(AssetEditor* editor,
                            AssetRegistry* registry,
                            SpriteSheetAsset* sheet) {
    // New-clip form: pick a name + a texture from the registry; grid + frames are set in the clip's
    // editor below once it's added.
    InputTextFixed("Name", editor->NewClipName);
    AssetSelector("Texture##SelectClip", EAssetType::Texture, registry, &editor->NewClipTexture);

    bool can_add_clip = !editor->NewClipName.IsEmpty();
    can_add_clip &= editor->NewClipTexture.IsValid();
    ImGui::BeginDisabled(!can_add_clip);
    if (ImGui::Button("Add Clip")) {
        if (!sheet->Clips.IsFull()) {
            SpriteSheetClip clip = {};
            clip.Name = editor->NewClipName;
            clip.Texture = editor->NewClipTexture;
            clip.Resolve(
                *registry);  // link the texture so the frame picker can slice it right away
            sheet->Clips.Push(clip);
            editor->SelectedClip = clip.Name;
            editor->ClipTime = 0.0f;
        }
    }
    ImGui::EndDisabled();

    // Existing clips: a dropdown picks one; its editor + preview render once below, instead of each
    // row expanding inline (which crowded the list with the whole form).
    ImGui::Separator();
    const char* clip_preview =
        editor->SelectedClip.IsEmpty() ? "(choose clip)" : editor->SelectedClip.Str();
    if (ImGui::BeginCombo("Clip", clip_preview)) {
        for (SpriteSheetClip& clip : sheet->Clips) {
            bool selected = clip.Name == editor->SelectedClip;
            if (ImGui::Selectable(clip.Name.Str(), selected)) {
                editor->SelectedClip = clip.Name;
                editor->ClipTime = 0.0f;
            }
        }
        ImGui::EndCombo();
    }

    i32 selected_idx = NONE;
    for (i32 i = 0; i < sheet->Clips.Size; ++i) {
        if (sheet->Clips[i].Name == editor->SelectedClip) {
            selected_idx = i;
            break;
        }
    }
    if (selected_idx == NONE) {
        return;
    }

    SpriteSheetClip& clip = sheet->Clips[selected_idx];
    ImGui::Text("%s  <-  %s  [%d frames]",
                clip.Name.Str(),
                ShortId(EAssetType::Texture, clip.Texture).Str(),
                clip.Frames.Size);
    DrawClipEditor(editor, registry, clip);
    if (ImGui::Button("Remove Clip")) {
        sheet->Clips.RemoveUnorderedAt(selected_idx);
        editor->SelectedClip = {};
    }
}

// The last '/'-separated segment of a short id ("wolf/d_walk" -> "d_walk"); the whole string if it
// has no slash. This is the clip name a texture contributes in batch create.
StringView LastSegment(StringView short_id) {
    u64 start = 0;
    for (u64 i = 0; i < short_id.Size; i++) {
        if (short_id[i] == '/') {
            start = i + 1;
        }
    }
    return StringView(short_id.Str() + start, short_id.Size - start);
}

// The clip name a texture yields in batch create: |prefix| + the texture's basename.
StringView ClipNameForTexture(Arena* arena, StringView prefix, const AssetId& texture) {
    StringView base = LastSegment(ShortId(EAssetType::Texture, texture));
    if (prefix.IsEmpty()) {
        return base;
    }
    return Printf(arena, "%s%s", prefix.Str(), base.Str());
}

// Whether |name| is already in |names| (linear; the batch is small).
bool ClipNameSeen(const FixedVector<FixedString<64>, AssetEditor::kMaxBatchClips>& names,
                  StringView name) {
    for (const FixedString<64>& n : names) {
        if (n.Equals(name)) {
            return true;
        }
    }
    return false;
}

// A grid can carve cells only if both cell dimensions are positive (margin/spacing may be 0).
bool BatchGridIsValid(const SpriteGrid& grid) {
    bool ok = grid.CellW > 0;
    ok &= grid.CellH > 0;
    return ok;
}

// Creates a clip per selected texture, all sharing editor->ClipBatchGrid. Skips names that repeat
// within the batch or already exist in the sheet (never overwrites). Optionally fills every cell as a
// frame. Tallies onto editor->ClipBatchResult*.
void BatchCreateClips(AssetEditor* editor, AssetRegistry* registry, SpriteSheetAsset* sheet) {
    auto scratch = Arena::GetScratch();
    FixedVector<FixedString<64>, AssetEditor::kMaxBatchClips> seen = {};
    i32 ok = 0;
    i32 skip = 0;
    FixedString<64> last_created = {};
    for (const AssetId& texture : editor->ClipBatchSelection) {
        StringView name = ClipNameForTexture(scratch, editor->ClipBatchPrefix.ToString(), texture);
        if (ClipNameSeen(seen, name)) {
            skip++;
            continue;
        }
        seen.Push(FixedString<64>(name));
        if (sheet->FindClip(name)) {
            skip++;
            continue;
        }
        if (sheet->Clips.IsFull()) {
            break;
        }

        SpriteSheetClip clip = {};
        clip.Name = name;
        clip.Texture = texture;
        clip.Grid = editor->ClipBatchGrid;
        clip.Resolve(*registry);  // link the texture so the grid can slice it
        if (editor->ClipBatchFillFrames) {
            i32 count = clip.CellCount();
            for (i32 c = 0; c < count; c++) {
                if (clip.Frames.IsFull()) {
                    break;
                }
                clip.Frames.Push(c);
            }
            clip.Resolve(*registry);  // re-bake now that Frames is populated
        }
        sheet->Clips.Push(clip);
        last_created = clip.Name;
        ok++;
    }

    editor->ClipBatchResultOk = ok;
    editor->ClipBatchResultSkip = skip;
    if (ok > 0) {
        editor->SelectedClip = last_created;  // so switching to the Clip tab lands on a new clip
        editor->ClipTime = 0.0f;
    }
}

// The batch-create tab: pick a shared grid + prefix, multi-select textures, and turn each into a clip
// named after its basename in one shot. Mirrors the texture tab's Batch Import.
void DrawSpriteSheetBatchTab(AssetEditor* editor, AssetRegistry* registry, SpriteSheetAsset* sheet) {
    // Grid + fill applied to every clip this batch creates.
    ImGui::SeparatorText("Grid (applied to all)");
    SpriteGrid& grid = editor->ClipBatchGrid;
    ImGui::InputInt("Cell W", &grid.CellW);
    ImGui::InputInt("Cell H", &grid.CellH);
    ImGui::InputInt("Margin", &grid.Margin);
    ImGui::InputInt("Spacing", &grid.Spacing);
    ImGui::Checkbox("Fill all frames", &editor->ClipBatchFillFrames);

    ImGui::SeparatorText("Naming");
    InputTextFixed("Clip name prefix", editor->ClipBatchPrefix);

    // Texture multi-select: click a row to toggle it in/out of the selection.
    ImGui::SeparatorText("Textures");
    DrawFilterBox("##clipbatch_filter", editor->ClipBatchFilter, 240.0f);
    ImGui::SameLine();
    bool select_all = ImGui::SmallButton("Select all");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        editor->ClipBatchSelection.Clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d selected)", editor->ClipBatchSelection.Size);

    auto scratch = Arena::GetScratch();
    std::span<const AssetId*> tex_ids = SortedFilteredIds(scratch,
                                                          registry->TextureAssets,
                                                          EAssetType::Texture,
                                                          editor->ClipBatchFilter.ToString());

    if (select_all) {
        for (const AssetId* id : tex_ids) {
            if (editor->ClipBatchSelection.IsFull()) {
                break;
            }
            if (!editor->ClipBatchSelection.Contains(*id)) {
                editor->ClipBatchSelection.Push(*id);
            }
        }
    }

    ImGui::BeginChild("clipbatch_textures", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders);
    for (const AssetId* id : tex_ids) {
        bool selected = editor->ClipBatchSelection.Contains(*id);
        if (ImGui::Selectable(ShortId(EAssetType::Texture, *id).Str(), selected)) {
            i32 idx = NONE;
            for (i32 i = 0; i < editor->ClipBatchSelection.Size; i++) {
                if (editor->ClipBatchSelection[i] == *id) {
                    idx = i;
                    break;
                }
            }
            if (idx != NONE) {
                editor->ClipBatchSelection.RemoveUnorderedAt(idx);
            } else if (!editor->ClipBatchSelection.IsFull()) {
                editor->ClipBatchSelection.Push(*id);
            }
        }
    }
    ImGui::EndChild();

    if (editor->ClipBatchSelection.IsEmpty()) {
        ImGui::TextDisabled("Select one or more textures to create clips from.");
        return;
    }

    // Preview: each selected texture -> its resulting clip name, with a status tag. Pure string work.
    ImGui::SeparatorText("Preview");
    FixedVector<FixedString<64>, AssetEditor::kMaxBatchClips> seen = {};
    i32 creatable = 0;
    ImGui::BeginChild("clipbatch_preview", ImVec2(0.0f, 140.0f), ImGuiChildFlags_Borders);
    for (const AssetId& texture : editor->ClipBatchSelection) {
        StringView name = ClipNameForTexture(scratch, editor->ClipBatchPrefix.ToString(), texture);
        const char* tag = "new";
        if (ClipNameSeen(seen, name)) {
            tag = "dup - skipped";
        } else {
            seen.Push(FixedString<64>(name));
            if (sheet->FindClip(name)) {
                tag = "exists - skipped";
            } else {
                tag = "new";
                creatable++;
            }
        }
        ImGui::Text("%s  ->  %s", ShortId(EAssetType::Texture, texture).Str(), name.Str());
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", tag);
    }
    ImGui::EndChild();

    bool grid_ok = BatchGridIsValid(grid);
    if (!grid_ok) {
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f),
                           "Set Cell W and Cell H (> 0) to create clips.");
    }

    char label[64];
    snprintf(label, sizeof(label), "Create %d clips", creatable);
    bool disabled = creatable == 0;
    disabled |= !grid_ok;
    if (disabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(label)) {
        BatchCreateClips(editor, registry, sheet);
    }
    if (disabled) {
        ImGui::EndDisabled();
    }

    if (editor->ClipBatchResultOk != NONE) {
        ImGui::SameLine();
        ImGui::TextDisabled("created %d, skipped %d",
                            editor->ClipBatchResultOk,
                            editor->ClipBatchResultSkip);
    }
}

void DrawSpriteSheetInspector(AssetEditor* editor,
                              AssetRegistry* registry,
                              SpriteSheetAsset* sheet) {
    ImGui::Text("Id: %s", sheet->Manifest.Id.Value.Str());
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        sheet->SaveManifest();
    }

    // Two ways to author clips: one at a time (Clip), or a whole folder of textures at once (Batch).
    if (ImGui::BeginTabBar("sheet_clip_modes")) {
        if (ImGui::BeginTabItem("Clip")) {
            DrawSpriteSheetClipTab(editor, registry, sheet);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Batch Create")) {
            DrawSpriteSheetBatchTab(editor, registry, sheet);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace asset_editor_private

namespace asset_editor_private {

// The manifest of the asset with |id|, found across every holder (ids are unique across types).
// nullptr if no loaded asset has that id. The per-holder scan is the same friction a generic
// asset-type table would collapse; fine at three types.
const AssetManifest* FindManifest(AssetRegistry* registry, AssetId id) {
    if (TextureAsset* tex = registry->FindTextureAsset(id)) {
        return &tex->Manifest;
    }
    if (SpriteSheetAsset* sheet = registry->FindSpriteSheetAsset(id)) {
        return &sheet->Manifest;
    }
    if (EnemyAsset* enemy = registry->FindEnemyAsset(id)) {
        return &enemy->Manifest;
    }
    if (TowerAsset* tower = registry->FindTowerAsset(id)) {
        return &tower->Manifest;
    }
    return nullptr;
}

// The Database detail pane: the common manifest fields, plus the manifest file's last-modified time
// (queried live from disk, UTC).
void DrawAssetMetadata(const AssetManifest* manifest) {
    ImGui::Text("Type:    %s", ToString(manifest->Type).Str());
    ImGui::Text("Id:      %s", manifest->Id.Value.Str());
    ImGui::Text("Version: %d", manifest->Version);
    if (!manifest->Source.IsEmpty()) {
        ImGui::Text("Source:  %s", manifest->Source.Str());
    }
    ImGui::Text("Payload: %s", manifest->HasPayload ? "yes" : "no");

    ImGui::Separator();

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView yml_path = AssetYmlPath(arena, manifest->Id);
    ImGui::TextDisabled("%s", yml_path.Str());

    DateTime dt = {};
    if (GetFileModTime(yml_path, nullptr, &dt)) {  // nullptr: only the calendar date, in local time
        StringView offset_label = "UTC"sv;
        if (dt.UtcOffsetSeconds != 0) {
            char sign = dt.UtcOffsetSeconds > 0 ? '+' : '-';
            i32 abs_seconds = dt.UtcOffsetSeconds < 0 ? -dt.UtcOffsetSeconds : dt.UtcOffsetSeconds;
            offset_label = Printf(arena,
                                  "UTC%c%02d:%02d",
                                  sign,
                                  abs_seconds / 3600,
                                  (abs_seconds % 3600) / 60);
        }
        ImGui::Text("Last modified: %04d-%02d-%02d %02d:%02d:%02d %s",
                    dt.Year,
                    dt.Month,
                    dt.Day,
                    dt.Hour,
                    dt.Minute,
                    dt.Second,
                    offset_label.Str());
    } else {
        ImGui::Text("Last modified: unknown");
    }
}

// Whether |id| is in the last verify's dirty set (linear scan; the set is small).
bool IsGitDirty(const AssetEditor* editor, AssetId id) {
    for (const AssetId& dirty : editor->GitDirtyIds) {
        if (dirty == id) {
            return true;
        }
    }
    return false;
}

// Turns one `git status --porcelain` line ("XY <path>") into the asset it belongs to and records it
// in |dirty|. The path is repo-relative ("assets/textures/goblin/walk.yml"); AssetId::Normalize
// strips the assets/ prefix and the extension, so a dirty .yml and .asset land on the same id.
// Lines that don't map to an asset (or duplicates) are ignored.
void CollectDirtyFromLine(StringView line,
                          FixedVector<AssetId, AssetEditor::kMaxDirtyAssets>* dirty) {
    if (line.Size > 0) {
        if (line[line.Size - 1] == '\r') {
            line = StringView(line.Str(), line.Size - 1);  // strip CR of a CRLF
        }
    }
    if (line.Size < 4) {  // "XY p" is the shortest meaningful line
        return;
    }

    // Porcelain v1: two status chars, a space, then the path. Renames read "orig -> new"; take new.
    StringView path = StringView(line.Str() + 3, line.Size - 3);
    for (u64 i = 0; i + 4 <= path.Size; ++i) {
        bool arrow = path[i] == ' ';
        arrow &= path[i + 1] == '-';
        arrow &= path[i + 2] == '>';
        arrow &= path[i + 3] == ' ';
        if (arrow) {
            path = StringView(path.Str() + i + 4, path.Size - i - 4);
            break;
        }
    }

    AssetId id = AssetId::Normalize(path);
    if (id.Value.ToString().IsEmpty()) {
        return;
    }
    for (const AssetId& existing : *dirty) {  // dedupe (the .yml and .asset both land here)
        if (existing == id) {
            return;
        }
    }
    if (!dirty->IsFull()) {
        dirty->Push(id);
    }
}

// One global `git status --porcelain -- assets`, mapping every dirty path back to its asset id. One
// spawn covers the whole tree - never per-asset, never per-frame. Cached on |editor|.
void RunGitVerifyAll(AssetEditor* editor) {
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;

    StringView repo = ForwardSlashes(arena, paths::GetBaseDir(arena));
    // -C sets git's directory (the contract has no cwd param); the pathspec scopes it to assets/.
    StringView args[] = {
        "git"sv,
        "-C"sv,
        repo,
        "status"sv,
        "--porcelain"sv,
        "--"sv,
        "assets"sv,
    };
    ProcessResult result = RunProcess(arena, std::span<const StringView>(args));

    editor->GitChecked = true;
    editor->GitLaunched = result.Launched;
    editor->GitExitCode = result.ExitCode;
    editor->GitDirtyIds.Clear();
    if (!result.Launched) {
        return;
    }
    if (result.ExitCode != 0) {
        return;
    }

    StringView out = result.Stdout;
    u64 start = 0;
    for (u64 i = 0; i <= out.Size; ++i) {
        bool cut = (i == out.Size);
        if (!cut) {
            cut = (out[i] == '\n');
        }
        if (!cut) {
            continue;
        }
        CollectDirtyFromLine(StringView(out.Str() + start, i - start), &editor->GitDirtyIds);
        start = i + 1;
    }
}

// The Database detail pane's git line: the selected asset's state per the last global verify.
void DrawGitStatus(const AssetEditor* editor, const AssetManifest* manifest) {
    ImGui::Separator();
    if (!editor->GitChecked) {
        ImGui::TextDisabled("Git: press \"Verify all\" above");
        return;
    }
    if (!editor->GitLaunched) {
        ImGui::TextDisabled("Git: unavailable");
        return;
    }
    if (editor->GitExitCode != 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f),
                           "Git: error (exit %d)",
                           editor->GitExitCode);
        return;
    }
    if (IsGitDirty(editor, manifest->Id)) {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.3f, 1.0f), "Git: Modified");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Git: Clean");
    }
}

// One Database list row: dirty rows (per the last verify) are marked "* " and coloured. Selection
// keys on the full id; PushID keeps rows distinct when two types share a short id (e.g. "goblin").
void DrawDatabaseRow(AssetEditor* editor, EAssetType type, AssetId id) {
    ImGui::PushID(id.Value.Str());

    bool dirty = editor->GitChecked;
    dirty &= IsGitDirty(editor, id);

    char label[160];
    snprintf(label, sizeof(label), "%s%s", dirty ? "* " : "", ShortId(type, id).Str());

    if (dirty) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.3f, 1.0f));
    }
    bool selected = (id == editor->DatabaseSelected);
    if (ImGui::Selectable(label, selected)) {
        editor->DatabaseSelected = id;
    }
    if (dirty) {
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

// The enemy inspector: edits the blueprint's InstanceData in place, then Save writes the manifest.
// No preview machinery in v1 — the color swatch is the whole visual.
const char* FacingLabel(EFacing facing) {
    switch (facing) {
        case EFacing::Down: return "Down";
        case EFacing::Up: return "Up";
        case EFacing::Left: return "Left";
        case EFacing::Right: return "Right";
        default: return "?";
    }
}

// A live, animated preview of the enemy's walk clip for one facing. Click the pane to focus it
// (border highlights); while focused, WASD steer the facing so each direction's clip — flip included
// — can be checked without spawning. Focus-gating keeps WASD from leaking into the text fields above.
void DrawEnemyPreview(AssetEditor* editor, const EnemyAsset& enemy, AssetRegistry* registry) {
    ImGui::SeparatorText("Preview");

    ImGui::BeginChild("enemy_preview",
                      ImVec2(0.0f, 180.0f),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);
    bool active = ImGui::IsWindowFocused();
    if (active) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            editor->PreviewFacing = EFacing::Up;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            editor->PreviewFacing = EFacing::Down;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            editor->PreviewFacing = EFacing::Left;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_D)) {
            editor->PreviewFacing = EFacing::Right;
        }
    }

    const DirectionalClip& slot = enemy.PerInstanceData.Walk.Resolve(editor->PreviewFacing);
    const SpriteSheetClip* clip = slot.Clip.Resolve(*registry);
    bool drawable = clip != nullptr;
    if (drawable) {
        drawable &= clip->_Resolved != nullptr;
    }
    i32 pos = NONE;
    if (drawable) {
        pos = clip->At(editor->PreviewTime, clip->FPS, true);
        drawable &= pos != NONE;
        drawable &= pos < clip->_Frames.Size;
    }

    if (drawable) {
        FrameUv uv = clip->_Frames[pos];
        ImVec2 uv0(uv.Uv0.x, uv.Uv0.y);
        ImVec2 uv1(uv.Uv1.x, uv.Uv1.y);
        if (slot.FlipX) {
            float u = uv0.x;
            uv0.x = uv1.x;
            uv1.x = u;
        }
        float zoom = editor->PreviewZoom;
        ImVec2 draw_size(clip->_CellSize.x * zoom, clip->_CellSize.y * zoom);
        // Center the sprite in the pane.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - draw_size.x) * 0.5f,
                                   cursor.y + (avail.y - draw_size.y) * 0.5f));
        ImGui::Image((ImTextureID)clip->_Handle, draw_size, uv0, uv1);
    } else {
        ImGui::TextDisabled("(no baked clip for %s)", FacingLabel(editor->PreviewFacing));
    }
    ImGui::EndChild();

    ImGui::Text("Facing: %s", FacingLabel(editor->PreviewFacing));
    ImGui::SameLine();
    if (active) {
        ImGui::TextDisabled("(WASD active)");
    } else {
        ImGui::TextDisabled("(click pane, then WASD)");
    }
    ImGui::SliderFloat("Zoom##preview", &editor->PreviewZoom, 0.5f, 16.0f, "%.1fx");
}

// One facing's walk slot: pick a sheet, then a clip within it, then whether to mirror it. The
// reference (sheet id + clip name) is stored directly on the slot and resolved live — no relink
// step — so the status line just re-resolves to reflect the current selection. |label| both titles
// the block and scopes the ImGui ids, so the four identical pickers don't collide.
void DrawDirectionalClipPicker(const char* label,
                               DirectionalClip* slot,
                               AssetRegistry* registry,
                               float time) {
    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    SpriteSheetClipReference& ref = slot->Clip;

    if (AssetSelector("Sheet", EAssetType::SpriteSheet, registry, &ref.SpriteSheetId)) {
        ref.ClipName = {};  // the previous clip belonged to the old sheet
    }

    SpriteSheetAsset* sheet = registry->FindSpriteSheetAsset(ref.SpriteSheetId);
    const char* clip_preview = ref.ClipName.IsEmpty() ? "(choose clip)" : ref.ClipName.Str();
    if (ImGui::BeginCombo("Clip", clip_preview)) {
        if (sheet) {
            for (SpriteSheetClip& clip : sheet->Clips) {
                bool selected = clip.Name == ref.ClipName;
                if (ImGui::Selectable(clip.Name.Str(), selected)) {
                    ref.ClipName = clip.Name;
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Checkbox("Flip X", &slot->FlipX);

    const SpriteSheetClip* resolved = ref.Resolve(*registry);
    bool baked = resolved != nullptr;
    if (baked) {
        baked &= resolved->_Resolved != nullptr;
    }
    if (baked) {
        // A small running thumbnail so this direction's clip (mirror included) plays right here,
        // synced to the shared preview clock.
        i32 pos = resolved->At(time, resolved->FPS, true);
        bool has_frame = pos != NONE;
        if (has_frame) {
            has_frame &= pos < resolved->_Frames.Size;
        }
        if (has_frame) {
            FrameUv uv = resolved->_Frames[pos];
            ImVec2 uv0(uv.Uv0.x, uv.Uv0.y);
            ImVec2 uv1(uv.Uv1.x, uv.Uv1.y);
            if (slot->FlipX) {
                float u = uv0.x;
                uv0.x = uv1.x;
                uv1.x = u;
            }
            constexpr float kThumb = 48.0f;
            float aspect =
                resolved->_CellSize.y > 0.0f ? resolved->_CellSize.x / resolved->_CellSize.y : 1.0f;
            ImGui::Image((ImTextureID)resolved->_Handle, ImVec2(kThumb * aspect, kThumb), uv0, uv1);
        }
    } else if (ref.SpriteSheetId.IsValid()) {
        ImGui::TextDisabled("unresolved");
    }
    ImGui::PopID();
}

void DrawEnemyInspector(AssetEditor* editor, EnemyAsset* enemy, AssetRegistry* registry) {
    ImGui::Text("Id: %s", enemy->Manifest.Id.Value.Str());
    ImGui::Separator();

    // One shared clock for every preview in this inspector (the four picker thumbnails and the big
    // steerable pane), advanced once here so they all play in sync.
    editor->PreviewTime += ImGui::GetIO().DeltaTime;

    EnemyAsset::InstanceData& data = enemy->PerInstanceData;
    ImGui::DragFloat("Speed", &data.Speed, 1.0f, 0.0f, 1000.0f);
    ImGui::DragFloat("Max Health", &data.MaxHealth, 1.0f, 1.0f, 100000.0f);
    ImGui::DragFloat("Damage", &data.Damage, 0.5f, 0.0f, 100000.0f);
    ImGui::DragInt("Reward", &data.Reward, 1.0f, 0, 100000);

    float rgba[4] = {
        data.Color.R / 255.0f,
        data.Color.G / 255.0f,
        data.Color.B / 255.0f,
        data.Color.A / 255.0f,
    };
    if (ImGui::ColorEdit4("Color", rgba)) {
        data.Color.R = (u8)(rgba[0] * 255.0f + 0.5f);
        data.Color.G = (u8)(rgba[1] * 255.0f + 0.5f);
        data.Color.B = (u8)(rgba[2] * 255.0f + 0.5f);
        data.Color.A = (u8)(rgba[3] * 255.0f + 0.5f);
    }

    // Walk animation: one clip slot per facing. A side-view sheet can serve Left and Right by pointing
    // both at the same clip and ticking Flip X on one.
    ImGui::SeparatorText("Walk");
    WalkClips& walk = enemy->PerInstanceData.Walk;
    DrawDirectionalClipPicker("Down", &walk.ByFacing[(i32)EFacing::Down], registry, editor->PreviewTime);
    DrawDirectionalClipPicker("Up", &walk.ByFacing[(i32)EFacing::Up], registry, editor->PreviewTime);
    DrawDirectionalClipPicker("Left", &walk.ByFacing[(i32)EFacing::Left], registry, editor->PreviewTime);
    DrawDirectionalClipPicker("Right", &walk.ByFacing[(i32)EFacing::Right], registry, editor->PreviewTime);

    DrawEnemyPreview(editor, *enemy, registry);

    ImGui::Separator();
    if (ImGui::Button("Save")) {
        enemy->SaveManifest();
    }
}

// Picks the sprite sheet + clip for a lone clip reference (no mirror), with a small running thumbnail
// synced to |time|. Like DrawDirectionalClipPicker but for a plain SpriteSheetClipReference — e.g. a
// tower's idle animation, which has no facing or flip. |label| titles the block and scopes the ids.
void DrawClipReferencePicker(const char* label,
                             SpriteSheetClipReference* ref,
                             AssetRegistry* registry,
                             float time,
                             float zoom) {
    ImGui::PushID(label);
    ImGui::SeparatorText(label);

    if (AssetSelector("Sheet", EAssetType::SpriteSheet, registry, &ref->SpriteSheetId)) {
        ref->ClipName = {};  // the previous clip belonged to the old sheet
    }

    SpriteSheetAsset* sheet = registry->FindSpriteSheetAsset(ref->SpriteSheetId);
    const char* clip_preview = ref->ClipName.IsEmpty() ? "(choose clip)" : ref->ClipName.Str();
    if (ImGui::BeginCombo("Clip", clip_preview)) {
        if (sheet) {
            for (SpriteSheetClip& clip : sheet->Clips) {
                bool selected = clip.Name == ref->ClipName;
                if (ImGui::Selectable(clip.Name.Str(), selected)) {
                    ref->ClipName = clip.Name;
                }
            }
        }
        ImGui::EndCombo();
    }

    const SpriteSheetClip* resolved = ref->Resolve(*registry);
    bool baked = resolved != nullptr;
    if (baked) {
        baked &= resolved->_Resolved != nullptr;
    }
    if (baked) {
        i32 pos = resolved->At(time, resolved->FPS, true);
        bool has_frame = pos != NONE;
        if (has_frame) {
            has_frame &= pos < resolved->_Frames.Size;
        }
        if (has_frame) {
            FrameUv uv = resolved->_Frames[pos];
            ImVec2 uv0(uv.Uv0.x, uv.Uv0.y);
            ImVec2 uv1(uv.Uv1.x, uv.Uv1.y);
            ImVec2 draw_size(resolved->_CellSize.x * zoom, resolved->_CellSize.y * zoom);
            ImGui::Image((ImTextureID)resolved->_Handle, draw_size, uv0, uv1);
        }
    } else if (ref->SpriteSheetId.IsValid()) {
        ImGui::TextDisabled("unresolved");
    }
    ImGui::PopID();
}

void DrawTowerInspector(AssetEditor* editor, TowerAsset* tower, AssetRegistry* registry) {
    ImGui::Text("Id: %s", tower->Manifest.Id.Value.Str());
    ImGui::Separator();

    // Advance the shared preview clock once so the idle thumbnail animates.
    editor->PreviewTime += ImGui::GetIO().DeltaTime;

    TowerAsset::InstanceData& data = tower->PerInstanceData;
    ImGui::DragFloat("Range", &data.Range, 1.0f, 0.0f, 100000.0f);
    ImGui::DragFloat("Fire Interval", &data.FireInterval, 0.05f, 0.0f, 100000.0f, "%.2f s");
    ImGui::DragFloat("Damage", &data.Damage, 0.5f, 0.0f, 100000.0f);
    ImGui::DragFloat("Projectile Hit Radius", &data.ProjectileHitRadius, 0.5f, 0.0f, 100000.0f);
    ImGui::DragInt("Cost", &data.Cost, 1.0f, 0, 100000);

    // Idle animation: a single clip played while the tower waits (no facing, no mirror).
    DrawClipReferencePicker("Idle", &data.IdleClip, registry, editor->PreviewTime, editor->PreviewZoom);
    ImGui::SliderFloat("Zoom##idle", &editor->PreviewZoom, 0.5f, 16.0f, "%.1fx");

    ImGui::Separator();
    if (ImGui::Button("Save")) {
        tower->SaveManifest();
    }
}

}  // namespace asset_editor_private

void AssetEditor::Draw(AssetRegistry* registry) {
    if (ShowDatabase) {
        DrawDatabaseTab(registry);
        return;
    }
    switch (CurrentType) {
        case EAssetType::Texture: {
            DrawTextureTab(registry);
            break;
        }
        case EAssetType::SpriteSheet: {
            DrawSpriteSheetTab(registry);
            break;
        }
        case EAssetType::Enemy: {
            DrawEnemyTab(registry);
            break;
        }
        case EAssetType::Tower: {
            DrawTowerTab(registry);
            break;
        }
        default: {
            break;
        }
    }
}

void AssetEditor::DrawDatabaseTab(AssetRegistry* registry) {
    using namespace asset_editor_private;

    ImGui::SeparatorText("Asset Database");
    if (ImGui::Button("Rescan assets/")) {
        registry->CrawlAndLoad();
    }
    ImGui::SameLine();
    if (ImGui::Button("Verify all (git)")) {
        RunGitVerifyAll(this);
    }
    bool show_count = GitChecked;
    show_count &= GitLaunched;
    show_count &= (GitExitCode == 0);
    if (show_count) {
        ImGui::SameLine();
        ImGui::TextDisabled("modified: %d", GitDirtyIds.Size);
    }
    ImGui::Separator();

    // List pane, grouped by type. Selecting a row records its id; the detail pane resolves it. Each
    // group is filtered by the shared box and shown alphabetically. One scratch scope spans all three
    // loops, so their id arrays stack safely and stay alive through the rendering below.
    DrawFilterBox("##db_filter", DatabaseFilter, 300.0f);
    ImGui::BeginChild("db_list", ImVec2(300.0f, 0.0f), ImGuiChildFlags_Borders);
    auto scratch = Arena::GetScratch();
    StringView filter = DatabaseFilter.ToString();

    // Rows show the short (root-relative) id since the group already names the type.
    ImGui::SeparatorText("Textures");
    for (const AssetId* id : SortedFilteredIds(scratch, registry->TextureAssets, EAssetType::Texture, filter)) {
        DrawDatabaseRow(this, EAssetType::Texture, *id);
    }

    ImGui::SeparatorText("SpriteSheets");
    for (const AssetId* id :
         SortedFilteredIds(scratch, registry->SpriteSheetAssets, EAssetType::SpriteSheet, filter)) {
        DrawDatabaseRow(this, EAssetType::SpriteSheet, *id);
    }

    ImGui::SeparatorText("Enemies");
    for (const AssetId* id : SortedFilteredIds(scratch, registry->EnemyAssets, EAssetType::Enemy, filter)) {
        DrawDatabaseRow(this, EAssetType::Enemy, *id);
    }

    ImGui::SeparatorText("Towers");
    for (const AssetId* id : SortedFilteredIds(scratch, registry->TowerAssets, EAssetType::Tower, filter)) {
        DrawDatabaseRow(this, EAssetType::Tower, *id);
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Detail pane: the selected asset's metadata + last-modified time.
    ImGui::BeginChild("db_detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (const AssetManifest* manifest = FindManifest(registry, DatabaseSelected)) {
        DrawAssetMetadata(manifest);
        DrawGitStatus(this, manifest);
    } else {
        ImGui::TextWrapped("Select an asset to see its metadata.");
    }
    ImGui::EndChild();
}

void AssetEditor::DrawTextureTab(AssetRegistry* registry) {
    // Two modes: the per-texture List (create + inspect) and Batch Import (a whole folder at once).
    if (ImGui::BeginTabBar("texture_modes")) {
        if (ImGui::BeginTabItem("List")) {
            DrawTextureListView(registry);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Batch Import")) {
            DrawTextureBatchView(registry);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void AssetEditor::DrawTextureListView(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Creation form.
    ImGui::SeparatorText("Create Texture");
    InputTextFixed("Source (raw)", NewSource);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        if (BrowseForSource(NewSource)) {
            SuggestTextureId(NewSource.ToString(), NewId);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Dir")) {
        OpenSourceFolder(NewSource.ToString());
    }
    InputTextFixed("Output id", NewId);

    // The field is root-relative; the tab's type supplies the root. The preview shows the full
    // canonical id the create will actually use.
    AssetId normalized = AssetIdFromShort(EAssetType::Texture, NewId.ToString());
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    ImGui::Checkbox("Flip vertically", &NewFlip);
    DrawFilterCombo("Filter", &NewFilter);

    if (ImGui::Button("Create / Re-import")) {
        TextureImportSettings settings = {};
        settings.FlipVertically = NewFlip;
        if (TextureAsset::Import(NewSource.ToString(), normalized, settings, NewFilter)) {
            registry->LoadTextureAsset(normalized);
            Selected = normalized;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan assets/")) {
        registry->CrawlAndLoad();
    }

    ImGui::Separator();

    // List pane.
    DrawFilterBox("##texture_filter", TextureFilter, 240.0f);
    ImGui::BeginChild("list", ImVec2(240.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Textures (%d)", registry->TextureAssets.Size);
    auto scratch = Arena::GetScratch();
    for (const AssetId* id :
         SortedFilteredIds(scratch, registry->TextureAssets, EAssetType::Texture, TextureFilter.ToString())) {
        bool selected = (*id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::Texture, *id).Str(), selected)) {
            Selected = *id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (TextureAsset* tex = registry->FindTextureAsset(Selected)) {
        DrawInspector(this, tex);
    } else {
        ImGui::TextWrapped("Select a texture, or create one above.");
    }
    ImGui::EndChild();
}

void AssetEditor::DrawTextureBatchView(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Stage 1: choose a folder. Browse fills the path; Browse or Load then scans it into memory.
    ImGui::SeparatorText("Source folder");
    InputTextFixed("Folder (raw)", BatchFolder);
    ImGui::SameLine();
    bool load = false;
    if (ImGui::Button("Browse...")) {
        load = BrowseForFolder(BatchFolder);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        load = true;
    }

    if (load) {
        // Propose the prefix as the project-relative folder path; the user edits it to taste.
        BatchPrefix.Set(BatchFolder.ToString());
        ScanBatchFolder(this);
        BatchLoaded = true;
        BatchResultOk = NONE;  // a fresh load invalidates the previous tally
    }

    if (!BatchLoaded) {
        ImGui::TextDisabled("Pick a folder and Load to list its .png files.");
        return;
    }

    // Stage 2: edit the prefix + settings, preview the resulting ids, import.
    ImGui::Separator();
    InputTextFixed("Asset path prefix", BatchPrefix);
    ImGui::Checkbox("Flip vertically", &BatchFlip);
    DrawFilterCombo("Filter", &BatchFilter);

    if (BatchFiles.IsEmpty()) {
        ImGui::TextDisabled("(no .png found in %s)", BatchFolder.Str());
        return;
    }

    // Live preview over the cached list: compute each file's final id, flag duplicates (same id
    // after normalization) and ids that already exist (a re-import). Pure string work — cheap.
    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    FixedVector<AssetId, kMaxBatch> seen = {};
    i32 importable = 0;

    ImGui::BeginChild("batch_preview", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders);
    for (FixedString<128>& file : BatchFiles) {
        StringView name = StringView(file.Str());
        StringView stem = paths::RemoveExtension(arena, name);
        StringView short_id = stem;
        if (!BatchPrefix.IsEmpty()) {
            short_id = Printf(arena, "%s/%s", BatchPrefix.Str(), stem.Str());
        }
        AssetId id = AssetIdFromShort(EAssetType::Texture, short_id);

        bool dup = seen.Contains(id);
        const char* tag = "new";
        if (dup) {
            tag = "dup - skipped";
        } else {
            bool exists = registry->FindTextureAsset(id) != nullptr;
            tag = exists ? "exists (re-import)" : "new";
            seen.Push(id);
            importable++;
        }
        ImGui::Text("%s  ->  %s", name.Str(), id.Value.Str());
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", tag);
    }
    ImGui::EndChild();

    char label[64];
    snprintf(label, sizeof(label), "Import %d textures", importable);
    bool disabled = importable == 0;
    if (disabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(label)) {
        ImportBatch(this, registry);
    }
    if (disabled) {
        ImGui::EndDisabled();
    }

    if (BatchResultOk != NONE) {
        ImGui::SameLine();
        ImGui::TextDisabled("imported %d, skipped %d, failed %d",
                            BatchResultOk,
                            BatchResultSkip,
                            BatchResultFail);
    }
}

void AssetEditor::DrawSpriteSheetTab(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Creation form: a spritesheet is a concept, created from just its id. Textures and clips are
    // added in the inspector.
    ImGui::SeparatorText("Create SpriteSheet");

    InputTextFixed("Output id", NewSheetId);
    AssetId normalized = AssetIdFromShort(EAssetType::SpriteSheet, NewSheetId.ToString());
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    if (ImGui::Button("Create")) {
        if (SpriteSheetAsset::Create(normalized)) {
            registry->LoadSpriteSheetAsset(normalized);
            registry->ResolveReferences();
            Selected = normalized;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan assets/")) {
        registry->CrawlAndLoad();
    }

    ImGui::Separator();

    // List pane.
    DrawFilterBox("##sheet_filter", SheetFilter, 240.0f);
    ImGui::BeginChild("sheet_list", ImVec2(240.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("SpriteSheets (%d)", registry->SpriteSheetAssets.Size);
    auto scratch = Arena::GetScratch();
    for (const AssetId* id :
         SortedFilteredIds(scratch, registry->SpriteSheetAssets, EAssetType::SpriteSheet, SheetFilter.ToString())) {
        bool selected = (*id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::SpriteSheet, *id).Str(), selected)) {
            Selected = *id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("sheet_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (SpriteSheetAsset* sheet = registry->FindSpriteSheetAsset(Selected)) {
        DrawSpriteSheetInspector(this, registry, sheet);
    } else {
        ImGui::TextWrapped("Select a spritesheet, or create one above.");
    }
    ImGui::EndChild();
}

void AssetEditor::DrawEnemyTab(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Creation form: a blueprint is created from just its id; stats are edited in the inspector.
    ImGui::SeparatorText("Create Enemy");

    InputTextFixed("Output id", NewEnemyId);
    AssetId normalized = AssetIdFromShort(EAssetType::Enemy, NewEnemyId.ToString());
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    if (ImGui::Button("Create")) {
        if (EnemyAsset::Create(normalized)) {
            registry->LoadEnemyAsset(normalized);
            Selected = normalized;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan assets/")) {
        registry->CrawlAndLoad();
    }

    ImGui::Separator();

    // List pane.
    DrawFilterBox("##enemy_filter", EnemyFilter, 240.0f);
    ImGui::BeginChild("enemy_list", ImVec2(240.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Enemies (%d)", registry->EnemyAssets.Size);
    auto scratch = Arena::GetScratch();
    for (const AssetId* id :
         SortedFilteredIds(scratch, registry->EnemyAssets, EAssetType::Enemy, EnemyFilter.ToString())) {
        bool selected = (*id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::Enemy, *id).Str(), selected)) {
            Selected = *id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("enemy_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (EnemyAsset* enemy = registry->FindEnemyAsset(Selected)) {
        DrawEnemyInspector(this, enemy, registry);
    } else {
        ImGui::TextWrapped("Select an enemy, or create one above.");
    }
    ImGui::EndChild();
}

void AssetEditor::DrawTowerTab(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Creation form: a blueprint is created from just its id; stats are edited in the inspector.
    ImGui::SeparatorText("Create Tower");

    InputTextFixed("Output id", NewTowerId);
    AssetId normalized = AssetIdFromShort(EAssetType::Tower, NewTowerId.ToString());
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    if (ImGui::Button("Create")) {
        if (TowerAsset::Create(normalized)) {
            registry->LoadTowerAsset(normalized);
            Selected = normalized;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan assets/")) {
        registry->CrawlAndLoad();
    }

    ImGui::Separator();

    // List pane.
    DrawFilterBox("##tower_filter", TowerFilter, 240.0f);
    ImGui::BeginChild("tower_list", ImVec2(240.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Towers (%d)", registry->TowerAssets.Size);
    auto scratch = Arena::GetScratch();
    for (const AssetId* id :
         SortedFilteredIds(scratch, registry->TowerAssets, EAssetType::Tower, TowerFilter.ToString())) {
        bool selected = (*id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::Tower, *id).Str(), selected)) {
            Selected = *id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("tower_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (TowerAsset* tower = registry->FindTowerAsset(Selected)) {
        DrawTowerInspector(this, tower, registry);
    } else {
        ImGui::TextWrapped("Select a tower, or create one above.");
    }
    ImGui::EndChild();
}

}  // namespace kdk
