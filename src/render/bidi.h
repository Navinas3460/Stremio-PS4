// Bidirectional text (Unicode UAX #9) for one line of Arabic, Hebrew or
// mixed text. Simplified: one paragraph, no explicit embeddings or isolates
// (their control characters are ignored), which is what subtitles and titles
// need. Mirrored brackets are left to HarfBuzz.
#pragma once

#include <cstdint>
#include <vector>

namespace bidi {

// Sets each character's embedding level (odd = right to left) and returns
// the paragraph level: 1 if the first strong character is right to left.
int resolve(const std::vector<uint32_t> &text, std::vector<uint8_t> &levels);

// The display order (rule L2), left to right, of items with these levels:
// indices into `levels`.
std::vector<int> visual_order(const std::vector<uint8_t> &levels);

// Whether `c` is a right-to-left letter (Hebrew, Arabic, Syriac...).
bool is_rtl(uint32_t c);

} // namespace bidi
