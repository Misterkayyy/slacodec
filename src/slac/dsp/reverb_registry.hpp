#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace slac::dsp {

// ──────────────────────────────────────────────────────────────
// Semantic enums — aligned with brainstorm section 2.6
// ──────────────────────────────────────────────────────────────

enum class ReverbCategory : uint8_t {
    None     = 0,
    Room     = 1,
    Chamber  = 2,
    Hall     = 3,
    Plate    = 4,
    Spring   = 5,
    Ambience = 6,
};

enum class ReverbSize : uint8_t {
    Unspecified = 0,
    Small       = 1,
    Medium      = 2,
    Large       = 3,
    Variable    = 4,
};

enum class ReverbDecay : uint8_t {
    Unspecified = 0,
    VeryShort   = 1,
    Short       = 2,
    Medium      = 3,
    Long        = 4,
};

// ──────────────────────────────────────────────────────────────
// PresetDescriptor — one entry in the registry
// ──────────────────────────────────────────────────────────────

struct PresetDescriptor {
    uint16_t       id;
    ReverbCategory category;
    ReverbSize     size;
    ReverbDecay    decay;

    // Human-readable name for debugging / CLI dump.
    const char* name;

    // Future: handle/path to the actual IR asset in the decoder's library.
    // For now, a placeholder string that would resolve to a bundled .wav.
    const char* ir_asset;
};

// ──────────────────────────────────────────────────────────────
// ResolvedPreset — what the DSP engine actually consumes
// ──────────────────────────────────────────────────────────────

struct ResolvedPreset {
    uint16_t requested_id;   // what the .slac file asked for
    uint16_t resolved_id;    // what the decoder will actually use
    bool     exact_match;    // true if requested == resolved
    bool     is_bypass;      // true if resolved to ID 0

    PresetDescriptor descriptor;
};

// ──────────────────────────────────────────────────────────────
// PresetRegistry — semantic preset database + fallback engine
// ──────────────────────────────────────────────────────────────

class PresetRegistry {
public:
    // Registry version — can be compared against ir_registry_version
    // in the fmt chunk (brainstorm section 2.6 suggestion).
    static constexpr uint32_t kRegistryVersion = 1;

    PresetRegistry() {
        register_standard_presets();
    }

    // ── Lookup ──────────────────────────────────────────────

    // Try exact ID match. Returns nullopt if not found.
    std::optional<PresetDescriptor> find_by_id(uint16_t id) const {
        for (const auto& p : presets_) {
            if (p.id == id) return p;
        }
        return std::nullopt;
    }

    // ── Resolve with fallback ───────────────────────────────
    //
    // This is the main entry point for the DSP pipeline.
    //
    // 1. Try exact match on preset_id.
    // 2. If not found, use fallback_category/size/decay to find
    //    the semantically closest preset in this registry.
    // 3. If nothing reasonable is found, return bypass (ID 0).
    //
    // The fallback parameters come from the spat chunk's
    // "parâmetros descritivos de fallback" (brainstorm §2.4).
    ResolvedPreset resolve(
        uint16_t preset_id,
        uint8_t  fb_category,
        uint8_t  fb_size,
        uint8_t  fb_decay
    ) const {
        // Bypass is always available.
        if (preset_id == 0) {
            return make_resolved(preset_id, get_bypass(), true);
        }

        // Try exact match first.
        if (auto exact = find_by_id(preset_id)) {
            return make_resolved(preset_id, *exact, true);
        }

        // Fallback: find best semantic match.
        auto fb_cat   = static_cast<ReverbCategory>(fb_category);
        auto fb_sz    = static_cast<ReverbSize>(fb_size);
        auto fb_dc    = static_cast<ReverbDecay>(fb_decay);

        // If all fallback params are zero/unspecified, just bypass.
        if (fb_cat == ReverbCategory::None &&
            fb_sz  == ReverbSize::Unspecified &&
            fb_dc  == ReverbDecay::Unspecified) {
            return make_resolved(preset_id, get_bypass(), false);
        }

        // Score every registered preset against the fallback params.
        int best_score = -1;
        const PresetDescriptor* best = nullptr;

        for (const auto& candidate : presets_) {
            // Skip bypass — it's not a useful fallback for "I want reverb".
            if (candidate.id == 0) continue;

            int score = score_match(candidate, fb_cat, fb_sz, fb_dc);
            if (score > best_score) {
                best_score = score;
                best = &candidate;
            }
        }

        if (best != nullptr && best_score > 0) {
            return make_resolved(preset_id, *best, false);
        }

        // Nothing matched at all — bypass.
        return make_resolved(preset_id, get_bypass(), false);
    }

    // ── Registry info ───────────────────────────────────────

    size_t size() const { return presets_.size(); }

    const std::vector<PresetDescriptor>& all_presets() const {
        return presets_;
    }

private:
    std::vector<PresetDescriptor> presets_;

    // ── Standard presets from brainstorm §2.6 ───────────────
    void register_standard_presets() {
        presets_ = {
            { 0, ReverbCategory::None,     ReverbSize::Unspecified, ReverbDecay::Unspecified, "Bypass",      ""            },
            { 1, ReverbCategory::Room,     ReverbSize::Small,       ReverbDecay::Short,       "Small Room",  "room_small"  },
            { 2, ReverbCategory::Chamber,  ReverbSize::Medium,      ReverbDecay::Medium,      "Chamber",     "chamber_med" },
            { 3, ReverbCategory::Hall,     ReverbSize::Large,       ReverbDecay::Long,        "Large Hall",  "hall_large"  },
            { 4, ReverbCategory::Plate,    ReverbSize::Unspecified, ReverbDecay::Medium,      "Plate",       "plate_med"   },
            { 5, ReverbCategory::Spring,   ReverbSize::Small,       ReverbDecay::Short,       "Spring",      "spring_sm"   },
            { 6, ReverbCategory::Ambience, ReverbSize::Variable,    ReverbDecay::VeryShort,   "Ambience",    "amb_vs"      },
        };
    }

    PresetDescriptor get_bypass() const {
        return presets_[0]; // ID 0 is always bypass.
    }

    static ResolvedPreset make_resolved(
        uint16_t requested,
        const PresetDescriptor& desc,
        bool exact
    ) {
        ResolvedPreset r;
        r.requested_id = requested;
        r.resolved_id  = desc.id;
        r.exact_match  = exact;
        r.is_bypass    = (desc.id == 0);
        r.descriptor   = desc;
        return r;
    }

    // ── Scoring ─────────────────────────────────────────────
    //
    // Category dominates, then size, then decay.
    // Adjacent values score partially so that a "Chamber" request
    // can fall back to "Room" or "Hall" rather than "Plate".
    static int score_match(
        const PresetDescriptor& candidate,
        ReverbCategory want_cat,
        ReverbSize     want_sz,
        ReverbDecay    want_dc
    ) {
        int score = 0;

        // Category: +10000 exact, +5000 adjacent.
        score += category_score(candidate.category, want_cat);

        // Size: +100 exact, +50 adjacent.
        score += size_score(candidate.size, want_sz);

        // Decay: +10 exact, +5 adjacent.
        score += decay_score(candidate.decay, want_dc);

        return score;
    }

    static int category_score(ReverbCategory a, ReverbCategory b) {
        if (a == b) return 10000;

        // Define adjacency: Room ↔ Chamber ↔ Hall are "acoustic spaces".
        // Plate ↔ Spring are "mechanical". Ambience is its own thing.
        int ai = category_group(a);
        int bi = category_group(b);
        if (ai == bi) return 5000;

        return 0;
    }

    // Group categories so that related ones can cross-fallback.
    static int category_group(ReverbCategory c) {
        switch (c) {
            case ReverbCategory::Room:
            case ReverbCategory::Chamber:
            case ReverbCategory::Hall:
                return 1; // acoustic spaces

            case ReverbCategory::Plate:
            case ReverbCategory::Spring:
                return 2; // mechanical / artificial

            case ReverbCategory::Ambience:
                return 3; // its own group

            default:
                return 0; // None
        }
    }

    static int size_score(ReverbSize a, ReverbSize b) {
        if (a == b) return 100;

        // Unspecified matches anything weakly.
        if (a == ReverbSize::Unspecified || b == ReverbSize::Unspecified) {
            return 25;
        }

        // Adjacent sizes get partial credit.
        int diff = static_cast<int>(a) - static_cast<int>(b);
        if (diff < 0) diff = -diff;
        if (diff == 1) return 50;

        return 0;
    }

    static int decay_score(ReverbDecay a, ReverbDecay b) {
        if (a == b) return 10;

        if (a == ReverbDecay::Unspecified || b == ReverbDecay::Unspecified) {
            return 2;
        }

        int diff = static_cast<int>(a) - static_cast<int>(b);
        if (diff < 0) diff = -diff;
        if (diff == 1) return 5;

        return 0;
    }
};

} // namespace slac::dsp
