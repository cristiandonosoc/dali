#include <dali/game/assets/asset_editor.h>

#include <dali/core/api.h>
#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/game/file.h>
#include <dali/game/platform_state.h>
#include <dali/game/process.h>

#include <imgui.h>

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

// Copies |s| into a fixed char buffer of |cap| bytes, truncating and null-terminating.
void CopyToBuffer(StringView s, char* dst, u64 cap) {
    u64 count = s.Size < cap - 1 ? s.Size : cap - 1;
    std::memcpy(dst, s.Str(), count);
    dst[count] = '\0';
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
// |dst| (a fixed char buffer of |cap| bytes). Returns true if a file was chosen.
bool BrowseForSource(char* dst, u64 cap) {
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

    CopyToBuffer(ToWorkingDirRelative(arena, chosen), dst, cap);
    return true;
}

// Suggests an asset id from a raw source path: drops the extension, strips the "raw/" source root,
// and replaces the first (category) directory with the asset type's id root. e.g.
// "raw/sprites/goblin/U_Walk.png" -> "textures/goblin/U_Walk". Case is preserved; the id is
// canonicalized (lowercased) at create time by AssetId::Normalize.
void SuggestTextureId(StringView source, char* dst, u64 cap) {
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

    CopyToBuffer(rest, dst, cap);
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

    const char* tex_preview = clip.Texture.IsValid()
                                  ? ShortId(EAssetType::Texture, clip.Texture).Str()
                                  : "(choose texture)";
    if (ImGui::BeginCombo("Texture##CreateClip", tex_preview)) {
        for (TextureAsset& tex : registry->TextureAssets) {
            bool selected = tex.Manifest.Id == clip.Texture;
            if (ImGui::Selectable(ShortId(EAssetType::Texture, tex.Manifest.Id).Str(), selected)) {
                clip.Texture = tex.Manifest.Id;
                dirty = true;
            }
        }
        ImGui::EndCombo();
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

void DrawSpriteSheetInspector(AssetEditor* editor,
                              AssetRegistry* registry,
                              SpriteSheetAsset* sheet) {
    ImGui::Text("Id: %s", sheet->Manifest.Id.Value.Str());
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        sheet->SaveManifest();
    }

    // --- Clips (each carries its own texture + grid) ---
    ImGui::SeparatorText("Clips");

    // New-clip form: pick a name + a texture from the registry; grid + frames are set in the clip's
    // editor below once it's added.
    ImGui::InputText("Name", editor->NewClipName, sizeof(editor->NewClipName));
    const char* clip_tex_preview = editor->NewClipTexture.IsValid()
                                       ? ShortId(EAssetType::Texture, editor->NewClipTexture).Str()
                                       : "(choose texture)";
    if (ImGui::BeginCombo("Texture##SelectClip", clip_tex_preview)) {
        for (TextureAsset& tex : registry->TextureAssets) {
            bool selected = tex.Manifest.Id == editor->NewClipTexture;
            if (ImGui::Selectable(ShortId(EAssetType::Texture, tex.Manifest.Id).Str(), selected)) {
                editor->NewClipTexture = tex.Manifest.Id;
            }
        }
        ImGui::EndCombo();
    }

    bool can_add_clip = editor->NewClipName[0] != '\0';
    can_add_clip &= editor->NewClipTexture.IsValid();
    ImGui::BeginDisabled(!can_add_clip);
    if (ImGui::Button("Add Clip")) {
        if (!sheet->Clips.IsFull()) {
            SpriteSheetClip clip = {};
            clip.Name = StringView(editor->NewClipName);
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

}  // namespace asset_editor_private

namespace asset_editor_private {

// The manifest of the asset with |id|, found across every holder (ids are unique across types).
// nullptr if no loaded asset has that id. The per-holder scan is the same friction a generic
// asset-type table would collapse; fine at three types.
const AssetManifest* FindManifest(AssetRegistry* registry, AssetId id) {
    if (TextureAsset* tex = registry->FindTexture(id)) {
        return &tex->Manifest;
    }
    if (SpriteSheetAsset* sheet = registry->FindSpriteSheet(id)) {
        return &sheet->Manifest;
    }
    if (EnemyAsset* enemy = registry->FindEnemyBlueprint(id)) {
        return &enemy->Manifest;
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

    const char* sheet_preview = ref.SpriteSheetId.IsValid()
                                    ? ShortId(EAssetType::SpriteSheet, ref.SpriteSheetId).Str()
                                    : "(none)";
    if (ImGui::BeginCombo("Sheet", sheet_preview)) {
        for (SpriteSheetAsset& sheet : registry->SpriteSheetAssets) {
            bool selected = sheet.Manifest.Id == ref.SpriteSheetId;
            if (ImGui::Selectable(ShortId(EAssetType::SpriteSheet, sheet.Manifest.Id).Str(),
                                  selected)) {
                ref.SpriteSheetId = sheet.Manifest.Id;
                ref.ClipName = {};
            }
        }
        ImGui::EndCombo();
    }

    SpriteSheetAsset* sheet = registry->FindSpriteSheet(ref.SpriteSheetId);
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

    // List pane, grouped by type. Selecting a row records its id; the detail pane resolves it.
    ImGui::BeginChild("db_list", ImVec2(300.0f, 0.0f), ImGuiChildFlags_Borders);

    // Rows show the short (root-relative) id since the group already names the type.
    ImGui::SeparatorText("Textures");
    for (TextureAsset& tex : registry->TextureAssets) {
        DrawDatabaseRow(this, EAssetType::Texture, tex.Manifest.Id);
    }

    ImGui::SeparatorText("SpriteSheets");
    for (SpriteSheetAsset& sheet : registry->SpriteSheetAssets) {
        DrawDatabaseRow(this, EAssetType::SpriteSheet, sheet.Manifest.Id);
    }

    ImGui::SeparatorText("Enemies");
    for (EnemyAsset& enemy : registry->EnemyAssets) {
        DrawDatabaseRow(this, EAssetType::Enemy, enemy.Manifest.Id);
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
    using namespace asset_editor_private;

    // Creation form.
    ImGui::SeparatorText("Create Texture");
    ImGui::InputText("Source (raw)", NewSource, sizeof(NewSource));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        if (BrowseForSource(NewSource, sizeof(NewSource))) {
            SuggestTextureId(StringView(NewSource), NewId, sizeof(NewId));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Dir")) {
        OpenSourceFolder(StringView(NewSource));
    }
    ImGui::InputText("Output id", NewId, sizeof(NewId));

    // The field is root-relative; the tab's type supplies the root. The preview shows the full
    // canonical id the create will actually use.
    AssetId normalized = AssetIdFromShort(EAssetType::Texture, StringView(NewId));
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    ImGui::Checkbox("Flip vertically", &NewFlip);
    DrawFilterCombo("Filter", &NewFilter);

    if (ImGui::Button("Create / Re-import")) {
        TextureImportSettings settings = {};
        settings.FlipVertically = NewFlip;
        if (TextureAsset::Import(StringView(NewSource), normalized, settings, NewFilter)) {
            registry->LoadTexture(normalized);
            Selected = normalized;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan assets/")) {
        registry->CrawlAndLoad();
    }

    ImGui::Separator();

    // List pane.
    ImGui::BeginChild("list", ImVec2(240.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Textures (%d)", registry->TextureAssets.Size);
    for (TextureAsset& tex : registry->TextureAssets) {
        bool selected = (tex.Manifest.Id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::Texture, tex.Manifest.Id).Str(), selected)) {
            Selected = tex.Manifest.Id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (TextureAsset* tex = registry->FindTexture(Selected)) {
        DrawInspector(this, tex);
    } else {
        ImGui::TextWrapped("Select a texture, or create one above.");
    }
    ImGui::EndChild();
}

void AssetEditor::DrawSpriteSheetTab(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Creation form: a spritesheet is a concept, created from just its id. Textures and clips are
    // added in the inspector.
    ImGui::SeparatorText("Create SpriteSheet");

    ImGui::InputText("Output id", NewSheetId, sizeof(NewSheetId));
    AssetId normalized = AssetIdFromShort(EAssetType::SpriteSheet, StringView(NewSheetId));
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    if (ImGui::Button("Create")) {
        if (SpriteSheetAsset::Create(normalized)) {
            registry->LoadSpriteSheet(normalized);
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
    ImGui::BeginChild("sheet_list", ImVec2(240.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("SpriteSheets (%d)", registry->SpriteSheetAssets.Size);
    for (SpriteSheetAsset& sheet : registry->SpriteSheetAssets) {
        bool selected = (sheet.Manifest.Id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::SpriteSheet, sheet.Manifest.Id).Str(),
                              selected)) {
            Selected = sheet.Manifest.Id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("sheet_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (SpriteSheetAsset* sheet = registry->FindSpriteSheet(Selected)) {
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

    ImGui::InputText("Output id", NewEnemyId, sizeof(NewEnemyId));
    AssetId normalized = AssetIdFromShort(EAssetType::Enemy, StringView(NewEnemyId));
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    if (ImGui::Button("Create")) {
        if (EnemyAsset::Create(normalized)) {
            registry->LoadEnemyBlueprint(normalized);
            Selected = normalized;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan assets/")) {
        registry->CrawlAndLoad();
    }

    ImGui::Separator();

    // List pane.
    ImGui::BeginChild("enemy_list", ImVec2(240.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Enemies (%d)", registry->EnemyAssets.Size);
    for (EnemyAsset& enemy : registry->EnemyAssets) {
        bool selected = (enemy.Manifest.Id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::Enemy, enemy.Manifest.Id).Str(), selected)) {
            Selected = enemy.Manifest.Id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("enemy_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (EnemyAsset* enemy = registry->FindEnemyBlueprint(Selected)) {
        DrawEnemyInspector(this, enemy, registry);
    } else {
        ImGui::TextWrapped("Select an enemy, or create one above.");
    }
    ImGui::EndChild();
}

}  // namespace kdk
