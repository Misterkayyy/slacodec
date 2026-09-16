#include <slac/dsp/reverb_registry.hpp>

#include <iostream>
#include <string>

using namespace slac::dsp;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "[OK] " << label << "\n";
        ++g_pass;
    } else {
        std::cerr << "[FAIL] " << label << "\n";
        ++g_fail;
    }
}

int main() {
    PresetRegistry registry;

    // ── 1. All standard presets are registered ──────────────
    check(registry.size() == 7, "Registry has 7 standard presets (ID 0-6)");

    // ── 2. Exact match for every standard ID ────────────────
    for (uint16_t id = 0; id <= 6; ++id) {
        auto result = registry.resolve(id, 0, 0, 0);
        check(result.exact_match, "ID " + std::to_string(id) + " exact match");
        check(result.resolved_id == id, "ID " + std::to_string(id) + " resolved to self");
    }

    // ── 3. Bypass ───────────────────────────────────────────
    {
        auto r = registry.resolve(0, 0, 0, 0);
        check(r.is_bypass, "ID 0 is bypass");
        check(r.exact_match, "ID 0 is exact match");
    }

    // ── 4. Unknown ID with fallback to Hall ─────────────────
    //
    // Simulate a future preset ID=10 that this decoder doesn't know.
    // The spat chunk says: category=Hall, size=Large, decay=Long.
    // The decoder should fall back to ID 3 (Large Hall).
    {
        auto r = registry.resolve(
            10,  // unknown future ID
            static_cast<uint8_t>(ReverbCategory::Hall),
            static_cast<uint8_t>(ReverbSize::Large),
            static_cast<uint8_t>(ReverbDecay::Long)
        );

        check(!r.exact_match, "ID 10 is not exact match");
        check(r.resolved_id == 3, "ID 10 falls back to ID 3 (Large Hall)");
        check(!r.is_bypass, "Fallback is not bypass");
    }

    // ── 5. Unknown ID with fallback to Room category ────────
    //
    // Category=Room, size=Small, decay=Short → should match ID 1.
    {
        auto r = registry.resolve(
            99,
            static_cast<uint8_t>(ReverbCategory::Room),
            static_cast<uint8_t>(ReverbSize::Small),
            static_cast<uint8_t>(ReverbDecay::Short)
        );

        check(r.resolved_id == 1, "Room/Small/Short falls back to ID 1");
    }

    // ── 6. Category group fallback ──────────────────────────
    //
    // Request Chamber but with decay=Long (unusual combo).
    // Chamber is ID 2 with Medium decay. Hall (ID 3) has Long decay.
    // Both are "acoustic spaces" group. Hall should win because
    // decay=Long matches Hall's Long exactly.
    {
        auto r = registry.resolve(
            200,
            static_cast<uint8_t>(ReverbCategory::Chamber),
            static_cast<uint8_t>(ReverbSize::Medium),
            static_cast<uint8_t>(ReverbDecay::Long)
        );

        // Chamber (ID 2) and Hall (ID 3) are in same group.
        // Hall has Long decay which matches exactly.
        // Chamber has Medium decay which is adjacent to Long.
        // Hall should score higher:
        //   Hall:    cat_group=1(+5000) + size Medium(+100) + decay Long(+10) = 5110
        //   Chamber: cat exact(+10000) + size Medium(+100) + decay adj(+5)    = 10105
        // Actually Chamber wins because exact category beats group match.
        check(r.resolved_id == 2, "Chamber/Medium/Long stays with Chamber (exact category wins)");
    }

    // ── 7. Cross-group fallback ─────────────────────────────
    //
    // Request Plate but this hypothetical registry only has Spring
    // in the mechanical group. Spring should be the fallback.
    {
        auto r = registry.resolve(
            300,
            static_cast<uint8_t>(ReverbCategory::Plate),
            static_cast<uint8_t>(ReverbSize::Unspecified),
            static_cast<uint8_t>(ReverbDecay::Medium)
        );

        // Plate (ID 4) exists! So this should be exact match.
        check(!r.exact_match, "ID 300 is not exact (unknown, falls back)");
	check(r.resolved_id == 4, "ID 300 falls back to Plate (ID 4)");
    }

    // ── 8. All-zero fallback params → bypass ────────────────
    {
        auto r = registry.resolve(500, 0, 0, 0);
        check(r.is_bypass, "Unknown ID + zero fallback params = bypass");
        check(!r.exact_match, "Not exact match");
    }

    // ── 9. Spring fallback for unknown mechanical ───────────
    //
    // Request a future Spring variant. Category=Spring, size=Small,
    // decay=Short. Should match standard Spring (ID 5).
    {
        auto r = registry.resolve(
            42,
            static_cast<uint8_t>(ReverbCategory::Spring),
            static_cast<uint8_t>(ReverbSize::Small),
            static_cast<uint8_t>(ReverbDecay::Short)
        );

        check(r.resolved_id == 5, "Spring/Small/Short falls back to ID 5");
    }

    // ── 10. Ambience fallback ───────────────────────────────
    {
        auto r = registry.resolve(
            77,
            static_cast<uint8_t>(ReverbCategory::Ambience),
            static_cast<uint8_t>(ReverbSize::Variable),
            static_cast<uint8_t>(ReverbDecay::VeryShort)
        );

        check(r.resolved_id == 6, "Ambience falls back to ID 6");
    }

    // ── Summary ─────────────────────────────────────────────
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed.\n";

    if (g_fail > 0) {
        return 1;
    }

    std::cout << "\n--- SLAC Reverb Preset Registry PASSED ---\n";
    return 0;
}
