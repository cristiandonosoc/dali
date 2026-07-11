#include <dali/game/assets/asset_editor.h>

#include <dali/core/api.h>
#include <dali/core/filesystem.h>
#include <dali/core/memory.h>
#include <dali/game/platform_state.h>

#include <imgui.h>

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
// |dst| (a fixed char buffer of |cap| bytes).
void BrowseForSource(char* dst, u64 cap) {
    PlatformState* ps = GetGlobalPlatformState();
    if (!ps || !ps->API.OpenFileDialog) {
        return;
    }

    auto scratch = Arena::GetScratch();
    Arena* arena = scratch;
    StringView chosen = {};
    FileDialogFilter filters[] = {
        {"Images", "png,jpg,jpeg,bmp,tga"},
    };
    if (!ps->API.OpenFileDialog(arena, &chosen, filters)) {
        return;
    }

    StringView rel = ToWorkingDirRelative(arena, chosen);
    u64 count = rel.Size < cap - 1 ? rel.Size : cap - 1;
    std::memcpy(dst, rel.Str(), count);
    dst[count] = '\0';
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

}  // namespace asset_editor_private

void AssetEditor::Draw(AssetRegistry* registry) {
    using namespace asset_editor_private;

    // Creation form.
    ImGui::SeparatorText("Create Texture");
    ImGui::InputText("Source (raw)", NewSource, sizeof(NewSource));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        BrowseForSource(NewSource, sizeof(NewSource));
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Dir")) {
        OpenSourceFolder(StringView(NewSource));
    }
    ImGui::InputText("Output id", NewId, sizeof(NewId));

    // Show the canonical id the create will actually use.
    AssetId normalized = AssetId::Normalize(StringView(NewId));
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
        if (ImGui::Selectable(tex.Manifest.Id.Value.Str(), selected)) {
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

}  // namespace kdk
