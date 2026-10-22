#include "bidi.h"

#include <hb.h>

#include <algorithm>

namespace bidi {

namespace {

// Bidi classes this implementation uses (BN covers the ignored controls).
enum Type : uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, WS, ON, BN };

bool in(uint32_t c, uint32_t lo, uint32_t hi) {
	return c >= lo && c <= hi;
}

bool r_range(uint32_t c) { // Hebrew, NKo, Samaritan, Mandaic, historic RTL scripts
	return in(c, 0x0590, 0x05FF) || in(c, 0x07C0, 0x085F) || in(c, 0xFB1D, 0xFB4F) || in(c, 0x10800, 0x10FFF) ||
			in(c, 0x1E800, 0x1EFFF);
}

bool al_range(uint32_t c) { // Arabic, Syriac, Thaana and their presentation forms
	return in(c, 0x0600, 0x07BF) || in(c, 0x0860, 0x08FF) || in(c, 0xFB50, 0xFDFF) || in(c, 0xFE70, 0xFEFF);
}

Type classify(uint32_t c) {
	switch (c) {
	case 0x200E: return L; // LRM
	case 0x200F: return R; // RLM
	case 0x061C: return AL; // ALM
	case '+': case '-': case 0x2212: return ES;
	case '#': case '$': case '%': case 0x00B0: case 0x00B1: case 0x066A: return ET;
	case ',': case '.': case '/': case ':': case 0x00A0: case 0x060C: case 0x202F: case 0x2044: return CS;
	case ' ': case '\t': case 0x3000: return WS;
	case 0x00B2: case 0x00B3: case 0x00B9: return EN;
	case 0x066B: case 0x066C: case 0x06DD: return AN;
	}
	if (in(c, '0', '9') || in(c, 0x06F0, 0x06F9) || in(c, 0x2070, 0x2079) || in(c, 0x2080, 0x2089) || in(c, 0xFF10, 0xFF19)) {
		return EN;
	}
	if (in(c, 0x0660, 0x0669) || in(c, 0x0600, 0x0605)) return AN;
	if (in(c, 0x00A2, 0x00A5) || in(c, 0x20A0, 0x20CF) || in(c, 0x2030, 0x2034)) return ET;
	if (in(c, 0x2000, 0x200A)) return WS;
	if (in(c, 0x200B, 0x200D) || in(c, 0x202A, 0x202E) || in(c, 0x2060, 0x2069) || c == 0xFEFF || c == 0x00AD) return BN;
	const hb_unicode_general_category_t cat = hb_unicode_general_category(hb_unicode_funcs_get_default(), c);
	if (cat == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK || cat == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK) return NSM;
	if (r_range(c)) return R;
	if (al_range(c)) return AL;
	switch (cat) {
	case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
	case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
	case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
	case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
	case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
	case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
	case HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER: // non-European digits (e.g. Devanagari) are L
	case HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER:
		return L;
	default:
		return ON;
	}
}

bool strong_or_number(Type t) {
	return t == L || t == R || t == AL || t == EN || t == AN;
}

} // namespace

bool is_rtl(uint32_t c) {
	const Type t = classify(c);
	return t == R || t == AL;
}

int resolve(const std::vector<uint32_t> &text, std::vector<uint8_t> &levels) {
	const size_t n = text.size();
	std::vector<Type> t(n);
	for (size_t i = 0; i < n; i++) t[i] = classify(text[i]);

	// P2/P3: paragraph level from the first strong character.
	int para = 0;
	for (Type x : t) {
		if (x == L) break;
		if (x == R || x == AL) {
			para = 1;
			break;
		}
	}
	const Type e = para ? R : L; // embedding direction; also sos and eos

	// W1: marks (and ignored controls) take the type before them.
	for (size_t i = 0; i < n; i++) {
		if (t[i] == NSM || t[i] == BN) t[i] = i == 0 ? e : t[i - 1];
	}
	// W2: European digits after Arabic letters are Arabic numbers. W3: AL -> R.
	Type last_strong = e;
	for (size_t i = 0; i < n; i++) {
		if (t[i] == L || t[i] == R || t[i] == AL) last_strong = t[i];
		if (t[i] == EN && last_strong == AL) t[i] = AN;
	}
	for (Type &x : t) {
		if (x == AL) x = R;
	}
	// W4: one separator between two numbers of the same kind joins them.
	for (size_t i = 1; i + 1 < n; i++) {
		if (t[i] == ES && t[i - 1] == EN && t[i + 1] == EN) t[i] = EN;
		if (t[i] == CS && t[i - 1] == EN && t[i + 1] == EN) t[i] = EN;
		if (t[i] == CS && t[i - 1] == AN && t[i + 1] == AN) t[i] = AN;
	}
	// W5: terminators ($, %, °) next to European numbers belong to them.
	for (size_t i = 0; i < n; i++) {
		if (t[i] != ET) continue;
		size_t j = i;
		while (j < n && t[j] == ET) j++;
		const bool next_to_en = (i > 0 && t[i - 1] == EN) || (j < n && t[j] == EN);
		for (size_t k = i; k < j; k++) t[k] = next_to_en ? EN : ON; // W6 for the rest
		i = j - 1;
	}
	// W6: remaining separators are neutral.
	for (Type &x : t) {
		if (x == ES || x == CS) x = ON;
	}
	// W7: European numbers in left-to-right context are L.
	last_strong = e;
	for (size_t i = 0; i < n; i++) {
		if (t[i] == L || t[i] == R) last_strong = t[i];
		if (t[i] == EN && last_strong == L) t[i] = L;
	}
	// N1/N2: neutrals between two characters of the same direction take it
	// (numbers count as R); others take the embedding direction.
	for (size_t i = 0; i < n; i++) {
		if (strong_or_number(t[i])) continue;
		size_t j = i;
		while (j < n && !strong_or_number(t[j])) j++;
		auto dir = [](Type x) { return x == L ? L : R; };
		const Type before = i == 0 ? e : dir(t[i - 1]);
		const Type after = j == n ? e : dir(t[j]);
		const Type resolved = before == after ? before : e;
		for (size_t k = i; k < j; k++) t[k] = resolved;
		i = j - 1;
	}
	// I1/I2: levels.
	levels.assign(n, (uint8_t)para);
	for (size_t i = 0; i < n; i++) {
		if (para == 0) {
			if (t[i] == R) levels[i] = 1;
			else if (t[i] == AN || t[i] == EN) levels[i] = 2;
		} else if (t[i] == L || t[i] == EN || t[i] == AN) {
			levels[i] = 2;
		}
	}
	// L1: trailing white space goes back to the paragraph level.
	for (size_t i = n; i > 0 && classify(text[i - 1]) == WS; i--) levels[i - 1] = (uint8_t)para;
	return para;
}

std::vector<int> visual_order(const std::vector<uint8_t> &levels) {
	std::vector<int> order(levels.size());
	for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
	if (levels.empty()) return order;
	const uint8_t highest = *std::max_element(levels.begin(), levels.end());
	uint8_t lowest_odd = 255;
	for (uint8_t l : levels) {
		if (l % 2 == 1 && l < lowest_odd) lowest_odd = l;
	}
	if (lowest_odd == 255) return order; // all left to right
	// L2: from the highest level down to the lowest odd one, reverse every
	// run at that level or higher.
	for (int level = highest; level >= lowest_odd; level--) {
		for (size_t i = 0; i < order.size();) {
			if (levels[order[i]] < level) {
				i++;
				continue;
			}
			size_t j = i;
			while (j < order.size() && levels[order[j]] >= level) j++;
			std::reverse(order.begin() + i, order.begin() + j);
			i = j;
		}
	}
	return order;
}

} // namespace bidi
