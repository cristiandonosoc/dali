#include <dali/game/ui/widgets.h>

#include <cfloat>
#include <cstring>

namespace kdk {

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

bool ClipReferencePicker(const char* label,
                         SpriteSheetClipReference* ref,
                         AssetRegistry* registry,
                         float time,
                         const ClipPickerOptions& options) {
    ImGui::PushID(label);
    ImGui::SeparatorText(label);

    bool changed = false;
    if (AssetSelector("Sheet", EAssetType::SpriteSheet, registry, &ref->SpriteSheetId)) {
        ref->ClipName = {};  // the previous clip belonged to the old sheet
        changed = true;
    }

    SpriteSheetAsset* sheet = registry->FindSpriteSheetAsset(ref->SpriteSheetId);
    const char* clip_preview = ref->ClipName.IsEmpty() ? "(choose clip)" : ref->ClipName.Str();
    if (ImGui::BeginCombo("Clip", clip_preview)) {
        if (sheet) {
            for (SpriteSheetClip& clip : sheet->Clips) {
                bool selected = clip.Name == ref->ClipName;
                if (ImGui::Selectable(clip.Name.Str(), selected)) {
                    ref->ClipName = clip.Name;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }

    if (options.ShowFlip) {
        changed |= ImGui::Checkbox("Flip X", &ref->FlipX);
    }

    const SpriteSheetClip* resolved = ref->Resolve(*registry);
    bool baked = resolved != nullptr;
    if (baked) {
        baked &= resolved->_Resolved != nullptr;
    }
    if (baked) {
        // A small running thumbnail so this clip (mirror included) plays right here, synced to the
        // shared preview clock.
        i32 pos = resolved->At(time, resolved->FPS, true);
        bool has_frame = pos != NONE;
        if (has_frame) {
            has_frame &= pos < resolved->_Frames.Size;
        }
        if (has_frame) {
            FrameUv uv = resolved->_Frames[pos];
            ImVec2 uv0(uv.Uv0.x, uv.Uv0.y);
            ImVec2 uv1(uv.Uv1.x, uv.Uv1.y);
            if (ref->FlipX) {
                float u = uv0.x;
                uv0.x = uv1.x;
                uv1.x = u;
            }
            ImVec2 draw_size;
            if (options.Zoom > 0.0f) {
                draw_size = ImVec2(resolved->_CellSize.x * options.Zoom,
                                   resolved->_CellSize.y * options.Zoom);
            } else {
                float aspect =
                    resolved->_CellSize.y > 0.0f ? resolved->_CellSize.x / resolved->_CellSize.y : 1.0f;
                draw_size = ImVec2(options.FitHeight * aspect, options.FitHeight);
            }
            ImGui::Image((ImTextureID)resolved->_Handle, draw_size, uv0, uv1);
        }
    } else if (ref->SpriteSheetId.IsValid()) {
        ImGui::TextDisabled("unresolved");
    }
    ImGui::PopID();
    return changed;
}

}  // namespace kdk
