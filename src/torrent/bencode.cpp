#include "bencode.h"

#include <cstring>

namespace torrent {

const BNode *BNode::get(const char *key) const {
	for (const auto &kv : dict) {
		if (kv.first == key) return &kv.second;
	}
	return nullptr;
}

int64_t BNode::get_int(const char *key, int64_t fallback) const {
	const BNode *n = get(key);
	return n && n->type == INT ? n->i : fallback;
}

std::string BNode::get_string(const char *key) const {
	const BNode *n = get(key);
	return n && n->type == STRING ? n->s : std::string();
}

namespace {

bool decode(const uint8_t *&p, const uint8_t *e, BNode &out, int depth) {
	if (p >= e || depth > 64) return false;
	out.begin = p;
	if (*p == 'i') {
		p++;
		const bool neg = p < e && *p == '-';
		if (neg) p++;
		if (p >= e || *p < '0' || *p > '9') return false;
		int64_t v = 0;
		while (p < e && *p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
		if (p >= e || *p != 'e') return false;
		p++;
		out.type = BNode::INT;
		out.i = neg ? -v : v;
	} else if (*p >= '0' && *p <= '9') {
		uint64_t n = 0;
		while (p < e && *p >= '0' && *p <= '9') {
			n = n * 10 + (*p++ - '0');
			if (n > (1u << 30)) return false;
		}
		if (p >= e || *p != ':' || (uint64_t)(e - p - 1) < n) return false;
		p++;
		out.type = BNode::STRING;
		out.s.assign((const char *)p, (size_t)n);
		p += n;
	} else if (*p == 'l' || *p == 'd') {
		const bool is_dict = *p == 'd';
		p++;
		out.type = is_dict ? BNode::DICT : BNode::LIST;
		while (p < e && *p != 'e') {
			if (is_dict) {
				BNode key;
				if (!decode(p, e, key, depth + 1) || key.type != BNode::STRING) return false;
				out.dict.emplace_back(std::move(key.s), BNode());
				if (!decode(p, e, out.dict.back().second, depth + 1)) return false;
			} else {
				out.list.emplace_back();
				if (!decode(p, e, out.list.back(), depth + 1)) return false;
			}
		}
		if (p >= e) return false;
		p++;
	} else {
		return false;
	}
	out.end = p;
	return true;
}

} // namespace

bool bdecode(const uint8_t *&p, const uint8_t *end, BNode &out) {
	return decode(p, end, out, 0);
}

bool bdecode(const std::string &data, BNode &out) {
	const uint8_t *p = (const uint8_t *)data.data();
	const uint8_t *e = p + data.size();
	return decode(p, e, out, 0) && p == e;
}

std::string bstr(const std::string &s) {
	return std::to_string(s.size()) + ":" + s;
}

std::string bint(int64_t v) {
	return "i" + std::to_string(v) + "e";
}

} // namespace torrent
