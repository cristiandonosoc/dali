#pragma once

#include <dali/core/algorithm.h>
#include <dali/core/memory.h>
#include <dali/core/string.h>
#include <dali/game/assets/registry.h>

#include <imgui.h>

#include <cstring>
#include <span>

// Reusable, registry-aware ImGui widgets for tooling — the asset editor and any debug panel. These
// are dev-facing controls built out of stock ImGui, NOT game UI: an in-game HUD wants its own look
// and its own input rules, so it belongs somewhere else rather than growing out of this file.
// Nothing here owns state — each widget edits a caller-provided value and, where useful, returns
// whether it changed this frame.
//
// This depends on the asset registry, which would be backwards if assets ever became a module of
// their own (tooling should depend on assets, not the reverse). Everything under dali/game is flat
// today, so it costs nothing; revisit when that split actually happens.
namespace kdk {

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

// A search box sized to |width|, drawn just above a left list. The hint shows in the empty box; |id|
// must be unique (a hidden "##..." label) so sibling boxes don't collide.
template <u64 N>
void DrawFilterBox(const char* id, FixedString<N>& filter, float width) {
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint(id, "filter (substring)...", filter.StrMutable(), N)) {
        filter.Size = (u32)std::strlen(filter.StrMutable());
    }
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

// SortedFilteredIds but dispatched by |type| onto the matching registry holder, so a generic (type-
// erased) picker can list any asset type without knowing the concrete container.
std::span<const AssetId*>
    SortedFilteredIdsForType(Arena* arena, AssetRegistry* registry, EAssetType type, StringView filter);

// A filterable, alphabetically-sorted picker for one asset type. Renders as a combo whose open popup
// carries a search box at the top — essential now that a type can hold dozens of assets. Writes the
// chosen id into |*selected| and returns true if it changed this frame. |label| both titles the
// combo and scopes its ImGui id (use "Name##uniq" when two pickers of the same type share a window).
bool AssetSelector(const char* label, EAssetType type, AssetRegistry* registry, AssetId* selected);

// Per-call options for ClipReferencePicker. The defaults suit a compact, stacked inline picker.
struct ClipPickerOptions {
    // Draw a "Flip X" checkbox that edits ref->FlipX (and mirrors the thumbnail). Leave on unless the
    // reference's flip is meaningless to the caller.
    bool ShowFlip = true;
    // Thumbnail sizing. When Zoom > 0 the thumbnail is cellSize * Zoom (integer pixel-art scaling, for
    // a zoomable inspector). Otherwise it's fit to FitHeight px, aspect-preserved (compact and
    // size-independent — right when several pickers stack).
    float Zoom = 0.0f;
    float FitHeight = 48.0f;
};

// Picks the sprite sheet + clip (+ optional flip) for a SpriteSheetClipReference, with a small
// running thumbnail synced to |time|. The reference stores ids resolved live, so switching sheets
// just re-resolves — no relink step. |label| titles the block and scopes the ImGui ids so identical
// pickers don't collide. Returns true if the reference changed this frame.
bool ClipReferencePicker(const char* label,
                         SpriteSheetClipReference* ref,
                         AssetRegistry* registry,
                         float time,
                         const ClipPickerOptions& options = {});

// Editors for every field in ClipEditFields: Cell W/H, Margin, Spacing, FPS, Pivot. This is the ONE
// place the clip-shape widget list lives, shared by the single-clip editor and both batch tabs so
// they can never drift. Returns true if any field changed this frame.
bool DrawClipEditFields(ClipEditFields* fields);

}  // namespace kdk
