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
    ImGui::BeginChild("preview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    if (res.IsValid()) {
        ImVec2 size(res.Width * editor->PreviewZoom, res.Height * editor->PreviewZoom);
        ImGui::Image((ImTextureID)res.ImGuiId(), size);
    }
    ImGui::EndChild();
}

// Grid-overlay preview of one texture ref: the texture with its cell boundaries drawn on top
// (reusing the ref's own FrameRect so it matches what the runtime samples), the selected frame
// highlighted.
void DrawTextureRefPreview(AssetEditor* editor, const SpriteTextureRef& ref) {
    if (!ref._Resolved) {
        ImGui::TextWrapped("Texture '%s' is not resolved.", ShortId(EAssetType::Texture, ref.Texture).Str());
        return;
    }
    const Texture& res = ref._Resolved->Resource;
    i32 frame_count = ref.FrameCount();

    ImGui::SliderFloat("Zoom", &editor->PreviewZoom, 0.5f, 16.0f, "%.1fx");
    if (frame_count > 0) {
        i32 max_frame = frame_count - 1;
        if (editor->PreviewFrame < 0) {
            editor->PreviewFrame = 0;
        }
        if (editor->PreviewFrame > max_frame) {
            editor->PreviewFrame = max_frame;
        }
        ImGui::SliderInt("Frame", &editor->PreviewFrame, 0, max_frame);
    }

    // Fixed, modest height so the preview doesn't eat the whole inspector (and hide the Clips
    // section below it); it scrolls if the image is larger.
    ImGui::BeginChild("ref_preview", ImVec2(0.0f, 300.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    if (res.IsValid()) {
        float zoom = editor->PreviewZoom;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)res.ImGuiId(), ImVec2(res.Width * zoom, res.Height * zoom));

        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (i32 f = 0; f < frame_count; ++f) {
            SpriteTextureRef::FrameUV uv = ref.FrameRect(f);
            ImVec2 tl(origin.x + uv.Uv0.x * res.Width * zoom, origin.y + uv.Uv0.y * res.Height * zoom);
            ImVec2 br(origin.x + uv.Uv1.x * res.Width * zoom, origin.y + uv.Uv1.y * res.Height * zoom);
            bool is_selected = (f == editor->PreviewFrame);
            ImU32 color = is_selected ? IM_COL32(255, 220, 60, 255) : IM_COL32(255, 255, 255, 90);
            float thickness = is_selected ? 2.0f : 1.0f;
            draw->AddRect(tl, br, color, 0.0f, 0, thickness);
        }
    }
    ImGui::EndChild();
}

// Playback preview for one clip: editor-local fps/loop advance the clip and draw its current frame
// (the frame's sub-rect of the referenced texture). Playback params live here, not on the clip.
void DrawClipPlayback(AssetEditor* editor, SpritesheetAsset* sheet, SpriteClip& clip) {
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

    const SpriteTextureRef* ref = sheet->FindTextureRef(clip.Texture);
    if (!ref) {
        ImGui::TextWrapped("References texture '%s', not in this sheet.",
                           ShortId(EAssetType::Texture, clip.Texture).Str());
        return;
    }
    if (ImGui::Button("Fill all frames")) {
        clip.Frames.Clear();
        i32 count = ref->FrameCount();
        for (i32 f = 0; f < count; ++f) {
            if (clip.Frames.IsFull()) {
                break;
            }
            clip.Frames.Push(f);
        }
    }

    if (editor->ClipPlaying) {
        editor->ClipTime += ImGui::GetIO().DeltaTime;
    }

    i32 frame = clip.At(editor->ClipTime, editor->ClipFps, editor->ClipLoop);
    if (frame == NONE) {
        ImGui::TextDisabled("(empty clip)");
        return;
    }
    SpriteTextureRef::FrameUV uv = ref->FrameRect(frame);
    ImGui::Text("Frame %d", frame);
    float zoom = editor->PreviewZoom;
    ImGui::Image((ImTextureID)uv.Handle,
                 ImVec2(uv.Width * zoom, uv.Height * zoom),
                 ImVec2(uv.Uv0.x, uv.Uv0.y),
                 ImVec2(uv.Uv1.x, uv.Uv1.y));
}

void DrawSpritesheetInspector(AssetEditor* editor, AssetRegistry* registry, SpritesheetAsset* sheet) {
    ImGui::Text("Id: %s", sheet->Manifest.Id.Value.Str());
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        sheet->SaveManifest();
    }

    // --- Texture references (each with its own grid) ---
    ImGui::SeparatorText("Texture References");

    // The textures already loaded in the registry, available to add as references. Already-added
    // ones are greyed out.
    ImGui::TextDisabled("Loaded textures (%d)", registry->Textures.Size);
    ImGui::BeginChild("loaded_textures", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders);
    for (TextureAsset& tex : registry->Textures) {
        ImGui::PushID((const void*)&tex);
        bool referenced = sheet->FindTextureRef(tex.Manifest.Id) != nullptr;
        ImGui::BeginDisabled(referenced);
        if (ImGui::SmallButton("Add")) {
            if (!sheet->Textures.IsFull()) {
                SpriteTextureRef ref = {};
                ref.Texture = tex.Manifest.Id;
                sheet->Textures.Push(ref);
                sheet->ResolveReferences(*registry);  // resolve the freshly added ref
                editor->SelectedRef = tex.Manifest.Id;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s", ShortId(EAssetType::Texture, tex.Manifest.Id).Str());
        ImGui::PopID();
    }
    ImGui::EndChild();

    // The references this sheet already has: pick one to preview, edit its grid, remove.
    i32 remove_ref = NONE;
    for (i32 i = 0; i < sheet->Textures.Size; ++i) {
        SpriteTextureRef& ref = sheet->Textures[i];
        ImGui::PushID((const void*)&ref);
        bool is_selected = ref.Texture == editor->SelectedRef;
        if (ImGui::RadioButton("##sel", is_selected)) {
            editor->SelectedRef = ref.Texture;
        }
        ImGui::SameLine();
        ImGui::Text("%s  (%d frames)", ShortId(EAssetType::Texture, ref.Texture).Str(), ref.FrameCount());
        // Only the selected reference expands its grid controls; the rest stay one-liners.
        if (is_selected) {
            ImGui::InputInt("Cell W", &ref.Grid.CellW);
            ImGui::InputInt("Cell H", &ref.Grid.CellH);
            ImGui::InputInt("Margin", &ref.Grid.Margin);
            ImGui::InputInt("Spacing", &ref.Grid.Spacing);
            if (ImGui::Button("Remove")) {
                remove_ref = i;
            }
            ImGui::Separator();
        }
        ImGui::PopID();
    }
    if (remove_ref != NONE) {
        sheet->Textures.RemoveUnorderedAt(remove_ref);
    }

    if (const SpriteTextureRef* ref = sheet->FindTextureRef(editor->SelectedRef)) {
        DrawTextureRefPreview(editor, *ref);
    }

    // --- Clips (each over one texture ref) ---
    ImGui::SeparatorText("Clips");

    ImGui::InputText("Name", editor->NewClipName, sizeof(editor->NewClipName));
    const char* clip_ref_preview =
        editor->NewClipTexture.IsValid()
            ? ShortId(EAssetType::Texture, editor->NewClipTexture).Str()
            : "(choose ref)";
    if (ImGui::BeginCombo("Ref", clip_ref_preview)) {
        for (SpriteTextureRef& ref : sheet->Textures) {
            bool selected = ref.Texture == editor->NewClipTexture;
            if (ImGui::Selectable(ShortId(EAssetType::Texture, ref.Texture).Str(), selected)) {
                editor->NewClipTexture = ref.Texture;
            }
        }
        ImGui::EndCombo();
    }

    bool can_add_clip = editor->NewClipName[0] != '\0';
    can_add_clip &= editor->NewClipTexture.IsValid();
    ImGui::BeginDisabled(!can_add_clip);
    if (ImGui::Button("Add Clip")) {
        if (!sheet->Clips.IsFull()) {
            SpriteClip clip = {};
            clip.Name = StringView(editor->NewClipName);
            clip.Texture = editor->NewClipTexture;
            // Default to all of the ref's frames in order (the common one-texture-per-clip case).
            if (const SpriteTextureRef* ref = sheet->FindTextureRef(clip.Texture)) {
                i32 count = ref->FrameCount();
                for (i32 f = 0; f < count; ++f) {
                    if (clip.Frames.IsFull()) {
                        break;
                    }
                    clip.Frames.Push(f);
                }
            }
            sheet->Clips.Push(clip);
        }
    }
    ImGui::EndDisabled();

    i32 remove_clip = NONE;
    for (i32 i = 0; i < sheet->Clips.Size; ++i) {
        SpriteClip& clip = sheet->Clips[i];
        ImGui::PushID((const void*)&clip);
        bool is_selected = clip.Name == editor->SelectedClip;
        if (ImGui::RadioButton("##selclip", is_selected)) {
            editor->SelectedClip = clip.Name;
            editor->ClipTime = 0.0f;
        }
        ImGui::SameLine();
        ImGui::Text("%s  <-  %s  [%d frames]",
                    clip.Name.Str(),
                    ShortId(EAssetType::Texture, clip.Texture).Str(),
                    clip.Frames.Size);
        // Only the selected clip expands to playback controls + remove.
        if (is_selected) {
            DrawClipPlayback(editor, sheet, clip);
            if (ImGui::Button("Remove")) {
                remove_clip = i;
            }
            ImGui::Separator();
        }
        ImGui::PopID();
    }
    if (remove_clip != NONE) {
        sheet->Clips.RemoveUnorderedAt(remove_clip);
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
    if (SpritesheetAsset* sheet = registry->FindSpritesheet(id)) {
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
            offset_label = Printf(arena, "UTC%c%02d:%02d", sign, abs_seconds / 3600, (abs_seconds % 3600) / 60);
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
// strips the assets/ prefix and the extension, so a dirty .yml and .asset land on the same id. Lines
// that don't map to an asset (or duplicates) are ignored.
void CollectDirtyFromLine(StringView line, FixedVector<AssetId, AssetEditor::kMaxDirtyAssets>* dirty) {
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
        "git"sv, "-C"sv, repo, "status"sv, "--porcelain"sv, "--"sv, "assets"sv,
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
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f), "Git: error (exit %d)", editor->GitExitCode);
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
void DrawEnemyInspector(EnemyAsset* enemy) {
    ImGui::Text("Id: %s", enemy->Manifest.Id.Value.Str());
    ImGui::Separator();

    InstanceData& data = enemy->Data;
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
        case EAssetType::Spritesheet: {
            DrawSpritesheetTab(registry);
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
    for (TextureAsset& tex : registry->Textures) {
        DrawDatabaseRow(this, EAssetType::Texture, tex.Manifest.Id);
    }

    ImGui::SeparatorText("Spritesheets");
    for (SpritesheetAsset& sheet : registry->Spritesheets) {
        DrawDatabaseRow(this, EAssetType::Spritesheet, sheet.Manifest.Id);
    }

    ImGui::SeparatorText("Enemies");
    for (EnemyAsset& enemy : registry->EnemyBlueprints) {
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
    ImGui::TextDisabled("Textures (%d)", registry->Textures.Size);
    for (TextureAsset& tex : registry->Textures) {
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

void AssetEditor::DrawSpritesheetTab(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Creation form: a spritesheet is a concept, created from just its id. Textures and clips are
    // added in the inspector.
    ImGui::SeparatorText("Create Spritesheet");

    ImGui::InputText("Output id", NewSheetId, sizeof(NewSheetId));
    AssetId normalized = AssetIdFromShort(EAssetType::Spritesheet, StringView(NewSheetId));
    ImGui::TextDisabled("-> %s", normalized.Value.Str());

    if (ImGui::Button("Create")) {
        if (SpritesheetAsset::Create(normalized)) {
            registry->LoadSpritesheet(normalized);
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
    ImGui::TextDisabled("Spritesheets (%d)", registry->Spritesheets.Size);
    for (SpritesheetAsset& sheet : registry->Spritesheets) {
        bool selected = (sheet.Manifest.Id == Selected);
        if (ImGui::Selectable(ShortId(EAssetType::Spritesheet, sheet.Manifest.Id).Str(), selected)) {
            Selected = sheet.Manifest.Id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Inspector pane.
    ImGui::BeginChild("sheet_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (SpritesheetAsset* sheet = registry->FindSpritesheet(Selected)) {
        DrawSpritesheetInspector(this, registry, sheet);
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
    ImGui::TextDisabled("Enemies (%d)", registry->EnemyBlueprints.Size);
    for (EnemyAsset& enemy : registry->EnemyBlueprints) {
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
        DrawEnemyInspector(enemy);
    } else {
        ImGui::TextWrapped("Select an enemy, or create one above.");
    }
    ImGui::EndChild();
}

}  // namespace kdk
